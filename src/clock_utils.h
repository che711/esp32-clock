#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ─── Uptime ───────────────────────────────────────────────
inline void formatUptime(uint32_t totalSeconds, char* buf, size_t sz) {
    uint32_t s = totalSeconds;
    uint32_t d = s / 86400; s %= 86400;
    uint32_t h = s / 3600;  s %= 3600;
    uint32_t m = s / 60;    s %= 60;
    if (d > 0)
        snprintf(buf, sz, "%dd %02dh %02dm %02ds", d, h, m, s);
    else
        snprintf(buf, sz, "%02dh %02dm %02ds", h, m, s);
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

inline uint8_t brightnessPct(uint8_t contrast) {
    return (uint8_t)((uint32_t)contrast * 100 / 200);
}

// ─── Форматирование времени ───────────────────────────────
inline void formatTime(int h, int m, int s, char* buf, size_t sz) {
    snprintf(buf, sz, "%02d:%02d:%02d", h, m, s);
}

inline void formatDate(int day, const char* month, int year,
                        char* buf, size_t sz) {
    snprintf(buf, sz, "%02d %s %04d", day, month, year);
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
