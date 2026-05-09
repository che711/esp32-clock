#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h>
#include <U8g2lib.h>
#include <SPI.h>

// ─── WiFi ────────────────────────────────────────────────
const char* WIFI_SSID = "SkyNet";
const char* WIFI_PASS = "password";

// ─── NTP ─────────────────────────────────────────────────
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 3600;   // UTC+1 (CET, Польша зимой)
const int   DST_OFFSET = 3600;   // +1 час летом (CEST)

// ─── Пины (программный SPI) ──────────────────────────────
#define PIN_CLK  6
#define PIN_DIN  7
#define PIN_CS   10
#define PIN_DC   1
#define PIN_RST  3

// ─── Дисплей ─────────────────────────────────────────────
U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI
    u8g2(U8G2_R0, PIN_CLK, PIN_DIN, PIN_CS, PIN_DC, PIN_RST);

WebServer server(80);

// ─── Строки ──────────────────────────────────────────────
const char* DAYS[] = {
    "SUN","MON","TUE","WED","THU","FRI","SAT"
};
const char* MONTHS[] = {
    "JAN","FEB","MAR","APR","MAY","JUN",
    "JUL","AUG","SEP","OCT","NOV","DEC"
};

char timeBuf[9];    // "HH:MM:SS"
char dateBuf[20];   // "09 MAY 2026"
char dayBuf[5];     // "FRI"
bool timeSynced = false;
String localIP = "";

// ─── HTML страница ────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Clock</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    background: #0a0a0a;
    color: #fff;
    font-family: 'Courier New', monospace;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    min-height: 100vh;
  }
  .card {
    background: #111;
    border: 1px solid #222;
    border-radius: 16px;
    padding: 40px 60px;
    text-align: center;
    box-shadow: 0 0 60px rgba(255,255,255,0.04);
  }
  #time {
    font-size: clamp(60px, 15vw, 120px);
    font-weight: 700;
    letter-spacing: 6px;
    color: #fff;
    line-height: 1;
    text-shadow: 0 0 40px rgba(255,255,255,0.15);
  }
  .divider {
    border: none;
    border-top: 1px solid #333;
    margin: 20px 0;
  }
  #date {
    font-size: clamp(18px, 4vw, 28px);
    color: #888;
    letter-spacing: 4px;
    text-transform: uppercase;
  }
  #day {
    font-size: clamp(13px, 2.5vw, 18px);
    color: #555;
    letter-spacing: 6px;
    margin-top: 8px;
    text-transform: uppercase;
  }
  .status {
    margin-top: 30px;
    font-size: 11px;
    color: #333;
    letter-spacing: 2px;
  }
  .dot {
    display: inline-block;
    width: 6px; height: 6px;
    border-radius: 50%;
    background: #2a2;
    margin-right: 6px;
    vertical-align: middle;
    animation: pulse 2s infinite;
  }
  @keyframes pulse {
    0%,100% { opacity: 1; }
    50%      { opacity: 0.3; }
  }
</style>
</head>
<body>
<div class="card">
  <div id="time">--:--:--</div>
  <hr class="divider">
  <div id="date">-- --- ----</div>
  <div id="day">---</div>
</div>
<div class="status"><span class="dot"></span>ESP32 SUPER MINI · NTP SYNC</div>

<script>
  async function tick() {
    try {
      const r = await fetch('/api/time');
      const d = await r.json();
      document.getElementById('time').textContent = d.time;
      document.getElementById('date').textContent = d.date;
      document.getElementById('day').textContent  = d.day;
    } catch(e) {}
  }
  tick();
  setInterval(tick, 1000);
</script>
</body>
</html>
)rawliteral";

// ─── HTTP обработчики ─────────────────────────────────────
void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiTime() {
    char json[128];
    snprintf(json, sizeof(json),
             "{\"time\":\"%s\",\"date\":\"%s\",\"day\":\"%s\"}",
             timeBuf, dateBuf, dayBuf);
    server.send(200, "application/json", json);
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found");
}

// ─── WiFi + NTP ───────────────────────────────────────────
void connectWifi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 30) {
        delay(500);
        Serial.print(".");
        tries++;
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
        delay(500);
        Serial.print(".");
        tries++;
    }
    timeSynced = (tries < 20);
    Serial.println(timeSynced ? " OK" : " TIMEOUT");
}

// ─── Обновление буферов времени ──────────────────────────
void updateTimeStrings() {
    struct tm t;
    if (getLocalTime(&t)) {
        snprintf(timeBuf, sizeof(timeBuf),
                 "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        snprintf(dateBuf, sizeof(dateBuf),
                 "%02d %s %04d", t.tm_mday, MONTHS[t.tm_mon], t.tm_year + 1900);
        snprintf(dayBuf, sizeof(dayBuf),
                 "%s", DAYS[t.tm_wday]);
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--:--");
        snprintf(dateBuf, sizeof(dateBuf), "-- --- ----");
        snprintf(dayBuf,  sizeof(dayBuf),  "---");
    }
}

// ─── Отрисовка дисплея ───────────────────────────────────
void drawOLED() {
    u8g2.clearBuffer();

    // Время — максимально крупно
    u8g2.setFont(u8g2_font_logisoso46_tr);
    int tw = u8g2.getStrWidth(timeBuf);
    u8g2.drawStr((256 - tw) / 2, 50, timeBuf);

    // Разделитель
    u8g2.drawHLine(0, 53, 256);

    // Нижняя строка: дата слева, IP справа
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(2, 63, dateBuf);

    if (localIP.length()) {
        int iw = u8g2.getStrWidth(localIP.c_str());
        u8g2.drawStr(254 - iw, 63, localIP.c_str());
    }

    if (!timeSynced) {
        u8g2.setFont(u8g2_font_4x6_tr);
        u8g2.drawStr(110, 63, "NO NTP");
    }

    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    // Заставка
    u8g2.begin();
    u8g2.setContrast(200);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(50, 35, "Connecting WiFi...");
    u8g2.sendBuffer();

    connectWifi();
    syncNTP();

    // Веб-сервер
    server.on("/",         HTTP_GET, handleRoot);
    server.on("/api/time", HTTP_GET, handleApiTime);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();
    updateTimeStrings();
    drawOLED();
    delay(1000);
}