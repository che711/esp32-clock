#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "esp_freertos_hooks.h"
#include "web_ui.h"
#include "clock_utils.h"

const char* WIFI_SSID = "SkyNet";
const char* WIFI_PASS = "password";

const char* HOSTNAME   = "clock";           // → http://clock.local
const char* NTP_SERVER = "pool.ntp.org";

// Часовой пояс задаётся POSIX-строкой TZ, а НЕ фиксированным сдвигом.
// Так переход на летнее/зимнее время происходит автоматически.
// Ниже — Варшава/Центральная Европа. Примеры для других зон:
//   Лондон:   "GMT0BST,M3.5.0/1,M10.5.0"
//   Киев:     "EET-2EEST,M3.5.0/3,M10.5.0/4"
//   UTC:      "UTC0"
const char* TZ_INFO = "CET-1CEST,M3.5.0,M10.5.0/3";

#define PIN_CLK  6
#define PIN_DIN  7
#define PIN_CS   10
#define PIN_DC   1
#define PIN_RST  3

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI
    u8g2(U8G2_R0, PIN_CLK, PIN_DIN, PIN_CS, PIN_DC, PIN_RST);

WebServer        server(80);
WebSocketsServer webSocket(81);

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

// ─── Усреднённая загрузка CPU (idle-hook + EMA) ───────────
// idle-hook тикает в простое; за окно 1 с считаем мгновенную загрузку
// относительно калибровочного максимума и сглаживаем EMA (~7 c),
// чтобы убрать всплески от фоновых задач WiFi/TCP.
static volatile uint32_t idleCount = 0;
static uint32_t idleMax    = 1;
static float    loadAvg    = 0.0f;
static bool idleHook() { idleCount++; return true; }

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

uint8_t getCpuLoad() { return (uint8_t)(loadAvg + 0.5f); }

// Обновление загрузки раз в секунду + EMA-сглаживание.
void updateCpuLoad() {
    static uint32_t lastWin = 0;
    uint32_t now = millis();
    if (now - lastWin < 1000) return;
    lastWin = now;
    uint32_t c = idleCount;
    idleCount = 0;
    if (c > idleMax) idleMax = c;                 // калибровка «100% простоя» (потолок — не переоценить)
    if (idleMax < 1000) return;                   // idle-hook ещё не считает — ждём калибровки
    uint32_t used = (uint32_t)((uint64_t)c * 100 / idleMax);
    float inst = (used > 100) ? 0.0f : (float)(100 - used);
    loadAvg = loadAvg * 0.85f + inst * 0.15f;     // EMA, τ≈7 c
}

// ─── Дисплей вкл/выкл ────────────────────────────────────
void setDisplayPower(bool on) {
    displayOn = on;
    if (on) {
        u8g2.setPowerSave(0);
        u8g2.setContrast(currentContrast);
    } else {
        u8g2.setPowerSave(1);
    }
    Serial.printf("Display -> %s\n", on ? "ON" : "OFF");
}

// ─── Яркость ─────────────────────────────────────────────
void applyContrast(uint8_t val, const char* label) {
    currentContrast = val;
    brightnessLabel = label;
    if (displayOn) u8g2.setContrast(currentContrast);
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

// ─── Uptime ───────────────────────────────────────────────
String getUptime() {
    char buf[32];
    formatUptime(millis() / 1000, buf, sizeof(buf));
    return String(buf);
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
        "\"cpu\":%d,"
        "\"ram_free\":%lu,"
        "\"ram_total\":%lu,"
        "\"brightness_pct\":%d,"
        "\"brightness_label\":\"%s\","
        "\"brightness_manual\":%s,"
        "\"display_on\":%s,"
        "\"sw_state\":%d,"
        "\"sw_ms\":%lu,"
        "\"requests\":%lu"
        "}",
        timeBuf, dateBuf, dayFullBuf,
        uptimeBuf,
        WIFI_SSID, localIP.c_str(),
        (int)WiFi.RSSI(),
        (float)getDieTemp(),
        getCpuLoad(),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)ESP.getHeapSize(),
        brightnessPct(currentContrast),
        brightnessLabel,
        manualBrightness ? "true" : "false",
        displayOn        ? "true" : "false",
        (int)swState,
        (unsigned long)swElapsed(),
        (unsigned long)requestCount
    );
}

void broadcastState() {
    if (webSocket.connectedClients() == 0) return;  // некому слать — не тратим CPU
    char json[768];
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
    char json[768];
    buildJson(json, sizeof(json));
    server.send(200, "application/json", json);
}

void handleApiTime() {
    requestCount++;
    char json[256];
    snprintf(json, sizeof(json),
        "{\"time\":\"%s\",\"date\":\"%s\",\"day\":\"%s\","
        "\"uptime\":\"%s\",\"timestamp\":%lu}",
        timeBuf, dateBuf, dayFullBuf,
        getUptime().c_str(), (unsigned long)time(nullptr));
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
        char json[768];
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

// ─── Дисплей ─────────────────────────────────────────────
void drawOLED() {
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

        // Статус внизу
        u8g2.setFont(u8g2_font_5x7_tr);
        const char* statusStr = (swState == SW_RUNNING) ? "STOPWATCH  RUNNING" : "STOPWATCH  PAUSED";
        int statusW = u8g2.getStrWidth(statusStr);
        u8g2.drawStr((256 - statusW) / 2, 63, statusStr);

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

        u8g2.setFont(u8g2_font_5x7_tr);
        if (timeSynced) {
            char ssidShort[10];
            strncpy(ssidShort, WIFI_SSID, 8);
            ssidShort[8] = 0;
            char bottom[56];
            if (localIP.length())
                snprintf(bottom, sizeof(bottom), "%s %s | %s %s",
                         dayShortBuf, dateBuf, ssidShort, localIP.c_str());
            else
                snprintf(bottom, sizeof(bottom), "%s %s", dayShortBuf, dateBuf);
            u8g2.drawStr(2, 63, bottom);
        } else {
            u8g2.drawStr(2, 63, "NO NTP");
        }
    }

    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1500);   // ждём поднятия USB CDC на хосте, иначе стартовый лог теряется

    Serial.println("\n=== ESP32-C3 Clock boot ===");

    // 80 МГц вместо 160: для часов + веб-сервера хватает с запасом,
    // а нагрев кристалла и потребление заметно ниже. 80 — минимум для WiFi.
    setCpuFrequencyMhz(80);
    Serial.printf("CPU @ %u MHz\n", getCpuFrequencyMhz());

    esp_register_freertos_idle_hook(idleHook);   // для усреднённой загрузки CPU

    u8g2.begin();
    u8g2.setContrast(currentContrast);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(50, 35, "Connecting WiFi...");
    u8g2.sendBuffer();

    connectWifi();
    syncNTP();

    server.on("/",               HTTP_GET,  handleRoot);
    server.on("/api/stats",      HTTP_GET,  handleApiStats);
    server.on("/api/time",       HTTP_GET,  handleApiTime);
    server.on("/api/brightness", HTTP_POST, handleApiBrightness);
    server.on("/api/power",      HTTP_POST, handleApiPower);
    server.on("/api/reboot",     HTTP_POST, handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    Serial.println("HTTP :80  WS :81");
}

void loop() {
    server.handleClient();
    webSocket.loop();
    maintainNetwork();
    updateTimeStrings();

    if (swState == SW_RUNNING) {
        // Секундомер активен — перерисовываем дисплей каждые ~100 мс
        drawOLED();
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

    // Пульс в Serial раз в 5 с — монитор покажет жизнь, когда бы его ни открыли
    static uint32_t lastHb = 0;
    if (millis() - lastHb >= 5000) {
        lastHb = millis();
        Serial.printf("[hb] up=%lus wifi=%s ip=%s rssi=%d heap=%u load=%u%%\n",
                      (unsigned long)(millis() / 1000),
                      WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
                      localIP.length() ? localIP.c_str() : "-",
                      (int)WiFi.RSSI(),
                      (unsigned)esp_get_free_heap_size(),
                      getCpuLoad());
    }

    updateCpuLoad();
    delay(100);
}
