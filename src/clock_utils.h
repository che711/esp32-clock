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
    // Приведение к unsigned обязательно: на 32-битной цели uint32_t —
    // это unsigned long, и %u без каста ловит -Wformat.
    if (d > 0)
        snprintf(buf, sz, "%ud %02uh %02um %02us",
                 (unsigned)d, (unsigned)h, (unsigned)m, (unsigned)s);
    else
        snprintf(buf, sz, "%02uh %02um %02us",
                 (unsigned)h, (unsigned)m, (unsigned)s);
}

// ─── Яркость по часу ─────────────────────────────────────
struct BrightnessLevel {
    uint8_t     contrast;
    const char* label;
};

// Уровни заданы на ПЕРЦЕПТИВНОЙ шкале интерфейса 0..255: в ток панели их
// переводит panelDriveForLevel() по гамме 2.2 (display_calc.h). Раньше таблица
// была 15/80/120/200 и уезжала прямо в регистр 0xC1, то есть задавала долю
// тока напрямую. После перехода на гамму те же числа стали означать совсем
// другую светимость — ночь просела с 6.3 % до 0.2 % полного тока, то есть в
// тридцать раз, и на панели её было уже не разглядеть.
//
// Поэтому числа пересчитаны обратно: L = 255 * f^(1/2.2), где f — прежняя
// доля тока (contrast+1)/256. Светимость авто-уровней вернулась к той, что
// была до гаммы, а перцептивной шкалой продолжает пользоваться ползунок.
static const uint8_t CONTRAST_NIGHT   = 72;    // ≈ 6.3 % тока (было 15/255)
static const uint8_t CONTRAST_MORNING = 151;   // ≈ 31.6 %     (было 80/255)
static const uint8_t CONTRAST_EVENING = 181;   // ≈ 47.3 %     (было 120/255)
static const uint8_t CONTRAST_DAY     = 228;   // ≈ 78.5 %     (было 200/255)

// Самая нижняя ступень, на которой экран ещё читается. Регистрами тока панель
// уводится и ниже — до 1/4096, — но там она уже неразличима, а команда
// «включить экран» должна давать видимый результат. Берём ночной уровень: это
// и есть самое тусклое, что проект считает читаемым.
static const uint8_t CONTRAST_MIN_VISIBLE = CONTRAST_NIGHT;

inline BrightnessLevel brightnessForHour(int hour) {
    if (hour >= 22 || hour < 6)  return { CONTRAST_NIGHT,   "Night"   };
    if (hour < 8)                return { CONTRAST_MORNING, "Morning" };
    if (hour < 20)               return { CONTRAST_DAY,     "Day"     };
    return                              { CONTRAST_EVENING, "Evening" };
}

// Часы не встали — час неизвестен, применять расписание не к чему. Берём
// средний уровень: держать полную яркость на батарее нельзя (именно этим
// раньше и заканчивался неудавшийся NTP), а гасить до ночного посреди дня
// незачем — время на экране прочерки, но метеоблок живой.
inline BrightnessLevel brightnessNoTime() {
    return { CONTRAST_EVENING, "No time" };
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
// До 60 минут  → MM:SS.mmm  (с миллисекундами)
// От 60 минут  → HH:MM:SS   (миллисекунды уже не нужны)
inline void formatStopwatch(uint32_t ms, char* buf, size_t sz) {
    uint32_t totalSec = ms / 1000;
    if (totalSec < 3600) {
        uint32_t mins   = totalSec / 60;
        uint32_t secs   = totalSec % 60;
        uint32_t millis = ms % 1000;
        snprintf(buf, sz, "%02u:%02u.%03u",
                 (unsigned)mins, (unsigned)secs, (unsigned)millis);
    } else {
        uint32_t hours = totalSec / 3600;
        uint32_t mins  = (totalSec % 3600) / 60;
        uint32_t secs  = totalSec % 60;
        snprintf(buf, sz, "%02u:%02u:%02u",
                 (unsigned)hours, (unsigned)mins, (unsigned)secs);
    }
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
