#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
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
void drawStopwatchFrame();
void invalidateSwLayout();

// ─── Секундомер: приём команд ─────────────────────────────
void swStart() {
    if (swState != SW_RUNNING) {
        swStartMs = millis();
        swState   = SW_RUNNING;
        invalidateSwLayout();       // первый кадр после старта — полный
        // Modem sleep задерживает доставку WS-пакетов до ~0.9 с: пока идёт
        // отсчёт, это прямая ошибка синхронизации, поэтому радио не усыпляем.
        WiFi.setSleep(false);
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
    invalidateSwLayout();
    WiFi.setSleep(true);            // отсчёт кончился — возвращаем экономию
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
        // "ping:<токен>" → "pong:<токен>:<state>:<elapsed>".
        // Горячий путь замера задержки: ни логов, ни рассылки состояния —
        // клиент по RTT вычисляет, каким был счётчик в момент приёма ответа.
        if (length >= 5 && strncmp((char*)payload, "ping:", 5) == 0) {
            size_t tokLen = length - 5;
            if (tokLen > 12) tokLen = 12;
            char reply[48];
            snprintf(reply, sizeof(reply), "pong:%.*s:%d:%lu",
                     (int)tokLen, (char*)payload + 5,
                     (int)swState, (unsigned long)swElapsed());
            webSocket.sendTXT(num, reply);
            return;
        }

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

// Нижняя строка (y=63): слева сегменты через тонкий разделитель,
// справа иконка батареи + проценты.
//   mark     — маркер перед сегментами ("II " на паузе), можно nullptr
//   withDate — день недели и дата (на часах)
//   withTemp — температура (на секундомере; на часах она уже крупно сверху)
static void drawBottomStatus(const char* mark, bool withDate, bool withTemp) {
    u8g2.setFont(u8g2_font_5x7_tr);

    char tstr[12], pstr[12];
    const char* segs[4];
    int n = 0;

    if (withDate && timeSynced) {
        segs[n++] = dayShortBuf;
        segs[n++] = dateBuf;
    }
    if (!weather.valid) {
        segs[n++] = "no sensor";
    } else {
        if (withTemp) {
            snprintf(tstr, sizeof(tstr), "%.1fC", weather.temperature);
            segs[n++] = tstr;
        }
        snprintf(pstr, sizeof(pstr), "%.0fhPa", weather.pressure);
        segs[n++] = pstr;
    }

    int x = 2;
    if (mark && mark[0]) {
        u8g2.drawStr(x, 63, mark);
        x += u8g2.getStrWidth(mark);
    }
    for (int i = 0; i < n; i++) {
        if (i) {
            // Точка по центру строчных: вертикальная черта в высоту строки
            // читалась как буква и сливалась с текстом.
            u8g2.drawBox(x + 3, 59, 2, 2);
            x += 8;
        }
        u8g2.drawStr(x, 63, segs[i]);
        x += u8g2.getStrWidth(segs[i]);
    }

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
// Раскладка часов: поля слева/справа и место под знак градуса.
static const int CLOCK_MARGIN = 4;
static const int DEGREE_W     = 9;    // кружок градуса после цифр
static const int SEP_GAP      = 8;    // воздух вокруг разделителя
// Базовая линия температуры. Часы стоят на 50, но шрифт температуры мельче,
// поэтому её блок центрируем в полосе цифр (0..53) — визуально это середина.
static const int TEMP_BASELINE = 42;

// Геометрия поля ".mmm" последнего полного кадра секундомера.
// swMsBoxX < 0 — данных нет, следующий кадр обязан быть полным.
static int  swMsX = 0, swMsBoxX = -1, swMsBoxW = 0;
static char swMmSs[6] = "";

void invalidateSwLayout() { swMsBoxX = -1; }

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

        // Запоминаем геометрию поля ".mmm" — по ней идёт быстрая
        // частичная перерисовка в drawStopwatchFrame().
        swMsX    = xMs;
        swMsBoxX = (xMs / 8) * 8;
        int boxRight = ((xMs + msW + 7) / 8) * 8 + 8;   // +тайл запаса справа
        if (boxRight > 256) boxRight = 256;
        swMsBoxW = boxRight - swMsBoxX;
        memcpy(swMmSs, full, 5);  swMmSs[5] = '\0';

        u8g2.drawHLine(0, 53, 256);

        // Низ: погода + батарея, с маркером состояния секундомера
        drawBottomStatus(swState == SW_RUNNING ? nullptr : "II ", false, true);

    } else {
        // ── Обычный режим часов ────────────────────────
        // Секунды не показываем — освободившуюся половину экрана забирает
        // температура. Слева градусы, справа HH:MM, между ними разделитель.
        char hh[3] = { timeBuf[0], timeBuf[1], 0 };
        char mm[3] = { timeBuf[3], timeBuf[4], 0 };

        u8g2.setFont(u8g2_font_logisoso46_tr);
        int dw  = u8g2.getStrWidth("00");
        int cw  = u8g2.getStrWidth(":");
        const int gap = 4;
        int clockW = dw * 2 + cw + gap * 2;

        // Часы прижаты к правому краю, разделитель — на фиксированном месте
        // слева от них, чтобы не дёргался при смене ширины температуры.
        int xClock = 256 - CLOCK_MARGIN - clockW;
        int xSep   = xClock - SEP_GAP;

        int x = xClock;
        u8g2.drawStr(x, 50, hh);  x += dw + gap;
        u8g2.drawStr(x, 50, ":"); x += cw + gap;
        u8g2.drawStr(x, 50, mm);

        u8g2.drawVLine(xSep, 4, 46);     // разделитель на всю высоту цифр

        // Температура крупно слева. Знак градуса рисуем кружком:
        // в _tr-наборе шрифта символа ° нет.
        char tstr[8];
        if (weather.valid) snprintf(tstr, sizeof(tstr), "%.1f", weather.temperature);
        else               snprintf(tstr, sizeof(tstr), "--");

        int avail = xSep - SEP_GAP - CLOCK_MARGIN - DEGREE_W;
        int th = 32;
        u8g2.setFont(u8g2_font_logisoso32_tr);
        int tw = u8g2.getStrWidth(tstr);
        if (tw > avail && weather.valid) {         // "-12.3" шире плюсовой:
            snprintf(tstr, sizeof(tstr), "%.0f", weather.temperature);
            tw = u8g2.getStrWidth(tstr);           // сперва жертвуем десятыми
        }
        if (tw > avail) {                          // и только потом размером
            th = 26;
            u8g2.setFont(u8g2_font_logisoso26_tr);
            tw = u8g2.getStrWidth(tstr);
        }

        // Блок «цифры + градус» по центру левой половины. По вертикали он не
        // стоит на базовой линии часов, а поднят к середине полосы цифр —
        // шрифт мельче, и на общей базовой линии температура висела низко.
        int xT = CLOCK_MARGIN + (avail - tw) / 2;
        u8g2.drawStr(xT, TEMP_BASELINE, tstr);
        u8g2.drawCircle(xT + tw + 4, TEMP_BASELINE - th + 4, 3);   // ° у верха цифр

        u8g2.drawHLine(0, 53, 256);

        // Низ: день недели | дата | давление, справа батарея.
        // Температура ушла наверх, поэтому внизу её нет.
        drawBottomStatus(nullptr, true, false);
        swMsBoxX = -1;                 // на часах поля ".mmm" нет
    }

    u8g2.sendBuffer();
#endif
}

// Кадр секундомера. Полный кадр стоит ~24 мс (8 КБ буфера по SPI на 10 МГц),
// поэтому гнать его 25 раз в секунду нельзя. Но между секундами меняется
// только поле ".mmm" — его полоску (4 тайла по высоте) отправляем отдельно,
// это ~2 мс, и миллисекунды идут плавно. Полный кадр — раз в секунду,
// когда меняется "MM:SS" и нижняя строка состояния.
void drawStopwatchFrame() {
#if !HAS_DISPLAY
    return;
#else
    if (!displayOn) return;

    uint32_t el = swElapsed();
    char full[16];
    formatStopwatch(el, full, sizeof(full));

    // >= 1 часа формат "HH:MM:SS" — миллисекунд нет, дробить нечего.
    if (el >= 3600000UL || swMsBoxX < 0 || memcmp(full, swMmSs, 5) != 0) {
        drawOLED();
        return;
    }

    u8g2.setDrawColor(0);
    u8g2.drawBox(swMsBoxX, 24, swMsBoxW, 27);   // y 24..50, линию на y=53 не трогаем
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_logisoso24_tr);
    u8g2.drawStr(swMsX, 50, full + 5);
    u8g2.updateDisplayArea(swMsBoxX / 8, 3, swMsBoxW / 8, 4);
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

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // Serial здесь — нативный USB-CDC, и его write() блокирующий: пока хост
    // держит порт открытым, но никто не вычитывает, буфер переполняется и
    // Serial.printf() встаёт на секунды, останавливая весь loop(). Именно так
    // секундомер и отставал от браузера на несколько секунд. Нулевой таймаут —
    // лишний вывод молча теряется, зато отсчёт времени больше не зависит от
    // того, открыт ли монитор.
    Serial.setTxTimeoutMs(0);
#endif
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
            drawStopwatchFrame();
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

    // Пока секундомер не в покое — короткий цикл: команда "sw:start"/"sw:pause"
    // не должна ждать в очереди 100 мс, иначе устройство стартует/останавливается
    // заметно позже браузера и расхождение копится с каждым Start/Pause.
    delay(swState == SW_IDLE ? 100 : 5);
}
