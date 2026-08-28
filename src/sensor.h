#pragma once
#include <Arduino.h>
#include "weather_calc.h"   // PRESSURE_HISTORY_SIZE + чистая арифметика

// ============================================================
//  sensor.h  —  BMP280 данные + производные расчёты
// ============================================================

struct SensorData {
    // Сырые данные
    float temperature;      // °C
    float pressure;         // гПа
    bool  valid;

    // Производные
    float pressureMmHg;     // мм.рт.ст.
    float pressureQnh;      // приведённое к морю от HOME_ALTITUDE_M, гПа
    float airDensity;       // кг/м³
    float pressureTrend;    // гПа/ч (+ рост, - падение)
    uint8_t forecastIcon;   // 0=нет данных 1=ясно 2=переменно 3=дождь
};

bool       sensorInit();
SensorData sensorRead();

const char* forecastText(uint8_t icon);
const char* forecastEmoji(uint8_t icon);
