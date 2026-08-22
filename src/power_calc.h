#pragma once
#include <stdint.h>
#include <string.h>

// ============================================================
//  power_calc.h — выбор режима энергосбережения и профили.
//
//  Без Arduino: собирается нативно и покрыто тестами.
//  Применение режима к железу — в power.cpp.
//
//  Три уровня. Обычный — как было всегда. Эконом ужимает то,
//  что не видно на глаз: реже опрос датчика, тусклее экран,
//  радио спит дольше между маячками, экран гаснет вне рабочего
//  окна. Выживание выключает Wi-Fi совсем — часы продолжают
//  идти, время поправляется коротким выходом в сеть раз в сутки.
// ============================================================

enum PowerMode : uint8_t {
    POWER_NORMAL   = 0,
    POWER_ECO      = 1,
    POWER_SURVIVAL = 2,
};

struct PowerProfile {
    const char* name;
    uint32_t    sensorMs;        // период опроса BMP280
    uint8_t     contrastPct;     // масштаб авто-яркости, % от штатной
    bool        wifi;            // радио включено постоянно
    bool        led;             // мигок WS2812 на каждый опрос
    bool        screenWindow;    // гасить экран вне рабочего окна
    uint8_t     listenInterval;  // маячков между пробуждениями радио
    int8_t      txDbm;           // мощность передатчика
};

inline const PowerProfile& powerProfile(PowerMode m) {
    static const PowerProfile P[3] = {
        //  name        sensorMs  contrast  wifi   led    window  listen  tx
        { "normal",     10000UL,      100,  true,  true,  false,      1,  20 },
        { "eco",        30000UL,       60,  true,  false, true,       5,  11 },
        { "survival",   60000UL,       35,  false, false, true,       5,  11 },
    };
    return P[m > POWER_SURVIVAL ? POWER_NORMAL : m];
}

// Выбор режима по заряду.
//
// Вниз переключаемся сразу по порогу, вверх — только когда заряд поднялся
// выше порога на hyst. Без этого запаса режим дребезжал бы на границе:
// сглаженное напряжение всё равно гуляет на единицы милливольт, а в средней
// части кривой разряда это целые проценты.
//
// Батарея не определена (питание от USB) — режим всегда обычный: гадать
// по отсутствующим данным незачем.
inline PowerMode powerModeForCharge(PowerMode cur, uint8_t pct, bool valid,
                                    uint8_t ecoPct, uint8_t survPct, uint8_t hyst) {
    if (!valid) return POWER_NORMAL;

    if (pct <= survPct) return POWER_SURVIVAL;
    if (cur == POWER_SURVIVAL && pct < (uint16_t)survPct + hyst) return POWER_SURVIVAL;

    if (pct <= ecoPct) return POWER_ECO;
    if (cur != POWER_NORMAL && pct < (uint16_t)ecoPct + hyst) return POWER_ECO;

    return POWER_NORMAL;
}

// Попадает ли час в окно [onHour, offHour). Окно через полночь поддержано,
// совпадающие границы означают «круглые сутки».
inline bool hourInWindow(int hour, int onHour, int offHour) {
    if (onHour == offHour) return true;
    if (onHour <  offHour) return hour >= onHour && hour < offHour;
    return hour >= onHour || hour < offHour;
}

// Можно ли держать экран включённым. В обычном режиме — всегда.
inline bool powerScreenAllowed(PowerMode m, int hour, int onHour, int offHour) {
    if (!powerProfile(m).screenWindow) return true;
    return hourInWindow(hour, onHour, offHour);
}

// Разбор имени режима для API: "normal" | "eco" | "survival".
inline bool powerModeFromName(const char* s, PowerMode* out) {
    if (!s || !out) return false;
    for (uint8_t m = POWER_NORMAL; m <= POWER_SURVIVAL; m++) {
        if (strcmp(s, powerProfile((PowerMode)m).name) == 0) {
            *out = (PowerMode)m;
            return true;
        }
    }
    return false;
}
