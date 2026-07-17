#pragma once
#include <Arduino.h>

// ============================================================
//  sensor.h  —  BMP280 данные + производные расчёты
// ============================================================

// История давления для расчёта тренда:
// 12 точек × 5 мин (PRESSURE_HISTORY_INTERVAL_MS) = окно в 1 час
#define PRESSURE_HISTORY_SIZE 12

struct SensorData {
    // Сырые данные
    float temperature;      // °C
    float pressure;         // гПа
    float altitude;         // м (расчётная по SEA_LEVEL_HPA)
    bool  valid;

    // Производные
    float pressureMmHg;     // мм.рт.ст.
    float pressureQnh;      // приведённое к уровню моря, гПа
    float airDensity;       // кг/м³
    float pressureTrend;    // гПа/ч (+ рост, - падение)
    uint8_t forecastIcon;   // 0=нет данных 1=ясно 2=переменно 3=дождь
};

bool       sensorInit();
SensorData sensorRead();

const char* forecastText(uint8_t icon);
const char* forecastEmoji(uint8_t icon);
