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
TrendHistory trendHistory;

char   timeBuf[9];
char   dateBuf[20];
char   dayShortBuf[5];
char   dayFullBuf[12];
bool   timeSynced = false;
String localIP    = "";

// Точка истории кладётся ровно там, где датчик прочитан, — и только там.
// Копить её из broadcast-кадров нельзя: те уходят раз в секунду и минуту
// подряд несут одно и то же число, из чего график получался ступенчатым.
static void historyPush(uint32_t nowMs) {
    trendHistory.push(nowMs, weather.valid, weather.temperature,
                      weather.pressure, weather.altitude, weather.pressureTrend);
}

static uint32_t lastSensorMs  = 0;
static int      lastSensorMin = -1;   // минута последнего замера, -1 — ещё не мерили

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

    // Две причины гасить экран, и подсветка вправе перебить только одну.
    bool battOk  = powerScreenBatteryOkNow();
    bool schedOk = !haveHour || powerScreenScheduleAllowsNow(hour);
    bool allowed = battOk && schedOk;

    // Пока идёт подсветка, расписание уступает — но только оно: рубеж по заряду
    // подсветка не перебивает. Полминуты OLED на исходе банки это не только
    // лишние миллиампер-часы, но и просадка напряжения, из-за которой чип уходит
    // в brownout, — часы не «покажут время напоследок», а выключатся совсем.
    // Истечёт окно — и ближайший же заход погасит панель обычным путём.
    if (!allowed && battOk && screenPeekActive(millis())) {
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
    if (!on) {
        displaySetPower(false);
        screenPeekUntil = 0;         // погасили сами — досматривать нечего
        return;
    }

    // Рубеж по заряду не обходится ничем, в том числе этой командой: иначе
    // подсветка сводила бы его на нет — зажгли на полминуты, и она же уронила
    // устройство в brownout. Часа для проверки не нужно, поэтому она работает
    // и без синхронизации времени. Отказ виден снаружи: display_on в ответе и
    // в снимке — фактическое состояние панели, а не то, что попросили.
    if (!powerScreenBatteryOkNow()) {
        screenPeekUntil = 0;
        Serial.println("Display: ON refused, battery critical");
        return;
    }

    displaySetPower(true);

    struct tm t;
    if (timeSynced && localTimeNow(&t) && !powerScreenScheduleAllowsNow(t.tm_hour)) {
        screenPeekUntil = millis() + POWER_SCREEN_PEEK_MS;
        if (screenPeekUntil == 0) screenPeekUntil = 1;   // 0 занят под «нет подсветки»
        Serial.printf("Display: night peek %lus\n",
                      (unsigned long)(POWER_SCREEN_PEEK_MS / 1000));
    } else {
        screenPeekUntil = 0;         // час рабочий — гасить по таймеру незачем
    }
}

// ─── Глубокий сон на пустой банке ─────────────────────────
//
// Ноль шкалы (3.65 В, см. battery_calc.h) — это команда «стоп». Раньше он
// означал только «индикатор упёрся»: часы продолжали работать с погашенным
// экраном примерно до 3.55 В, где чип уходит в brownout, — то есть поднятый
// ради ресурса банки ноль защищал её лишь наполовину. Теперь на нуле мы
// перестаём тянуть из элемента совсем: ~20 мкА во сне против ~25 мА.
//
// Будильник — таймер: физической кнопки нет, а подключение USB чип не
// перезагружает (питание идёт через тот же диод). Поэтому раз в
// POWER_SLEEP_CHECK_MS просыпаемся, читаем АЦП и решаем — вставать или спать
// дальше. Проверка стоит доли секунды: ни экран, ни радио на этом пути не
// поднимаются, полный setup() до неё не доходит.
static void deepSleepNow(const char* why) {
    Serial.printf("[Battery] %s -> deep sleep, проверка через %lu мин\n",
                  why, (unsigned long)(POWER_SLEEP_CHECK_MS / 60000UL));
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)POWER_SLEEP_CHECK_MS * 1000ULL);
    esp_deep_sleep_start();
}

// Полный путь: гасим то, что успели поднять, и засыпаем.
static void sleepUntilCharged() {
    screenSetPower(false);
    ledColor(0, 0, 0);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    deepSleepNow("банка пуста");
}

// Ноль должен продержаться POWER_SLEEP_CONFIRM_MS: одиночный выброс АЦП не
// должен стоить пяти минут темноты. Ноль на пустом месте маловероятен —
// медиана набора и сглаживание его давят, — но цена ошибки тут велика.
static void checkBatteryEmpty() {
    static uint32_t emptySince = 0;

    if (!powerShouldSleep(battery.percent, battery.valid, POWER_SLEEP_PCT)) {
        emptySince = 0;
        return;
    }
    uint32_t now = millis();
    if (emptySince == 0) {
        emptySince = now ? now : 1;
        Serial.println("[Battery] ноль шкалы, подтверждаем...");
        return;
    }
    if (now - emptySince >= POWER_SLEEP_CONFIRM_MS) sleepUntilCharged();
}

// ─── Секундомер: команды ─────────────────────────────────
// Экономия радио. MAX_MODEM холоднее всех, но задерживает входящий пакет до
// ~0.9 с: под секундомером это ощущается — кнопка в браузере откликается
// с заметным опозданием. Поэтому на время замера сон ужимаем до MIN_MODEM
// (пробуждение на каждый маячок, ~100 мс), а не выключаем.
//
// Раньше здесь стоял setSleep(false), то есть сон снимался совсем, и радио
// держало приёмник включённым постоянно — около 65 мА на ровном месте.
// Обосновано это было «точностью отсчёта», но отсчёт идёт по millis() и от
// сна радио не зависит вовсе: страдала только доставка команд, а её хватает
// и MIN_MODEM. Вместе с listen_interval = 1 из профиля normal задержка
// выходит около десятой секунды.
static void setRadioSaving(bool save) {
    WiFi.setSleep(true);            // сон включён всегда, вопрос только в глубине
    esp_wifi_set_ps(save ? WIFI_PS_MAX_MODEM : WIFI_PS_MIN_MODEM);
}

void swStart() {
    if (stopwatch.start(millis())) {
        displayInvalidateStopwatch();   // первый кадр после старта — полный
        setRadioSaving(false);

        // Замер обязан быть виден, из какого бы режима и часа его ни начали.
        // powerLoop() здесь, а не на следующем обороте цикла: он поднимает
        // уровень до обычного (в том числе поверх ручной фиксации), а вместе
        // с ним снимает ночное гашение экрана — иначе первые кадры уходили бы
        // в темноту. screenSetPower() добирает случай, когда панель погасили
        // руками кнопкой в дашборде.
        //
        // Порядок важен: после powerLoop() режим уже «обычный», расписание
        // экран не ограничивает, и screenSetPower() не заводит ночную
        // подсветку на POWER_SCREEN_PEEK_MS — иначе она погасила бы панель
        // через полминуты посреди отсчёта.
        powerLoop();
        screenSetPower(true);

        Serial.println("Stopwatch START");
    }
}

void swPause() {
    if (stopwatch.pause(millis())) {
        forceRedraw = true;             // маркер «II» — сразу, а не со сменой секунды
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
//
//  Подключение — автомат, а не цикл с delay() внутри. Ассоциация занимает
//  секунды, а из loop() она вызывается на возврате из выживания: пока
//  connectWifi() крутил свои 30 × delay(500), вставало всё — часы, секундомер,
//  веб и опрос датчика, до пятнадцати секунд разом.
//
//  Наружу торчат два вызова: wifiBeginConnect() начинает, wifiConnectStep()
//  двигает на один шаг и говорит, закончилось ли. Вся работа «после связи»
//  собрана в wifiOnConnected(), чтобы не разъезжалась между путями.
static bool     wifiConnecting   = false;
static uint32_t wifiConnectStart = 0;
static uint32_t wifiDotMs        = 0;   // ритм точек в логе, раз в 500 мс
// Отработала ли wifiOnConnected() для текущей ассоциации. Связь поднимается не
// только через автомат: setAutoReconnect(true) и WiFi.reconnect() возвращают её
// сами, мимо него. Без этого признака после такого возврата не было бы ни
// mDNS, ни настроек радио под профиль — то есть clock.local молчал бы до ребута.
static bool     wifiReady        = false;

static void wifiOnConnected() {
    wifiReady = true;
    localIP = WiFi.localIP().toString();
    Serial.printf("\nIP: %s\n", localIP.c_str());
    // Пока идёт замер секундомера, сон радио выключен ради точности отсчёта
    // (см. setRadioSaving) — реконнект посреди замера включать его обратно
    // не должен. Ровно та же оговорка, что и в powerApplyRadio().
    if (stopwatch.idle()) setRadioSaving(true);
    powerApplyRadio();          // мощность и listen_interval под режим
    // end() перед begin(): сюда заходят и повторно — после возврата из
    // выживания, где радио выключалось вместе с ответчиком mDNS.
    MDNS.end();
    if (MDNS.begin(DEVICE_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS: http://%s.local\n", DEVICE_HOSTNAME);
    }
}

static void wifiBeginConnect() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiConnecting   = true;
    wifiReady        = false;
    wifiConnectStart = millis();
    wifiDotMs        = wifiConnectStart;
}

// true — подключение больше не в процессе: либо связь есть, либо вышло время.
// Промах не страшен: обычная проверка связи раз в 10 с сделает WiFi.reconnect().
static bool wifiConnectStep(uint32_t now) {
    if (!wifiConnecting) return true;

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnecting = false;
        wifiOnConnected();
        return true;
    }
    if (now - wifiDotMs >= 500) { wifiDotMs = now; Serial.print("."); }
    if (now - wifiConnectStart >= WIFI_CONNECT_TIMEOUT_MS) {
        wifiConnecting = false;
        Serial.println("\nWiFi FAILED");
        return true;
    }
    return false;
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

// Поддержание сети: реконнект + периодический ре-синк NTP
static void maintainNetwork() {
    static uint32_t lastCheck = 0;
    uint32_t now = millis();

    // Пока идёт ассоциация — только двигаем автомат. Проверять связь и
    // ре-синкать NTP посреди подключения нечего.
    if (wifiConnecting) { wifiConnectStep(now); return; }

    if (now - lastCheck >= 10000) {          // проверка связи раз в 10 с
        lastCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            wifiReady = false;
            Serial.println("WiFi lost -> reconnect");
            WiFi.reconnect();
        } else if (!wifiReady) {
            // Связь вернулась мимо автомата — авто-реконнектом стека или
            // предыдущим WiFi.reconnect(). Доводим её до конца тем же путём.
            wifiOnConnected();
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

// Пора ли опрашивать датчик. Замер привязан к началу минуты по часам, а не
// к моменту включения: раньше интервал отсчитывался от millis() загрузки, и
// показания обновлялись в произвольную секунду (12:34:17, 12:35:17 …).
// Сравнить их с чем-то по времени было нельзя, а точки графика ложились
// вразнобой относительно минут.
static bool sensorDue(uint32_t nowMs) {
    struct tm t;
    if (!localTimeNow(&t)) {
        // NTP ещё не ответил — минут у нас нет, работает прежний отсчёт
        // от millis(). Иначе датчик молчал бы до синхронизации.
        return nowMs - lastSensorMs >= powerSensorIntervalMs();
    }
    if (t.tm_min == lastSensorMin) return false;   // в эту минуту уже мерили

    // В экономе опрос раз в две минуты — берём чётные, чтобы момент замера
    // не зависел от того, когда устройство включили. Ноль в делителе тут
    // означал бы деление на ноль, поэтому шаг не опускается ниже минуты.
    uint32_t everyMin = powerSensorIntervalMs() / 60000UL;
    if (everyMin < 1) everyMin = 1;
    if ((uint32_t)t.tm_min % everyMin != 0) return false;

    lastSensorMin = t.tm_min;
    return true;
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
    if (sensorDue(now)) {
        lastSensorMs = now;
        weather = sensorRead();
        historyPush(now);
        // Профиль спрашиваем первым: в экономе индикация молчит вся, включая
        // аварийную. Раньше «датчик умер» стояло выше — и красный горел РОВНО,
        // то есть непрерывно, именно в режимах, заведённых ради экономии.
        if (!powerLedEnabled()) {
            ledColor(0, 0, 0);                   // в экономе индикация молчит
            ledBlinking = false;
        } else if (!weather.valid) {
            ledColor(4, 0, 0);                   // красный — ошибка датчика
            ledBlinking = false;                 // горит ровно, не мигок
        } else if (battery.valid && battery.low) {
            ledBlink(6, 3, 0);                   // жёлтый — АКБ разряжена
        } else {
            ledBlink(0, 3, 0);                   // зелёный — норма
        }
    }
}

// ── Причина последнего сброса ─────────────────────────────
//  Перезагрузившиеся ночью часы выглядят в дашборде ровно как часы, которые
//  не перезагружались: аптайм обнулился, и всё. А различать тут есть что —
//  просадка питания на исходе банки, паника прошивки и сторожевой таймер
//  требуют разных действий, и узнавать о них, подключившись к USB задним
//  числом, поздно. Причина сброса переживает и brownout (в отличие от core
//  dump: писать во флеш на падающем питании уже нечем), поэтому она едет
//  в снимок и оседает в журнале дашборда.
static esp_reset_reason_t bootReason = ESP_RST_UNKNOWN;

const char* resetReasonName() {
    switch (bootReason) {
        case ESP_RST_POWERON:    return "power-on";
        case ESP_RST_EXT:        return "external pin";
        case ESP_RST_SW:         return "software";      // ESP.restart(), в т.ч. /api/reboot
        case ESP_RST_PANIC:      return "panic";
        case ESP_RST_INT_WDT:    return "interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "task watchdog";
        case ESP_RST_WDT:        return "watchdog";
        case ESP_RST_DEEPSLEEP:  return "deep sleep";
        case ESP_RST_BROWNOUT:   return "brownout";
        case ESP_RST_SDIO:       return "SDIO";
        case ESP_RST_USB:        return "USB";
        case ESP_RST_JTAG:       return "JTAG";
        case ESP_RST_EFUSE:      return "efuse error";
        case ESP_RST_PWR_GLITCH: return "power glitch";
        case ESP_RST_CPU_LOCKUP: return "CPU lockup";
        default:                 return "unknown";
    }
}

// Штатное — это включили питание, нажали reset, перезагрузились по своей же
// команде, проснулись из сна. Всё прочее авария, и дашборд поднимет её в
// журнале до warn, вместо того чтобы утопить в потоке info.
bool resetWasAbnormal() {
    switch (bootReason) {
        case ESP_RST_POWERON:
        case ESP_RST_EXT:
        case ESP_RST_SW:
        case ESP_RST_DEEPSLEEP: return false;
        default:                return true;
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
    bootReason = esp_reset_reason();
    Serial.printf("Reset reason: %s%s\n", resetReasonName(),
                  resetWasAbnormal() ? "  <-- аварийный" : "");
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
    battery = batteryRead();

    // Проснулись по таймеру — значит уснули на пустой банке. Решаем прямо
    // здесь, пока не подняты ни экран, ни радио: если заряд не подрос, полный
    // старт был бы дороже всего, что мы за него получим.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER &&
        !powerShouldWake(battery.percent, battery.valid, POWER_WAKE_PCT)) {
        deepSleepNow("заряд всё ещё на нуле");
    }

    // Время сон переживает: RTC идёт и в нём. Если часы показывают
    // правдоподобную дату, синхронизация уже была — иначе экран рисовал бы
    // прочерки при исправных часах, пока не дойдёт очередь до NTP.
    struct tm t;
    if (localTimeNow(&t)) {
        timeSynced = true;
        Serial.println("RTC пережил перезагрузку, время на месте");
    }

    weather = sensorRead();
    historyPush(millis());

    displayBegin();
    displaySplash("Connecting WiFi...");

    // В setup() ждём связь на месте: показывать всё равно нечего — на экране
    // заставка, — а syncNTP() ниже без сети бессмысленна. Автомат тот же, что
    // крутится в loop(), просто здесь его прокачиваем сами.
    wifiBeginConnect();
    while (!wifiConnectStep(millis())) delay(50);
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
    checkBatteryEmpty();      // ноль шкалы -> deep sleep, дальше не возвращаемся
    powerLoop();              // уровень энергосбережения
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
