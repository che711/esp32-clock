#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================
//  battery_calc.h — пересчёт напряжения 18650 в проценты.
//
//  Без Arduino: собирается нативно и покрыто тестами.
//  Чтение АЦП и делителя — в battery.cpp.
// ============================================================

// Границы правдоподобия. Вне диапазона Li-ion → скорее всего АКБ
// не подключена (вход висит в воздухе или питание чисто от USB).
inline bool batteryVoltagePlausible(float v) {
    return v >= 2.80f && v <= 4.35f;
}

inline uint8_t batteryVoltageToPercent(float v) {
    // Кривая разряда Li-ion 18650 под малой нагрузкой (V → %)
    static const struct { float v; uint8_t pct; } CURVE[] = {
        {4.20f, 100}, {4.06f, 90}, {3.98f, 80}, {3.92f, 70},
        {3.87f, 60},  {3.82f, 50}, {3.79f, 40}, {3.77f, 30},
        {3.74f, 20},  {3.68f, 10}, {3.45f, 5},  {3.00f, 0},
    };
    static const size_t CURVE_N = sizeof(CURVE) / sizeof(CURVE[0]);

    if (v >= CURVE[0].v)           return 100;
    if (v <= CURVE[CURVE_N - 1].v) return 0;

    for (size_t i = 1; i < CURVE_N; i++) {
        if (v >= CURVE[i].v) {
            // Линейная интерполяция между точками i-1 и i
            float span = CURVE[i - 1].v - CURVE[i].v;
            float frac = (v - CURVE[i].v) / span;
            return (uint8_t)(CURVE[i].pct + frac * (CURVE[i - 1].pct - CURVE[i].pct));
        }
    }
    return 0;
}
