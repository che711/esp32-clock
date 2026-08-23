#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <time.h>
#include "config.h"
#include "app.h"
#include "display.h"
#include "web_api.h"
#include "sensor.h"
#include "battery.h"
#include "power.h"
#include "mqtt.h"

// ============================================================
//  main.cpp — жизненный цикл устройства: сеть, время, датчики
//  и главный цикл. Экран — в display.cpp, веб — в web_api.cpp.
// ============================================================

// ── Общее состояние (объявлено в app.h) ──────────────────
SensorData  weather{};
BatteryData battery{};
Stopwatch   stopwatch;

char   timeBuf[9];
char   dateBuf[20];
char   dayShortBuf[5];
char   dayFullBuf[12];
bool   timeSynced = false;
String localIP    = "";

static uint32_t lastSensorMs = 0;
// Взводится, когда кадр надо перерисовать не дожидаясь смены секунды
static bool     forceRedraw  = false;

static const char* DAYS_SHORT[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
static const char* DAYS_FULL[]  = { "Sunday","Monday","Tuesday","Wednesday",
                                    "Thursday","Friday","Saturday" };
static const char* MONTHS[]     = { "JAN","FEB","MAR","APR","MAY","JUN",
                                    "JUL","AUG","SEP","OCT","NOV","DEC" };

// Локальное время «прямо сейчас», без ожидания.
// Своя реализация вместо getLocalTime(&t, 0): та отмеряет таймаут как
// while (millis() - start <= ms) и при ms == 0 возвращает false, ни разу
// не прочитав часы, если между двумя millis() успел смениться тик, —
// а вытеснение loop-задачи планировщиком делает это регулярно. Экран
// в такие моменты на секунду показывал прочерки при исправных часах.
static bool localTimeNow(struct tm* t, time_t now) {
    localtime_r(&now, t);
    return t->tm_year > (2016 - 1900);   // тот же критерий «время задано»
}

static bool localTimeNow(struct tm* t) {
    return localTimeNow(t, time(nullptr));
}

// WS2812 на GPIO8: pinMode НЕ вызывать — сломает RMT адресного LED
static void ledColor(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(LED_PIN, r, g, b);
}

// Температура кристалла: читаем не чаще раза в 10 с (temperatureRead
// на C6 может блокировать на десятки мс — незачем дёргать каждую секунду).
float dieTempC() {
    static float    cached = 0.0f;
    static uint32_t last   = 0;
    uint32_t now = millis();
    if (last == 0 || now - last >= 10000) {
        last   = now ? now : 1;
        cached = temperatureRead();
    }
    return cached;
}

// ─── Яркость и расписание экрана ─────────────────────────
// В эконом-режимах вне рабочего окна панель гасится совсем: это самая
// крупная статья расхода, и ночью она всё равно никому не светит.
// Ручное выключение из дашборда расписание не переигрывает — включаем
// обратно только то, что сами же и погасили.
static bool screenOffBySchedule = false;

// Ночью посмотреть на часы всё-таки надо, поэтому команда «включить экран»
// работает и в запрещённый час — но не навсегда: держим панель
// POWER_SCREEN_PEEK_MS и гасим сами. Забыть выключить легко, а расплата за
// забытый экран — самый крупный потребитель, горящий до утра.
static uint32_t screenPeekUntil = 0;      // 0 — подсветки нет

// Сравнение через знаковую разность, а не «now < until»: так подсветка
// переживает переполнение millis() на 49-м дне работы.
static bool screenPeekActive(uint32_t now) {
    if (screenPeekUntil == 0) return false;
    if ((int32_t)(now - screenPeekUntil) >= 0) {
        screenPeekUntil = 0;
        Serial.println("Display: night peek expired");
        return false;
    }
    return true;
}

// Час известен — ставим уровень по расписанию, нет — уровень «времени нет».
// В ручном режиме обе функции внутри ничего не делают.
static void applyAutoLevelFor(bool haveHour, int hour) {
    if (haveHour) displayAutoForHour(hour);
    else          displayAutoNoTime();
}

void applyAutoBrightness() {
    struct tm t;
    // Раньше функция выходила первой же строкой по !timeSynced — и вместе с
    // расписанием отключались и яркость, и рубеж по заряду: без синхронизации
    // NTP панель оставалась на стартовом уровне, а порог POWER_SCREEN_OFF_PCT
    // не срабатывал никогда. Устройство без сети жгло экран до brownout.
    // Теперь без времени пропускается только то, что без часа не считается.
    bool haveHour = timeSynced && localTimeNow(&t);
    int  hour     = haveHour ? t.tm_hour : 0;

    bool allowed = powerScreenBatteryOkNow()
                   && (!haveHour || powerScreenScheduleAllowsNow(hour));

    // Пока идёт подсветка, расписание уступает — но только ей одной:
    // истечёт окно, и ближайший же заход погасит панель обычным путём.
    if (!allowed && screenPeekActive(millis())) {
        applyAutoLevelFor(haveHour, hour);
        return;
    }

    // Час стал рабочим — подсветку снимаем: экран и так горит по расписанию,
    // а оставленный отсчёт врал бы в дашборде про скорое гашение.
    if (allowed) screenPeekUntil = 0;

    if (!allowed) {
        if (displayIsOn()) {
            displaySetPower(false);
            screenOffBySchedule = true;
        }
    } else if (screenOffBySchedule && !displayIsOn()) {
        displaySetPower(true);
        screenOffBySchedule = false;
    } else {
        screenOffBySchedule = false;
    }

    applyAutoLevelFor(haveHour, hour);
}

// Сколько ещё гореть по подсветке, секунды; 0 — подсветки нет. Нужно
// дашборду: без обратного отсчёта самопроизвольное гашение через полминуты
// выглядит сбоем, а не задумкой.
uint32_t screenPeekLeftS() {
    if (screenPeekUntil == 0) return 0;
    int32_t left = (int32_t)(screenPeekUntil - millis());   // знаковая: см. выше
    if (left <= 0) return 0;
    return ((uint32_t)left + 999) / 1000;                   // вверх, до целых секунд
}

// Команда питания экрана из дашборда. Отдельно от displaySetPower(), потому
// что кроме самой панели трогает расписание: включение в запрещённый час
// заводит подсветку, выключение снимает её досрочно.
void screenSetPower(bool on) {
    displaySetPower(on);

    if (!on) {
        screenPeekUntil = 0;         // погасили сами — досматривать нечего
        return;
    }

    struct tm t;
    if (timeSynced && localTimeNow(&t) && !powerScreenAllowedNow(t.tm_hour)) {
        screenPeekUntil = millis() + POWER_SCREEN_PEEK_MS;
        if (screenPeekUntil == 0) screenPeekUntil = 1;   // 0 занят под «нет подсветки»
        Serial.printf("Display: night peek %lus\n",
                      (unsigned long)(POWER_SCREEN_PEEK_MS / 1000));
    } else {
        screenPeekUntil = 0;         // час рабочий — гасить по таймеру незачем
    }
}

// ─── Секундомер: команды ─────────────────────────────────
// Экономия радио. MAX_MODEM холоднее MIN, но задерживает доставку пакетов
// до ~0.9 с — пока идёт отсчёт секундомера, сон выключаем совсем.
static void setRadioSaving(bool save) {
    WiFi.setSleep(save);
    if (save) esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

void swStart() {
    if (stopwatch.start(millis())) {
        displayInvalidateStopwatch();   // первый кадр после старта — полный
        setRadioSaving(false);
        Serial.println("Stopwatch START");
    }
}

void swPause() {
    if (stopwatch.pause(millis())) {
        setRadioSaving(true);           // счётчик заморожен, точность больше не нужна
        Serial.println("Stopwatch PAUSE");
    }
}

void swReset() {
    stopwatch.reset();
    forceRedraw = true;                 // вернуть часы на экран сразу, а не через секунду
    displayInvalidateStopwatch();
    setRadioSaving(true);
    Serial.println("Stopwatch RESET");
}

// ─── WiFi + NTP ───────────────────────────────────────────
static void connectWifi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        localIP = WiFi.localIP().toString();
        Serial.printf("\nIP: %s\n", localIP.c_str());
        setRadioSaving(true);
        powerApplyRadio();          // мощность и listen_interval под режим
        // end() перед begin(): сюда заходят и повторно — после возврата из
        // выживания, где радио выключалось вместе с ответчиком mDNS.
        MDNS.end();
        if (MDNS.begin(DEVICE_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS: http://%s.local\n", DEVICE_HOSTNAME);
        }
    } else {
        Serial.println("\nWiFi FAILED");
    }
}

// Момент последнего запуска SNTP. Общий для setup() и maintainNetwork(),
// чтобы ретрай отсчитывался от старта, а не от первого захода в цикл.
static uint32_t lastNtpMs = 0;

// Поднимает SNTP-демона и задаёт TZ-правило (DST считается автоматически).
// Вызывается и без связи: TZ должен быть установлен в любом случае, а демон
// сам ретраит запрос, когда сеть появится.
static void startNTP() {
    configTzTime(TZ_INFO, NTP_SERVER);
    lastNtpMs = millis();
}

static void syncNTP() {
    startNTP();
    // Без связи ждать нечего — уйдём в loop(), ретрай сделает maintainNetwork().
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("NTP: no WiFi, retry in background");
        return;
    }
    Serial.print("NTP sync");
    struct tm t;
    uint8_t tries = 0;
    // getLocalTime с малым таймаутом на итерацию, чтобы не блокировать по 5 с.
    while (!getLocalTime(&t, 500) && tries < 20) {
        Serial.print("."); tries++;
    }
    // Ждём только ради первого кадра: не дождались — не беда, экран покажет
    // прочерки, а демон и ретрай в maintainNetwork() доведут дело до конца.
    Serial.println(tries < 20 ? " OK" : " TIMEOUT, retry in background");
}

// Радио в режиме выживания: выключено, но раз в POWER_NTP_WAKE_MS
// поднимается на POWER_NTP_WAKE_TIMEOUT_MS ради синхронизации часов.
// Без этого RTC уезжает: у внутреннего генератора точность порядка
// нескольких секунд в сутки.
static bool ntpWaking    = false;
static bool ntpRequested = false;
static uint32_t ntpWakeStart = 0;

static void radioOff(const char* why) {
    if (WiFi.getMode() == WIFI_OFF) return;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    localIP = "";
    Serial.printf("Power: WiFi off (%s)\n", why);
}

static void maintainSurvivalRadio(uint32_t now) {
    if (!ntpWaking) {
        radioOff("survival");
        if (now - lastNtpMs >= POWER_NTP_WAKE_MS) {
            Serial.println("Power: WiFi up for NTP");
            ntpWaking    = true;
            ntpRequested = false;
            ntpWakeStart = now;
            WiFi.mode(WIFI_STA);
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        return;
    }

    if (WiFi.status() == WL_CONNECTED && !ntpRequested) {
        startNTP();                       // демон сам доведёт запрос до конца
        ntpRequested = true;
    }
    if (now - ntpWakeStart >= POWER_NTP_WAKE_TIMEOUT_MS) {
        ntpWaking = false;
        // Отметку времени двигаем в любом случае: не достучались до сети —
        // следующая попытка через сутки, а не в каждой итерации цикла.
        lastNtpMs = now;
        radioOff("NTP done");
    }
}

// Поддержание сети: реконнект + периодический ре-синк NTP
static void maintainNetwork() {
    static uint32_t lastCheck = 0;
    uint32_t now = millis();

    if (!powerWifiWanted()) { maintainSurvivalRadio(now); return; }

    // Вернулись из выживания — радио надо поднять заново. Случай ntpWaking
    // отдельно: там радио уже в STA, и без этой ветки соединение осталось бы
    // недоделанным — без localIP и без mDNS, то есть дашборд не найти.
    if (WiFi.getMode() == WIFI_OFF || ntpWaking) {
        ntpWaking = false;
        connectWifi();
        return;
    }

    if (now - lastCheck >= 10000) {          // проверка связи раз в 10 с
        lastCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost -> reconnect");
            WiFi.reconnect();
        }
        String ip = WiFi.localIP().toString();
        if (ip != localIP) localIP = ip;
    }
    uint32_t ntpEvery = timeSynced ? NTP_RESYNC_MS : NTP_RETRY_MS;
    if (WiFi.status() == WL_CONNECTED && now - lastNtpMs >= ntpEvery) {
        Serial.println(timeSynced ? "NTP resync" : "NTP retry");
        startNTP();
    }
}

static bool updateTimeStrings() {
    static time_t lastEpoch = 0;
    static bool   rendered  = false;

    time_t now = time(nullptr);
    if (rendered && now == lastEpoch) return false;
    lastEpoch = now;
    rendered  = true;

    struct tm t;
    // timeSynced — живой признак «часы идут», а не «синк на старте прошёл»:
    // иначе после неудачного старта флаг оставался бы ложным даже с верным
    // временем, а после потери часов — истинным с прочерками на экране.
    // Раскладываем тот же now, что дал шаг «секунда сменилась», — иначе
    // кадр мог бы отрисовать соседнюю секунду.
    bool ok = localTimeNow(&t, now);
    if (ok != timeSynced) {
        timeSynced = ok;
        Serial.println(ok ? "Clock: time acquired" : "Clock: time LOST");
    }

    if (ok) {
        snprintf(timeBuf,     sizeof(timeBuf),
                 "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        snprintf(dateBuf,     sizeof(dateBuf),
                 "%02d %s %04d", t.tm_mday, MONTHS[t.tm_mon], t.tm_year + 1900);
        snprintf(dayShortBuf, sizeof(dayShortBuf), "%s", DAYS_SHORT[t.tm_wday]);
        snprintf(dayFullBuf,  sizeof(dayFullBuf),  "%s", DAYS_FULL[t.tm_wday]);
    } else {
        snprintf(timeBuf,     sizeof(timeBuf),     "--:--:--");
        snprintf(dateBuf,     sizeof(dateBuf),     "-- --- ----");
        snprintf(dayShortBuf, sizeof(dayShortBuf), "---");
        snprintf(dayFullBuf,  sizeof(dayFullBuf),  "---");
    }
    return true;
}

// ─── Метео: опрос датчика + индикация LED ────────────────
// Мигок LED без delay(): гасим на следующих итерациях loop().
static uint32_t ledBlinkStart = 0;
static bool     ledBlinking   = false;

static void ledBlink(uint8_t r, uint8_t g, uint8_t b) {
    ledColor(r, g, b);
    ledBlinkStart = millis();
    ledBlinking   = true;
}

static void updateWeather() {
    uint32_t now = millis();

    if (ledBlinking && now - ledBlinkStart >= 30) {
        ledColor(0, 0, 0);
        ledBlinking = false;
    }

    // Заряд забираем каждую итерацию: batteryLoop() всё равно считает его
    // непрерывно, и копия структуры бесплатна. Иначе показания зависели бы
    // от периода опроса датчика, а от него зависят пороги гашения экрана.
    battery = batteryRead();

    // Погоду спрашиваем редко: температура и давление за минуту никуда
    // не убегут, а каждый опрос I²C — это работа шины и ядра.
    if (now - lastSensorMs >= powerSensorIntervalMs()) {
        lastSensorMs = now;
        weather = sensorRead();
        if (!weather.valid) {
            ledColor(4, 0, 0);                   // красный — ошибка датчика
            ledBlinking = false;                 // горит ровно, не мигок
        } else if (!powerLedEnabled()) {
            ledColor(0, 0, 0);                   // в экономе индикация молчит
            ledBlinking = false;
        } else if (battery.valid && battery.low) {
            ledBlink(6, 3, 0);                   // жёлтый — АКБ разряжена
        } else {
            ledBlink(0, 3, 0);                   // зелёный — норма
        }
    }
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // Без этого write() в USB-CDC блокирует loop() на секунды,
    // когда порт открыт, но никто не вычитывает.
    Serial.setTxTimeoutMs(0);
#endif
    delay(1500);   // ждём поднятия USB CDC на хосте, иначе стартовый лог теряется

    Serial.println("\n=== ESP32-C6 Clock + Weather boot ===");
    ledColor(0, 0, 5);   // dim синий на старте

    // 80 МГц вместо 160: для часов + веб-сервера хватает с запасом,
    // а нагрев кристалла и потребление заметно ниже. 80 — минимум для WiFi.
    setCpuFrequencyMhz(80);
    Serial.printf("CPU @ %u MHz\n", (unsigned)getCpuFrequencyMhz());

    // Датчик BMP280 и батарея
    if (!sensorInit()) {
        for (int i = 0; i < 4; i++) { ledColor(20,0,0); delay(150); ledColor(0,0,0); delay(150); }
    }
    batteryInit();
    weather = sensorRead();
    battery = batteryRead();

    displayBegin();
    displaySplash("Connecting WiFi...");

    connectWifi();
    syncNTP();
    powerBegin();          // профиль применяем, когда экран и радио уже есть

    webApiBegin();

#if MQTT_ENABLED
    mqttInit();
#endif
}

void loop() {
    webApiLoop();
    batteryLoop();            // копит отсчёты АЦП по одному, без задержек
    powerLoop();              // режим энергосбережения по заряду
    maintainNetwork();
    updateWeather();          // BMP280 + батарея по таймеру + LED

    bool frameDue = updateTimeStrings() || forceRedraw;
    forceRedraw = false;

    if (stopwatch.running()) {
        static uint32_t lastSwDraw = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastSwDraw >= SW_DRAW_INTERVAL_MS) {
            lastSwDraw = nowMs;
            displayStopwatchFrame();
        }
        // Часы/аптайм в браузере: broadcast раз в секунду
        if (frameDue) {
            applyAutoBrightness();
            webApiBroadcast();
        }
    } else if (frameDue) {
        applyAutoBrightness();
        displayDraw();
        webApiBroadcast();
    }

#if MQTT_ENABLED
    mqttLoop(weather, battery);
#endif

    // Пульс в Serial раз в 5 с — монитор покажет жизнь, когда бы его ни открыли
    static uint32_t lastHb = 0;
    if (millis() - lastHb >= 5000) {
        lastHb = millis();
        Serial.printf("[hb] up=%lus wifi=%s ip=%s rssi=%d heap=%u clients=%u "
                      "chip=%.1fC bmp=%.1fC p=%.0fhPa bat=%d%% v=%.2f adc=%umV pm=%s\n",
                      (unsigned long)(millis() / 1000),
                      WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
                      localIP.length() ? localIP.c_str() : "-",
                      (int)WiFi.RSSI(),
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)webApiClientCount(),
                      dieTempC(),
                      weather.valid ? weather.temperature : 0.0f,
                      weather.valid ? weather.pressure : 0.0f,
                      battery.valid ? battery.percent : -1,
                      batteryRawVoltage(), (unsigned)batteryAdcMv(),
                      powerModeName());
    }

    // Короткий цикл на ходу: иначе команда стоит в очереди до конца паузы
    // и устройство стартует заметно позже браузера.
    delay(stopwatch.running() ? LOOP_STOPWATCH_MS : LOOP_IDLE_MS);
}
