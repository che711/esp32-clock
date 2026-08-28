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

    bmp.setSampling(
        Adafruit_BMP280::MODE_FORCED,
        Adafruit_BMP280::SAMPLING_X2,
        Adafruit_BMP280::SAMPLING_X16,
        Adafruit_BMP280::FILTER_X4,
        Adafruit_BMP280::STANDBY_MS_500
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

const char* forecastText(uint8_t icon) {
    switch (icon) {
        case 1: return "Ясно";
        case 2: return "Переменно";
        case 3: return "Осадки";
        default: return "Нет данных";
    }
}

const char* forecastEmoji(uint8_t icon) {
    switch (icon) {
        case 1: return "☀️";
        case 2: return "⛅";
        case 3: return "🌧️";
        default: return "❓";
    }
}
