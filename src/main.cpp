#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <time.h>
#include <U8g2lib.h>
#include <SPI.h>
#include "web_ui.h"

// ─── WiFi ────────────────────────────────────────────────
const char* WIFI_SSID = "SkyNet";
const char* WIFI_PASS = "password";

// ─── NTP ─────────────────────────────────────────────────
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 3600;
const int   DST_OFFSET = 3600;

// ─── Пины ────────────────────────────────────────────────
#define PIN_CLK  6
#define PIN_DIN  7
#define PIN_CS   10
#define PIN_DC   1
#define PIN_RST  3

// ─── Дисплей ─────────────────────────────────────────────
U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI
    u8g2(U8G2_R0, PIN_CLK, PIN_DIN, PIN_CS, PIN_DC, PIN_RST);

WebServer        server(80);
WebSocketsServer webSocket(81);

// ─── Строки ──────────────────────────────────────────────
const char* DAYS_SHORT[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
const char* DAYS_FULL[]  = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
const char* MONTHS[]     = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };

char timeBuf[9];
char prevTimeBuf[9] = "";
char dateBuf[20];
char dayShortBuf[5];
char dayFullBuf[12];
bool timeSynced = false;
String localIP  = "";

// ─── CPU ─────────────────────────────────────────────────
static float cpuLoadPct = 0.0f;
uint8_t getCpuLoad() { return (uint8_t)constrain(cpuLoadPct, 0, 99); }

// ─── Автояркость ─────────────────────────────────────────
uint8_t     currentContrast = 200;
const char* brightnessLabel = "Day";

void updateBrightness() {
    if (!timeSynced) return;
    struct tm t;
    if (!getLocalTime(&t)) return;
    int h = t.tm_hour;

    uint8_t    newC;
    const char* newL;
    if      (h >= 22 || h < 6) { newC = 15;  newL = "Night";   }
    else if (h < 8)             { newC = 80;  newL = "Morning"; }
    else if (h < 20)            { newC = 200; newL = "Day";     }
    else                        { newC = 120; newL = "Evening"; }

    if (newC != currentContrast) {
        currentContrast = newC;
        brightnessLabel = newL;
        u8g2.setContrast(currentContrast);
        Serial.printf("Brightness → %s (%d)\n", brightnessLabel, currentContrast);
    }
}

uint8_t getBrightnessPct() {
    return (uint8_t)(currentContrast * 100 / 200);
}

// ─── Uptime ───────────────────────────────────────────────
String getUptime() {
    uint32_t s = millis() / 1000;
    uint32_t d = s / 86400; s %= 86400;
    uint32_t h = s / 3600;  s %= 3600;
    uint32_t m = s / 60;    s %= 60;
    char buf[32];
    if (d > 0) snprintf(buf, sizeof(buf), "%dd %02dh %02dm %02ds", d, h, m, s);
    else        snprintf(buf, sizeof(buf), "%02dh %02dm %02ds", h, m, s);
    return String(buf);
}

// ─── JSON builder (HTTP + WebSocket) ─────────────────────
void buildJson(char* buf, size_t sz) {
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
        "\"brightness_label\":\"%s\""
        "}",
        timeBuf, dateBuf, dayFullBuf,
        getUptime().c_str(),
        WIFI_SSID, localIP.c_str(),
        (int)WiFi.RSSI(),
        (float)temperatureRead(),
        getCpuLoad(),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)ESP.getHeapSize(),
        getBrightnessPct(),
        brightnessLabel
    );
}

// ─── HTTP ─────────────────────────────────────────────────
void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleApiStats() {
    char json[640];
    buildJson(json, sizeof(json));
    server.send(200, "application/json", json);
}

void handleApiTime() {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"time\":\"%s\",\"date\":\"%s\",\"day\":\"%s\","
        "\"uptime\":\"%s\",\"timestamp\":%lu}",
        timeBuf, dateBuf, dayFullBuf,
        getUptime().c_str(), (unsigned long)time(nullptr));
    server.send(200, "application/json", json);
}

void handleReboot() {
    server.send(200, "text/plain", "Rebooting...");
    delay(300);
    ESP.restart();
}

void handleNotFound() { server.send(404, "text/plain", "Not found"); }

// ─── WebSocket ────────────────────────────────────────────
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        // Сразу отправляем свежие данные новому клиенту
        char json[640];
        buildJson(json, sizeof(json));
        webSocket.sendTXT(num, json);
        Serial.printf("WS client #%d connected\n", num);
    }
}

// ─── WiFi + NTP ───────────────────────────────────────────
void connectWifi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500); Serial.print("."); tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        localIP = WiFi.localIP().toString();
        Serial.printf("\nIP: %s\n", localIP.c_str());
    } else {
        Serial.println("\nWiFi FAILED");
    }
}

void syncNTP() {
    if (WiFi.status() != WL_CONNECTED) return;
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    Serial.print("NTP sync");
    struct tm t;
    uint8_t tries = 0;
    while (!getLocalTime(&t) && tries < 20) {
        delay(500); Serial.print("."); tries++;
    }
    timeSynced = (tries < 20);
    Serial.println(timeSynced ? " OK" : " TIMEOUT");
}

// ─── Буферы времени ───────────────────────────────────────
void updateTimeStrings() {
    struct tm t;
    if (getLocalTime(&t)) {
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
    u8g2.clearBuffer();

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

    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    u8g2.begin();
    u8g2.setContrast(200);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(50, 35, "Connecting WiFi...");
    u8g2.sendBuffer();

    connectWifi();
    syncNTP();

    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/api/stats",  HTTP_GET,  handleApiStats);
    server.on("/api/time",   HTTP_GET,  handleApiTime);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    Serial.printf("HTTP on :80, WebSocket on :81\n");
}

void loop() {
    uint32_t t0 = micros();

    server.handleClient();
    webSocket.loop();
    updateTimeStrings();

    if (strcmp(timeBuf, prevTimeBuf) != 0) {
        updateBrightness();
        drawOLED();
        strncpy(prevTimeBuf, timeBuf, sizeof(prevTimeBuf));

        // Push всем WebSocket клиентам раз в секунду
        char json[640];
        buildJson(json, sizeof(json));
        webSocket.broadcastTXT(json);
    }

    uint32_t workUs = micros() - t0;
    float sample = (float)workUs / 1000.0f;
    cpuLoadPct = cpuLoadPct * 0.88f + (sample / 100.0f * 100.0f) * 0.12f;

    delay(100);
}