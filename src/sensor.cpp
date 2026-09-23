#include "sensor.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>

// ============================================================
//  sensor.cpp
// ============================================================

static Adafruit_BMP280 bmp;
static bool bmpInitialized = false;

// Кольцевой буфер для тренда давления (логика — в weather_calc.h)
static PressureHistory history;

bool sensorInit() {
    Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);

    if (!bmp.begin(BMP280_I2C_ADDR)) {
        Serial.println("[BMP280] Устройство не найдено!");
        Serial.printf("[BMP280] Ожидаемый адрес: 0x%02X\n", BMP280_I2C_ADDR);
        bmpInitialized = false;
        return false;
    }

    // Профиль «weather monitoring» из даташита Bosch: forced, ×1/×1, без
    // фильтра. Раньше стояли ×2/×16 и IIR ×4. Фильтр в forced-режиме хранит
    // состояние между замерами, а замеры тут раз в одну-две минуты, — так
    // что он сглаживал не шум, а саму погоду: ступенька давления доходила
    // до показаний за несколько замеров, то есть за минуты. Оверсэмплинг ×16
    // давит шум, который на минутном шаге и так ниже порога тренда
    // (±0.5 гПа/ч против ~0.03 гПа шума на ×1). Standby в forced не
    // используется, стоит для полноты вызова.
    bmp.setSampling(
        Adafruit_BMP280::MODE_FORCED,
        Adafruit_BMP280::SAMPLING_X1,
        Adafruit_BMP280::SAMPLING_X1,
        Adafruit_BMP280::FILTER_OFF,
        Adafruit_BMP280::STANDBY_MS_1
    );

    bmpInitialized = true;
    Serial.println("[BMP280] Инициализация успешна.");
    return true;
}

SensorData sensorRead() {
    SensorData data{};

    if (!bmpInitialized) {
        data.valid = false;
        return data;
    }

    if (!bmp.takeForcedMeasurement()) {
        data.valid = false;
        Serial.println("[BMP280] Измерение не завершилось!");
        return data;
    }

    float t = bmp.readTemperature();
    float p = bmp.readPressure() / 100.0f;

    if (!weatherPlausible(t, p)) {
        data.valid = false;
        Serial.println("[BMP280] Некорректные данные!");
        return data;
    }

    data.temperature = t;
    data.pressure    = p;
    data.valid       = true;

    // ── Производные (формулы — в weather_calc.h) ─────────────
    data.pressureMmHg = pressureToMmHg(p);
    data.pressureQnh  = pressureToQnh(p, t, HOME_ALTITUDE_M);
    data.airDensity   = airDensityOf(p, t);

    history.maybePush(p, millis(), PRESSURE_HISTORY_INTERVAL_MS);
    data.pressureTrend = history.trendPerHour();
    data.forecastIcon  = forecastFromTrend(data.pressureTrend, history.count);

    return data;
}
