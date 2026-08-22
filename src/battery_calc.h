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

// ── Фильтрация замеров ───────────────────────────────────
//  Wi-Fi потребляет импульсами: в момент передачи ток скачет со ~80
//  до ~300 мА, и напряжение на банке проваливается на десятки мВ.
//  Отсчёт, попавший в такой импульс, — выброс, а не измерение.
//  Среднее размазывает выброс по результату, медиана его отбрасывает.

// Медиана массива. Сортирует его на месте (вставками: n здесь ≤ 32).
inline uint32_t batteryMedianMv(uint32_t* v, size_t n) {
    if (n == 0) return 0;
    for (size_t i = 1; i < n; i++) {
        uint32_t key = v[i];
        size_t   j   = i;
        while (j > 0 && v[j - 1] > key) { v[j] = v[j - 1]; j--; }
        v[j] = key;
    }
    if (n & 1) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) / 2;
}

// Экспоненциальное сглаживание между наборами: alpha — вес свежего
// значения. Напряжение банки за доли секунды физически не меняется,
// поэтому быстрые движения — помеха, и давить их можно смело.
inline float batterySmooth(float prev, float fresh, float alpha) {
    return prev + alpha * (fresh - prev);
}

// Скачок больше порога — это не помеха, а событие: сняли USB, поменяли
// банку, сработала защита. Ползти к новому уровню полминуты незачем.
inline bool batteryJumped(float prev, float fresh, float threshold) {
    float d = fresh - prev;
    if (d < 0) d = -d;
    return d >= threshold;
}
