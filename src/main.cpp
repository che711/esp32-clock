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

static char     prevTimeBuf[9] = "";
static uint32_t lastSensorMs   = 0;

static const char* DAYS_SHORT[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
static const char* DAYS_FULL[]  = { "Sunday","Monday","Tuesday","Wednesday",
                                    "Thursday","Friday","Saturday" };
static const char* MONTHS[]     = { "JAN","FEB","MAR","APR","MAY","JUN",
                                    "JUL","AUG","SEP","OCT","NOV","DEC" };

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

// ─── Яркость ─────────────────────────────────────────────
void applyAutoBrightness() {
    if (displayIsManual() || !timeSynced) return;
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    displayAutoForHour(t.tm_hour);
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
    prevTimeBuf[0] = '\0';              // заставляем перерисовать часы
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
        if (MDNS.begin(DEVICE_HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS: http://%s.local\n", DEVICE_HOSTNAME);
        }
    } else {
        Serial.println("\nWiFi FAILED");
    }
}

static void syncNTP() {
    if (WiFi.status() != WL_CONNECTED) return;
    // configTzTime сразу задаёт TZ-правило (DST считается автоматически).
    configTzTime(TZ_INFO, NTP_SERVER);
    Serial.print("NTP sync");
    struct tm t;
    uint8_t tries = 0;
    // getLocalTime с малым таймаутом на итерацию, чтобы не блокировать по 5 с.
    while (!getLocalTime(&t, 500) && tries < 20) {
        Serial.print("."); tries++;
    }
    timeSynced = (tries < 20);
    Serial.println(timeSynced ? " OK" : " TIMEOUT");
}

// Поддержание сети: реконнект + периодический ре-синк NTP
static void maintainNetwork() {
    static uint32_t lastCheck = 0;
    static uint32_t lastNtp   = 0;
    uint32_t now = millis();

    if (now - lastCheck >= 10000) {          // проверка связи раз в 10 с
        lastCheck = now;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi lost -> reconnect");
            WiFi.reconnect();
        } else {
            String ip = WiFi.localIP().toString();
            if (ip != localIP) localIP = ip;  // IP мог смениться при реконнекте
        }
    }
    if (timeSynced && now - lastNtp >= 6UL * 3600UL * 1000UL) {  // ре-синк раз в 6 ч
        lastNtp = now;
        configTzTime(TZ_INFO, NTP_SERVER);
        Serial.println("NTP resync");
    }
}

// ─── Буферы времени ───────────────────────────────────────
static void updateTimeStrings() {
    struct tm t;
    if (getLocalTime(&t, 0)) {
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

    if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
        lastSensorMs = now;
        weather = sensorRead();
        battery = batteryRead();
        if (!weather.valid) {
            ledColor(4, 0, 0);                   // красный — ошибка датчика
            ledBlinking = false;                 // горит ровно, не мигок
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

    webApiBegin();

#if MQTT_ENABLED
    mqttInit();
#endif
}

void loop() {
    webApiLoop();
    maintainNetwork();
    updateWeather();          // BMP280 + батарея по таймеру + LED
    updateTimeStrings();

    bool secondChanged = strcmp(timeBuf, prevTimeBuf) != 0;
    if (secondChanged) {
        strncpy(prevTimeBuf, timeBuf, sizeof(prevTimeBuf) - 1);
        prevTimeBuf[sizeof(prevTimeBuf) - 1] = '\0';
    }

    if (stopwatch.running()) {
        static uint32_t lastSwDraw = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastSwDraw >= SW_DRAW_INTERVAL_MS) {
            lastSwDraw = nowMs;
            displayStopwatchFrame();
        }
        // Часы/аптайм в браузере: broadcast раз в секунду
        if (secondChanged) {
            applyAutoBrightness();
            webApiBroadcast();
        }
    } else if (secondChanged) {
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
                      "chip=%.1fC bmp=%.1fC p=%.0fhPa bat=%d%%\n",
                      (unsigned long)(millis() / 1000),
                      WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
                      localIP.length() ? localIP.c_str() : "-",
                      (int)WiFi.RSSI(),
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)webApiClientCount(),
                      dieTempC(),
                      weather.valid ? weather.temperature : 0.0f,
                      weather.valid ? weather.pressure : 0.0f,
                      battery.valid ? battery.percent : -1);
    }

    // Короткий цикл только на ходу: иначе команда стоит в очереди до 100 мс
    // и устройство стартует заметно позже браузера.
    delay(stopwatch.running() ? 5 : 100);
}
