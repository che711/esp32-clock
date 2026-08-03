#pragma once
#include <stdint.h>
#include <math.h>

// ============================================================
//  weather_calc.h — чистая арифметика метеоблока.
//
//  Без Arduino и без обращений к железу: всё, что здесь лежит,
//  собирается нативно и покрыто тестами (test/test_native).
//  Работа с BMP280 — в sensor.cpp.
// ============================================================

// История давления для расчёта тренда:
// 12 точек × 5 мин (PRESSURE_HISTORY_INTERVAL_MS) = окно в 1 час
#define PRESSURE_HISTORY_SIZE 12

// ─── Производные от давления ──────────────────────────────
inline float pressureToMmHg(float hPa) {
    return hPa * 0.75006f;
}

// QNH — давление, приведённое к уровню моря:
// P0 = P * (1 - 0.0065*h / (T + 0.0065*h + 273.15))^(-5.257)
inline float pressureToQnh(float hPa, float tempC, float altitudeM) {
    const float h = altitudeM;
    return hPa * powf(1.0f - 0.0065f * h / (tempC + 0.0065f * h + 273.15f), -5.257f);
}

// Плотность сухого воздуха: ρ = P / (R * T), R_specific = 287.05 Дж/(кг·К)
inline float airDensityOf(float hPa, float tempC) {
    return (hPa * 100.0f) / (287.05f * (tempC + 273.15f));
}

// Отсев мусора с датчика: NaN и давление вне земного диапазона
inline bool weatherPlausible(float tempC, float hPa) {
    return !isnan(tempC) && !isnan(hPa) && hPa >= 300.0f && hPa <= 1200.0f;
}

// ─── Прогноз (упрощённый метод Бьюри) ─────────────────────
// 0=нет данных 1=ясно 2=переменно 3=осадки
inline uint8_t forecastFromTrend(float trendPerHour, uint8_t samples) {
    if (samples < 3)          return 0;   // недостаточно данных (≥ 10 мин истории)
    if (trendPerHour >  0.5f) return 1;   // ясно
    if (trendPerHour < -0.5f) return 3;   // дождь
    return 2;                             // переменно
}

// ─── Кольцевой буфер истории давления ─────────────────────
struct PressureHistory {
    float    pressure[PRESSURE_HISTORY_SIZE] = {};
    uint32_t stampMs[PRESSURE_HISTORY_SIZE]  = {};
    uint8_t  idx      = 0;   // слот СЛЕДУЮЩЕЙ записи
    uint8_t  count    = 0;
    uint32_t lastPush = 0;

    // Точка пишется раз в intervalMs, а не при каждом опросе датчика.
    // Иначе окно тренда = 12 × 10 с = 2 минуты, и порог ±0.5 гПа/ч
    // ловил бы чистый шум сенсора. Возвращает true, если точка записана.
    bool maybePush(float hPa, uint32_t nowMs, uint32_t intervalMs) {
        if (count != 0 && nowMs - lastPush < intervalMs) return false;
        lastPush       = nowMs;
        pressure[idx]  = hPa;
        stampMs[idx]   = nowMs;
        idx            = (idx + 1) % PRESSURE_HISTORY_SIZE;
        if (count < PRESSURE_HISTORY_SIZE) count++;
        return true;
    }

    // Тренд в гПа/ч по всему накопленному окну.
    float trendPerHour() const {
        if (count < 2) return 0.0f;

        // idx указывает на СЛЕДУЮЩИЙ слот записи, поэтому:
        //   newest = idx - 1
        //   oldest = idx - count        (было idx - count + 1 → off-by-one,
        //                                самая старая точка выпадала из окна)
        uint8_t newest = (idx + PRESSURE_HISTORY_SIZE - 1) % PRESSURE_HISTORY_SIZE;
        uint8_t oldest = (idx + PRESSURE_HISTORY_SIZE - count) % PRESSURE_HISTORY_SIZE;

        float    dp = pressure[newest] - pressure[oldest];
        uint32_t dt = stampMs[newest] - stampMs[oldest];  // беззнаковое вычитание
                                                          // корректно и при overflow millis()
        if (dt < 1000) return 0.0f;
        return dp / (dt / 3600000.0f);   // гПа/ч
    }
};
