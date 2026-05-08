#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <U8g2lib.h>
#include <SPI.h>

// ─── WiFi ────────────────────────────────────────────────
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

// ─── NTP ─────────────────────────────────────────────────
const char* NTP_SERVER   = "pool.ntp.org";
const long  GMT_OFFSET   = 3600;        // UTC+1 (Польша зимой)
const int   DST_OFFSET   = 3600;        // +1 час летом (CEST)

// ─── Пины SPI → SSD1322 ──────────────────────────────────
// SCK  → GPIO6  (аппаратный SPI)
// MOSI → GPIO7  (аппаратный SPI)
#define PIN_CS   5
#define PIN_DC   4
#define PIN_RST  3

// U8g2: SSD1322 256×64, аппаратный SPI
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI
    u8g2(U8G2_R0, PIN_CS, PIN_DC, PIN_RST);

// ─── Дни недели ──────────────────────────────────────────
const char* DAYS[] = {
    "ВОСКРЕСЕНЬЕ", "ПОНЕДЕЛЬНИК", "ВТОРНИК",
    "СРЕДА", "ЧЕТВЕРГ", "ПЯТНИЦА", "СУББОТА"
};
const char* MONTHS[] = {
    "ЯНВ","ФЕВ","МАР","АПР","МАЙ","ИЮН",
    "ИЮЛ","АВГ","СЕН","ОКТ","НОЯ","ДЕК"
};

// ─── Буферы ──────────────────────────────────────────────
char timeBuf[9];   // "HH:MM:SS"
char dateBuf[20];  // "12 МАЙ 2025"
char dayBuf[20];   // "ПЯТНИЦА"

bool timeSynced = false;

// ─────────────────────────────────────────────────────────
void connectWifi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint8_t tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" OK");
    } else {
        Serial.println(" FAIL (offline mode)");
    }
}

void syncNTP() {
    if (WiFi.status() != WL_CONNECTED) return;
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
    Serial.print("Waiting for NTP");
    struct tm t;
    uint8_t tries = 0;
    while (!getLocalTime(&t) && tries < 20) {
        delay(500);
        Serial.print(".");
        tries++;
    }
    if (tries < 20) {
        timeSynced = true;
        Serial.println(" synced");
    } else {
        Serial.println(" timeout");
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);  // экономим энергию
}

void updateTimeStrings(struct tm& t) {
    snprintf(timeBuf, sizeof(timeBuf),
             "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    snprintf(dateBuf, sizeof(dateBuf),
             "%02d %s %04d", t.tm_mday, MONTHS[t.tm_mon], t.tm_year + 1900);
    snprintf(dayBuf,  sizeof(dayBuf),
             "%s", DAYS[t.tm_wday]);
}

void drawClock() {
    u8g2.clearBuffer();

    // ── Время: крупный шрифт по центру ──────────────────
    u8g2.setFont(u8g2_font_logisoso32_tr);  // высота ~32px
    int tw = u8g2.getStrWidth(timeBuf);
    u8g2.drawStr((256 - tw) / 2, 36, timeBuf);

    // ── Разделитель ─────────────────────────────────────
    u8g2.drawHLine(0, 40, 256);

    // ── Дата слева, день недели справа ──────────────────
    u8g2.setFont(u8g2_font_6x10_tr);       // маленький шрифт
    u8g2.drawStr(4,   54, dateBuf);
    int dw = u8g2.getStrWidth(dayBuf);
    u8g2.drawStr(252 - dw, 54, dayBuf);

    // ── Иконка "нет синхронизации" ──────────────────────
    if (!timeSynced) {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(4, 63, "NO NTP");
    }

    u8g2.sendBuffer();
}

// ─────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    u8g2.begin();
    u8g2.setContrast(200);  // яркость 0–255

    // Заставка
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(70, 35, "Connecting WiFi...");
    u8g2.sendBuffer();

    connectWifi();
    syncNTP();
}

void loop() {
    struct tm t;
    if (getLocalTime(&t)) {
        updateTimeStrings(t);
    } else {
        // Если время не получено — показываем прочерки
        snprintf(timeBuf, sizeof(timeBuf), "--:--:--");
        snprintf(dateBuf, sizeof(dateBuf), "-- --- ----");
        snprintf(dayBuf,  sizeof(dayBuf),  "-------");
    }
    drawClock();
    delay(1000);
}

