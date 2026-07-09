#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ─── Единая шкала контраста ───────────────────────────────
// SSD1322 принимает контраст 0..255. Держим одну константу,
// чтобы проценты в UI, ручная установка и авто-яркость
// считались по одной и той же шкале (раньше было 200 и 255).
static const uint8_t CONTRAST_MAX = 255;

// ─── Uptime ───────────────────────────────────────────────
inline void formatUptime(uint32_t totalSeconds, char* buf, size_t sz) {
    uint32_t s = totalSeconds;
    uint32_t d = s / 86400; s %= 86400;
    uint32_t h = s / 3600;  s %= 3600;
    uint32_t m = s / 60;    s %= 60;
    if (d > 0)
        snprintf(buf, sz, "%ud %02uh %02um %02us", d, h, m, s);
    else
        snprintf(buf, sz, "%02uh %02um %02us", h, m, s);
}

// ─── Яркость по часу ─────────────────────────────────────
struct BrightnessLevel {
    uint8_t     contrast;
    const char* label;
};

inline BrightnessLevel brightnessForHour(int hour) {
    if (hour >= 22 || hour < 6)  return { 15,  "Night"   };
    if (hour < 8)                return { 80,  "Morning" };
    if (hour < 20)               return { 200, "Day"     };
    return                              { 120, "Evening" };
}

// contrast(0..255) → проценты(0..100)
inline uint8_t brightnessPct(uint8_t contrast) {
    return (uint8_t)((uint32_t)contrast * 100 / CONTRAST_MAX);
}

// проценты(0..100) → contrast(0..255)
inline uint8_t pctToContrast(int pct) {
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)((uint32_t)pct * CONTRAST_MAX / 100);
}

// ─── Форматирование времени ───────────────────────────────
inline void formatTime(int h, int m, int s, char* buf, size_t sz) {
    snprintf(buf, sz, "%02d:%02d:%02d", h, m, s);
}

inline void formatDate(int day, const char* month, int year,
                        char* buf, size_t sz) {
    snprintf(buf, sz, "%02d %s %04d", day, month, year);
}

// ─── Форматирование секундомера ───────────────────────────
// Общая логика MM:SS.mmm, чтобы её можно было покрыть тестом.
inline void formatStopwatch(uint32_t ms, char* buf, size_t sz) {
    uint32_t mins   = (ms / 60000) % 100;   // до 99 минут
    uint32_t secs   = (ms / 1000)  % 60;
    uint32_t millis = ms % 1000;
    snprintf(buf, sz, "%02u:%02u.%03u",
             (unsigned)mins, (unsigned)secs, (unsigned)millis);
}

// ─── Валидация JSON-поля ──────────────────────────────────
inline bool jsonContainsKey(const char* json, const char* key) {
    return strstr(json, key) != nullptr;
}

// ─── WiFi signal → уровень 1–4 ───────────────────────────
inline int rssiToLevel(int rssi) {
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    return 1;
}
