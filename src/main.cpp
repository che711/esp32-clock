#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>
#include <time.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "config.h"
#include "clock_utils.h"
#include "sensor.h"
#include "battery.h"
#include "mqtt.h"
#include "web_ui.h"

// Совместимость имён: config.h ↔ прежний код часов
#define WIFI_PASS  WIFI_PASSWORD
#define HOSTNAME   DEVICE_HOSTNAME
#define PIN_CLK    OLED_CLK_PIN
#define PIN_DIN    OLED_DIN_PIN
#define PIN_CS     OLED_CS_PIN
#define PIN_DC     OLED_DC_PIN
#define PIN_RST    OLED_RST_PIN

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI
    u8g2(U8G2_R0, PIN_CS, PIN_DC, PIN_RST);

WebServer        server(80);
WebSocketsServer webSocket(81);

// ── Метео / батарея / LED ────────────────────────────────
static SensorData  weather{};
static BatteryData battery{};
static uint32_t    lastSensorMs = 0;

// WS2812 на GPIO8: pinMode НЕ вызывать — сломает RMT адресного LED
static void ledColor(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(LED_PIN, r, g, b);
}


const char* DAYS_SHORT[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
const char* DAYS_FULL[]  = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
const char* MONTHS[]     = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };

char timeBuf[9];
char prevTimeBuf[9] = "";
char dateBuf[20];
char dayShortBuf[5];
char dayFullBuf[12];
bool timeSynced   = false;
String localIP    = "";

static uint32_t requestCount     = 0;
static bool     displayOn        = true;
static uint8_t  currentContrast  = 255;
static bool     manualBrightness = false;
const char*     brightnessLabel  = "Day";

// ─── Секундомер ───────────────────────────────────────────
enum SwState { SW_IDLE, SW_RUNNING, SW_PAUSED };
static SwState   swState    = SW_IDLE;
static uint32_t  swStartMs  = 0;   // millis() момент старта текущего отрезка
static uint32_t  swAccumMs  = 0;   // накопленное время предыдущих отрезков, мс

uint32_t swElapsed() {
    if (swState == SW_RUNNING)
        return swAccumMs + (millis() - swStartMs);
    return swAccumMs;
}

// Температура кристалла: читаем не чаще раза в 10 с (temperatureRead
// на C3 может блокировать на десятки мс — незачем дёргать каждую секунду).
float getDieTemp() {
    static float    cached = 0.0f;
    static uint32_t last   = 0;
    uint32_t now = millis();
    if (last == 0 || now - last >= 10000) {
        last   = now ? now : 1;
        cached = temperatureRead();
    }
    return cached;
}

// ─── Дисплей вкл/выкл ────────────────────────────────────
void setDisplayPower(bool on) {
    displayOn = on;
#if HAS_DISPLAY
    if (on) {
        u8g2.setPowerSave(0);
        u8g2.setContrast(currentContrast);
    } else {
        u8g2.setPowerSave(1);
    }
#endif
    Serial.printf("Display -> %s\n", on ? "ON" : "OFF");
}

// ─── Яркость ─────────────────────────────────────────────
void applyContrast(uint8_t val, const char* label) {
    currentContrast = val;
    brightnessLabel = label;
#if HAS_DISPLAY
    if (displayOn) u8g2.setContrast(currentContrast);
#endif
}

void updateBrightness() {
    if (manualBrightness || !timeSynced) return;
    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    BrightnessLevel b = brightnessForHour(t.tm_hour);
    if (b.contrast != currentContrast) {
        applyContrast(b.contrast, b.label);
        Serial.printf("Auto brightness -> %s (%d)\n", brightnessLabel, currentContrast);
    }
}

// ─── JSON ─────────────────────────────────────────────────
void buildJson(char* buf, size_t sz) {
    char uptimeBuf[32];
    formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
    snprintf(buf, sz,
        "{"
        "\"time\":\"%s\","
        "\"date\":\"%s\","
        "\"day\":\"%s\","
        "\"uptime\":\"%s\","
        "\"ssid\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"temp\":\"%.1f\","
        "\"clients\":%d,"
        "\"ram_free\":%lu,"
        "\"ram_total\":%lu,"
        "\"brightness_pct\":%d,"
        "\"brightness_label\":\"%s\","
        "\"brightness_manual\":%s,"
        "\"display_on\":%s,"
        "\"sw_state\":%d,"
        "\"sw_ms\":%lu,"
        "\"bmp_valid\":%s,"
        "\"bmp_temp\":%.2f,"
        "\"pressure\":%.2f,"
        "\"pressure_mmhg\":%.1f,"
        "\"altitude\":%.1f,"
        "\"trend\":%.2f,"
        "\"forecast\":%d,"
        "\"bat_valid\":%s,"
        "\"bat_pct\":%d,"
        "\"bat_v\":%.2f,"
        "\"bat_low\":%s,"
        "\"requests\":%lu"
        "}",
        timeBuf, dateBuf, dayFullBuf,
        uptimeBuf,
        WIFI_SSID, localIP.c_str(),
        (int)WiFi.RSSI(),
        (float)getDieTemp(),
        (int)webSocket.connectedClients(),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)ESP.getHeapSize(),
        brightnessPct(currentContrast),
        brightnessLabel,
        manualBrightness ? "true" : "false",
        displayOn        ? "true" : "false",
        (int)swState,
        (unsigned long)swElapsed(),
        weather.valid ? "true" : "false",
        weather.temperature,
        weather.pressure,
        weather.pressureMmHg,
        weather.altitude,
        weather.pressureTrend,
        (int)weather.forecastIcon,
        battery.valid ? "true" : "false",
        (int)battery.percent,
        battery.voltage,
        battery.low ? "true" : "false",
        (unsigned long)requestCount
    );
}

void broadcastState() {
    if (webSocket.connectedClients() == 0) return;  // некому слать — не тратим CPU
    char json[1024];
    buildJson(json, sizeof(json));
    webSocket.broadcastTXT(json);
}

// ─── HTTP ─────────────────────────────────────────────────
void handleRoot() {
    requestCount++;
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiStats() {
    requestCount++;
    char json[1024];
    buildJson(json, sizeof(json));
    server.send(200, "application/json", json);
}

void handleApiTime() {
    requestCount++;
    char uptimeBuf[32];
    formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
    char json[256];
    snprintf(json, sizeof(json),
        "{\"time\":\"%s\",\"date\":\"%s\",\"day\":\"%s\","
        "\"uptime\":\"%s\",\"timestamp\":%lu}",
        timeBuf, dateBuf, dayFullBuf,
        uptimeBuf, (unsigned long)time(nullptr));
    server.send(200, "application/json", json);
}

void handleApiBrightness() {
    requestCount++;
    if (server.hasArg("auto")) {
        manualBrightness = false;
        server.send(200, "application/json", "{\"ok\":true,\"mode\":\"auto\"}");
        updateBrightness();          // сразу применяем авто-уровень
        broadcastState();
        return;
    }
    if (server.hasArg("value")) {
        int pct = constrain(server.arg("value").toInt(), 0, 100);
        manualBrightness = true;
        applyContrast(pctToContrast(pct), "Manual");
        char resp[64];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"mode\":\"manual\",\"pct\":%d}", pct);
        server.send(200, "application/json", resp);
        broadcastState();
        return;
    }
    server.send(400, "application/json", "{\"error\":\"missing value or auto\"}");
}

void handleApiPower() {
    requestCount++;
    if (server.hasArg("on")) {
        bool on = server.arg("on") != "0";
        setDisplayPower(on);
        char resp[48];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"display_on\":%s}", on ? "true" : "false");
        server.send(200, "application/json", resp);
        broadcastState();
        return;
    }
    server.send(400, "application/json", "{\"error\":\"missing on param\"}");
}

void handleReboot() {
    requestCount++;
    server.send(200, "text/plain", "Rebooting...");
    delay(300);
    ESP.restart();
}

void handleNotFound() {
    requestCount++;
    server.send(404, "text/plain", "Not found");
}

// ─── Forward declarations ─────────────────────────────────
void drawOLED();

// ─── Секундомер: приём команд ─────────────────────────────
void swStart() {
    if (swState != SW_RUNNING) {
        swStartMs = millis();
        swState   = SW_RUNNING;
        Serial.println("Stopwatch START");
    }
}
void swPause() {
    if (swState == SW_RUNNING) {
        swAccumMs += millis() - swStartMs;
        swState    = SW_PAUSED;
        Serial.println("Stopwatch PAUSE");
    }
}
void swReset() {
    swState        = SW_IDLE;
    swAccumMs      = 0;
    swStartMs      = 0;
    prevTimeBuf[0] = '\0';          // заставляем перерисовать часы
    Serial.println("Stopwatch RESET");
}

// ─── WebSocket ────────────────────────────────────────────
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        requestCount++;
        char json[1024];
        buildJson(json, sizeof(json));
        webSocket.sendTXT(num, json);
        Serial.printf("WS client #%d connected\n", num);
    } else if (type == WStype_TEXT) {
        Serial.printf("WS #%d TEXT: %.*s\n", num, (int)length, (char*)payload);
        // Команды секундомера: "sw:start" / "sw:pause" / "sw:reset"
        if      (length >= 8 && strncmp((char*)payload, "sw:start", 8) == 0) swStart();
        else if (length >= 8 && strncmp((char*)payload, "sw:pause", 8) == 0) swPause();
        else if (length >= 8 && strncmp((char*)payload, "sw:reset", 8) == 0) swReset();
        broadcastState();           // мгновенно рассылаем новое состояние
    }
}

// ─── WiFi + NTP ───────────────────────────────────────────
void connectWifi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(true);           // modem-sleep: радио спит между маячками — меньше нагрев
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        localIP = WiFi.localIP().toString();
        Serial.printf("\nIP: %s\n", localIP.c_str());
        // MAX modem-sleep: радио спит дольше (просыпается по DTIM точки доступа).
        // Заметно холоднее MIN; плата — отклик UI подрастает на десятки–сотни мс.
        // Если понадобится максимальная отзывчивость — верни WIFI_PS_MIN_MODEM.
        esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
        if (MDNS.begin(HOSTNAME)) {
            MDNS.addService("http", "tcp", 80);
            Serial.printf("mDNS: http://%s.local\n", HOSTNAME);
        }
    } else {
        Serial.println("\nWiFi FAILED");
    }
}

void syncNTP() {
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

// ─── Поддержание сети: реконнект + периодический ре-синк NTP ──
void maintainNetwork() {
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
void updateTimeStrings() {
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

// ─── Нижняя строка OLED: погода + батарея ────────────────
#if HAS_DISPLAY
// Маленькая иконка батареи: рамка + «носик» + заливка по проценту.
// Рисует левым краем в x, верх иконки yTop (высота 7). Возвращает полную ширину.
static int drawBattIcon(int x, int yTop, uint8_t pct) {
    const int w = 14, h = 7;
    u8g2.drawFrame(x, yTop, w, h);              // контур тела
    u8g2.drawBox(x + w, yTop + 2, 2, 3);        // носик
    int fillW = (int)pct * (w - 2) / 100;       // заливка пропорционально
    if (fillW > 0) u8g2.drawBox(x + 1, yTop + 1, fillW, h - 2);
    return w + 2;                               // тело + носик
}

// Нижняя строка (y=63): слева prefix+погода, справа иконка батареи + проценты.
static void drawBottomStatus(const char* prefix) {
    u8g2.setFont(u8g2_font_5x7_tr);

    char left[48];
    if (weather.valid)
        snprintf(left, sizeof(left), "%s%.1fC  %.0fhPa",
                 prefix, weather.temperature, weather.pressure);
    else
        snprintf(left, sizeof(left), "%sno sensor", prefix);
    u8g2.drawStr(2, 63, left);

    // Батарея — только если обнаружена (иначе питание от USB)
    if (battery.valid) {
        char pctStr[6];
        snprintf(pctStr, sizeof(pctStr), "%u%%", battery.percent);
        int pctW   = u8g2.getStrWidth(pctStr);
        int iconW  = 16;                         // 14 тело + 2 носик
        int totalW = iconW + 3 + pctW;
        int x      = 256 - totalW - 2;
        drawBattIcon(x, 56, battery.percent);    // верх 56 → низ 62, вровень с текстом
        u8g2.drawStr(x + iconW + 3, 63, pctStr);
    }
}
#endif

// ─── Дисплей ─────────────────────────────────────────────
void drawOLED() {
#if !HAS_DISPLAY
    return;
#else
    if (!displayOn) return;
    u8g2.clearBuffer();

    if (swState != SW_IDLE) {
        // ── Режим секундомера ──────────────────────────
        char full[16];
        formatStopwatch(swElapsed(), full, sizeof(full));  // "MM:SS.mmm"
        // делим на "MM:SS" (крупно) и ".mmm" (мельче)
        char mmss[6];  char msStr[5];
        memcpy(mmss, full, 5);   mmss[5] = '\0';
        snprintf(msStr, sizeof(msStr), "%s", full + 5);    // ".mmm" -> ровно 5 байт

        u8g2.setFont(u8g2_font_logisoso46_tr);
        int mainW = u8g2.getStrWidth(mmss);
        u8g2.setFont(u8g2_font_logisoso24_tr);
        int msW = u8g2.getStrWidth(msStr);

        int totalW  = mainW + msW + 4;
        int xMain   = (256 - totalW) / 2;
        int xMs     = xMain + mainW + 4;

        u8g2.setFont(u8g2_font_logisoso46_tr);
        u8g2.drawStr(xMain, 50, mmss);
        u8g2.setFont(u8g2_font_logisoso24_tr);
        u8g2.drawStr(xMs, 50, msStr);

        u8g2.drawHLine(0, 53, 256);

        // Низ: погода + батарея, с маркером состояния секундомера
        drawBottomStatus(swState == SW_RUNNING ? "" : "II ");

    } else {
        // ── Обычный режим часов ────────────────────────
        char hh[3] = { timeBuf[0], timeBuf[1], 0 };
        char mm[3] = { timeBuf[3], timeBuf[4], 0 };
        char ss[3] = { timeBuf[6], timeBuf[7], 0 };

        u8g2.setFont(u8g2_font_logisoso46_tr);
        int dw  = u8g2.getStrWidth("00");
        int cw  = u8g2.getStrWidth(":");
        const int gap = 5;
        int total  = dw * 3 + cw * 2 + gap * 4;
        int margin = (256 - total) / 2;

        int x = margin;
        u8g2.drawStr(x, 50, hh);  x += dw + gap;
        u8g2.drawStr(x, 50, ":"); x += cw + gap;
        u8g2.drawStr(x, 50, mm);  x += dw + gap;
        u8g2.drawStr(x, 50, ":"); x += cw + gap;
        u8g2.drawStr(x, 50, ss);

        u8g2.drawHLine(0, 53, 256);

        // Низ: [день недели] погода + батарея
        char pfx[8];
        if (timeSynced) snprintf(pfx, sizeof(pfx), "%s  ", dayShortBuf);
        else            snprintf(pfx, sizeof(pfx), "%s", "");
        drawBottomStatus(pfx);
    }

    u8g2.sendBuffer();
#endif
}

// ─── Метео: HTTP + опрос датчика ─────────────────────────
void handleApiWeather() {
    requestCount++;
    char json[320];
    snprintf(json, sizeof(json),
        "{\"valid\":%s,\"temperature\":%.2f,\"pressure\":%.2f,"
        "\"pressure_mmhg\":%.1f,\"altitude\":%.1f,\"qnh\":%.2f,"
        "\"air_density\":%.4f,\"trend\":%.2f,\"forecast\":%d,"
        "\"battery_valid\":%s,\"battery_pct\":%d,\"battery_v\":%.2f}",
        weather.valid ? "true" : "false",
        weather.temperature, weather.pressure, weather.pressureMmHg,
        weather.altitude, weather.pressureQnh, weather.airDensity,
        weather.pressureTrend, (int)weather.forecastIcon,
        battery.valid ? "true" : "false",
        (int)battery.percent, battery.voltage);
    server.send(200, "application/json", json);
}

// Опрос BMP280 + батареи по таймеру + индикация LED
void updateWeather() {
    uint32_t now = millis();
    if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
        lastSensorMs = now;
        weather = sensorRead();
        battery = batteryRead();
        if (!weather.valid) {
            ledColor(4, 0, 0);                         // красный — ошибка датчика
        } else if (battery.valid && battery.low) {
            ledColor(6, 3, 0); delay(30); ledColor(0, 0, 0);  // жёлтый мигок — АКБ разряжена
        } else {
            ledColor(0, 3, 0); delay(30); ledColor(0, 0, 0);  // зелёный мигок — норма
        }
    }
}

// ─── OTA по воздуху ──────────────────────────────────────
void otaInit() {
#if OTA_ENABLED
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]()  { ledColor(20, 10, 0); Serial.println("[OTA] start"); });
    ArduinoOTA.onEnd([]()    { ledColor(0, 20, 0);  Serial.println("[OTA] done");  });
    ArduinoOTA.onError([](ota_error_t e){ ledColor(20, 0, 0); Serial.printf("[OTA] err %u\n", e); });
    ArduinoOTA.begin();
    Serial.println("[OTA] ready");
#endif
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);   // ждём поднятия USB CDC на хосте, иначе стартовый лог теряется

    Serial.println("\n=== ESP32-C6 Clock + Weather boot ===");
    ledColor(0, 0, 5);   // dim синий на старте

    // 80 МГц вместо 160: для часов + веб-сервера хватает с запасом,
    // а нагрев кристалла и потребление заметно ниже. 80 — минимум для WiFi.
    setCpuFrequencyMhz(80);
    Serial.printf("CPU @ %u MHz\n", getCpuFrequencyMhz());

    // Датчик BMP280 и батарея
    if (!sensorInit()) {
        for (int i = 0; i < 4; i++) { ledColor(20,0,0); delay(150); ledColor(0,0,0); delay(150); }
    }
    batteryInit();
    weather = sensorRead();
    battery = batteryRead();

#if HAS_DISPLAY
    SPI.begin(PIN_CLK, -1, PIN_DIN, PIN_CS);
    u8g2.begin();
    u8g2.setContrast(currentContrast);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(50, 35, "Connecting WiFi...");
    u8g2.sendBuffer();
#endif

    connectWifi();
    syncNTP();
    otaInit();

    server.on("/",               HTTP_GET,  handleRoot);
    server.on("/api/stats",      HTTP_GET,  handleApiStats);
    server.on("/api/time",       HTTP_GET,  handleApiTime);
    server.on("/api/weather",    HTTP_GET,  handleApiWeather);
    server.on("/api/brightness", HTTP_POST, handleApiBrightness);
    server.on("/api/power",      HTTP_POST, handleApiPower);
    server.on("/api/reboot",     HTTP_POST, handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

#if MQTT_ENABLED
    mqttInit();
#endif

    Serial.println("HTTP :80  WS :81");
}

void loop() {
#if OTA_ENABLED
    ArduinoOTA.handle();
#endif
    server.handleClient();
    webSocket.loop();
    maintainNetwork();
    updateWeather();          // BMP280 + батарея по таймеру + LED
    updateTimeStrings();

    if (swState == SW_RUNNING) {
        static uint32_t lastSwDraw = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastSwDraw >= SW_DRAW_INTERVAL_MS) {
            lastSwDraw = nowMs;
            drawOLED();
        }
        // Часы/аптайм в браузере: broadcastState раз в секунду
        if (strcmp(timeBuf, prevTimeBuf) != 0) {
            strncpy(prevTimeBuf, timeBuf, sizeof(prevTimeBuf) - 1);
            prevTimeBuf[sizeof(prevTimeBuf) - 1] = '\0';
            updateBrightness();
            broadcastState();
        }
    } else {
        if (strcmp(timeBuf, prevTimeBuf) != 0) {
            updateBrightness();
            drawOLED();
            strncpy(prevTimeBuf, timeBuf, sizeof(prevTimeBuf) - 1);
            prevTimeBuf[sizeof(prevTimeBuf) - 1] = '\0';
            broadcastState();
        }
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
                      (unsigned)webSocket.connectedClients(),
                      getDieTemp(),
                      weather.valid ? weather.temperature : 0.0f,
                      weather.valid ? weather.pressure : 0.0f,
                      battery.valid ? battery.percent : -1);
    }

    delay(100);
}
