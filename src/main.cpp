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
const long  GMT_OFFSET = 3600;
const int   DST_OFFSET = 3600;

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

// ─── HTML ─────────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Clock</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&family=JetBrains+Mono:wght@400;700&display=swap" rel="stylesheet">
<style>
  *, *::before, *::after { margin:0; padding:0; box-sizing:border-box; }

  body {
    background: #080e1a;
    color: #c9d8f0;
    font-family: 'Inter', system-ui, sans-serif;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 24px 16px;
    gap: 14px;
  }

  .card {
    background: #0d1626;
    border: 1px solid #1c2d4a;
    border-radius: 20px;
    width: 100%;
    max-width: 480px;
  }

  /* ── Часы ── */
  .clock-card {
    padding: 32px 28px 24px;
    text-align: center;
  }

  #time {
    font-family: 'JetBrains Mono', monospace;
    font-size: clamp(52px, 16vw, 88px);
    font-weight: 700;
    letter-spacing: -1px;
    line-height: 1;
    color: #ffffff;
    white-space: nowrap;
  }

  .divider {
    border: none;
    border-top: 1px solid #1c2d4a;
    margin: 18px 0 14px;
  }

  #date {
    font-size: clamp(13px, 3.5vw, 18px);
    font-weight: 500;
    color: #4a7ab5;
    letter-spacing: 3px;
    text-transform: uppercase;
  }

  #day {
    font-size: clamp(11px, 2.5vw, 14px);
    color: #2a4a72;
    letter-spacing: 4px;
    margin-top: 5px;
    text-transform: uppercase;
  }

  /* ── Статистика ── */
  .stats-card {
    padding: 20px 24px;
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 18px 20px;
  }

  .stat { display: flex; flex-direction: column; gap: 3px; }
  .stat.full { grid-column: span 2; }

  .stat-label {
    font-size: 10px;
    font-weight: 600;
    color: #2a4a72;
    letter-spacing: 1.5px;
    text-transform: uppercase;
  }

  .stat-value {
    font-size: 13px;
    font-family: 'JetBrains Mono', monospace;
    color: #7aaad8;
    margin-top: 1px;
  }
  .stat-value b { color: #c9d8f0; font-weight: 600; }

  /* прогресс-бар */
  .bar-wrap {
    background: #0a1525;
    border-radius: 3px;
    height: 3px;
    margin-top: 6px;
    overflow: hidden;
  }
  .bar-fill {
    height: 100%;
    border-radius: 3px;
    background: #2563eb;
    transition: width 0.9s ease;
  }
  .bar-fill.warn { background: #d97706; }
  .bar-fill.crit { background: #dc2626; }

  /* WiFi dots */
  .wifi-dots {
    display: inline-flex;
    gap: 3px;
    align-items: flex-end;
    margin-left: 7px;
    vertical-align: middle;
  }
  .wifi-dots span {
    display: inline-block;
    width: 5px;
    border-radius: 2px;
    background: #1c2d4a;
  }
  .wifi-dots span.on { background: #2563eb; }

  /* curl hint */
  .curl-hint {
    width: 100%;
    max-width: 480px;
    background: #060d18;
    border: 1px solid #1c2d4a;
    border-radius: 12px;
    padding: 14px 18px;
  }
  .curl-hint .hint-label {
    font-size: 10px;
    color: #2a4a72;
    letter-spacing: 1.5px;
    text-transform: uppercase;
    font-weight: 600;
    margin-bottom: 8px;
  }
  .curl-hint code {
    font-family: 'JetBrains Mono', monospace;
    font-size: 11px;
    color: #4a90d9;
    display: block;
    line-height: 1.8;
    word-break: break-all;
  }
  .curl-hint code .cmd { color: #7aaad8; }
  .curl-hint code .url { color: #34d399; }

  /* статус */
  .status {
    font-size: 10px;
    color: #1c2d4a;
    letter-spacing: 2px;
    font-weight: 500;
  }
  .dot {
    display: inline-block;
    width: 6px; height: 6px;
    border-radius: 50%;
    background: #16a34a;
    margin-right: 6px;
    vertical-align: middle;
    animation: pulse 2s infinite;
  }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:.25} }
</style>
</head>
<body>

<div class="card clock-card">
  <div id="time">--:--:--</div>
  <hr class="divider">
  <div id="date">-- --- ----</div>
  <div id="day">---------</div>
</div>

<div class="card stats-card">

  <div class="stat">
    <div class="stat-label">Uptime</div>
    <div class="stat-value" id="uptime">—</div>
  </div>

  <div class="stat">
    <div class="stat-label">Chip Temperature</div>
    <div class="stat-value"><b id="temp">—</b> °C</div>
  </div>

  <div class="stat">
    <div class="stat-label">WiFi SSID</div>
    <div class="stat-value"><b id="ssid">—</b></div>
  </div>

  <div class="stat">
    <div class="stat-label">IP Address</div>
    <div class="stat-value"><b id="ip">—</b></div>
  </div>

  <div class="stat full">
    <div class="stat-label">
      WiFi Signal &nbsp;
      <span id="rssi-val" style="color:#4a7ab5">—</span> dBm
      <span class="wifi-dots">
        <span id="d1" style="height:4px"></span>
        <span id="d2" style="height:7px"></span>
        <span id="d3" style="height:10px"></span>
        <span id="d4" style="height:13px"></span>
      </span>
    </div>
    <div class="bar-wrap"><div class="bar-fill" id="rssi-bar" style="width:0%"></div></div>
  </div>

  <div class="stat full">
    <div class="stat-label">RAM &nbsp;<span id="ram-txt" style="color:#4a7ab5;font-weight:400">—</span></div>
    <div class="bar-wrap"><div class="bar-fill" id="ram-bar" style="width:0%"></div></div>
  </div>

</div>

<div class="curl-hint">
  <div class="hint-label">API · GET запрос</div>
  <code><span class="cmd">curl</span> <span class="url" id="curl-url">http://&lt;IP&gt;/api/time</span></code>
  <code style="color:#4a7ab5;font-size:10px;margin-top:4px">→ {"time":"12:34:56","date":"10 MAY 2026","day":"Sunday","uptime":"01h 23m 45s","timestamp":1746870000}</code>
</div>

<div class="status"><span class="dot"></span>ESP32-C3 SUPER MINI</div>

<script>
  function setBar(id, pct) {
    const el = document.getElementById(id);
    el.style.width = pct + '%';
    el.className = 'bar-fill' + (pct>85?' crit':pct>60?' warn':'');
  }
  function setWifi(rssi) {
    const lvl = rssi>=-50?4:rssi>=-60?3:rssi>=-70?2:1;
    for(let i=1;i<=4;i++)
      document.getElementById('d'+i).className = i<=lvl?'on':'';
    setBar('rssi-bar', Math.max(0, Math.min(100, (rssi+90)*2)));
  }

  async function tick() {
    try {
      const d = await (await fetch('/api/stats')).json();
      document.getElementById('time').textContent = d.time;
      document.getElementById('date').textContent = d.date;
      document.getElementById('day').textContent  = d.day;
      document.getElementById('uptime').textContent = d.uptime;
      document.getElementById('ssid').textContent   = d.ssid;
      document.getElementById('ip').textContent     = d.ip;
      document.getElementById('temp').textContent   = d.temp;
      document.getElementById('rssi-val').textContent = d.rssi;
      document.getElementById('curl-url').textContent = 'http://' + d.ip + '/api/time';
      setWifi(d.rssi);

      const used = d.ram_total - d.ram_free;
      const pct  = Math.round(used * 100 / d.ram_total);
      document.getElementById('ram-txt').textContent =
        (d.ram_free/1024).toFixed(1) + ' KB free / ' +
        (d.ram_total/1024).toFixed(1) + ' KB total · ' + pct + '% used';
      setBar('ram-bar', pct);
    } catch(e) {}
  }
  tick();
  setInterval(tick, 2000);
</script>
</body>
</html>
)rawliteral";

// ─── HTTP ─────────────────────────────────────────────────
void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

// /api/stats — для веб-страницы
void handleApiStats() {
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"time\":\"%s\","
        "\"date\":\"%s\","
        "\"day\":\"%s\","
        "\"uptime\":\"%s\","
        "\"ssid\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"temp\":\"%.1f\","
        "\"ram_free\":%lu,"
        "\"ram_total\":%lu"
        "}",
        timeBuf, dateBuf, dayFullBuf,
        getUptime().c_str(),
        WIFI_SSID,
        localIP.c_str(),
        (int)WiFi.RSSI(),
        (float)temperatureRead(),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)ESP.getHeapSize()
    );
    server.send(200, "application/json", json);
}

// /api/time — простой эндпоинт для curl / внешних систем
void handleApiTime() {
    struct tm t;
    time_t now = time(nullptr);
    getLocalTime(&t);
    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"time\":\"%s\","
        "\"date\":\"%s\","
        "\"day\":\"%s\","
        "\"uptime\":\"%s\","
        "\"timestamp\":%lu"
        "}",
        timeBuf, dateBuf, dayFullBuf,
        getUptime().c_str(),
        (unsigned long)now
    );
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

    // Нижняя строка: день  дата  SSID  IP
    u8g2.setFont(u8g2_font_5x7_tr);
    if (timeSynced) {
        // SSID обрезаем до 8 символов если длиннее
        char ssidShort[10];
        strncpy(ssidShort, WIFI_SSID, 8);
        ssidShort[8] = 0;

        char bottom[56];
        if (localIP.length())
            snprintf(bottom, sizeof(bottom), "%s %s %s %s",
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

    server.on("/",          HTTP_GET, handleRoot);
    server.on("/api/stats", HTTP_GET, handleApiStats);
    server.on("/api/time",  HTTP_GET, handleApiTime);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started");
}

void loop() {
    server.handleClient();
    updateTimeStrings();

    // Перерисовываем дисплей только если время изменилось
    if (strcmp(timeBuf, prevTimeBuf) != 0) {
        drawOLED();
        strncpy(prevTimeBuf, timeBuf, sizeof(prevTimeBuf));
    }

    delay(100); // опрашиваем часто → изменение отображается быстро
}