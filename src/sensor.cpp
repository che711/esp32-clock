#include "sensor.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>

// ============================================================
//  sensor.cpp
// ============================================================

static Adafruit_BMP280 bmp;
static bool _initialized = false;

// Кольцевой буфер для тренда давления
static float    _pressureHistory[PRESSURE_HISTORY_SIZE] = {};
static uint32_t _historyTime[PRESSURE_HISTORY_SIZE]     = {};
static uint8_t  _historyIdx   = 0;
static uint8_t  _historyCount = 0;
static uint32_t _lastHistoryMs = 0;

bool sensorInit() {
    Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);

    if (!bmp.begin(BMP280_I2C_ADDR)) {
        Serial.println("[BMP280] Устройство не найдено!");
        Serial.printf("[BMP280] Ожидаемый адрес: 0x%02X\n", BMP280_I2C_ADDR);
        _initialized = false;
        return false;
    }

    bmp.setSampling(
        Adafruit_BMP280::MODE_FORCED,
        Adafruit_BMP280::SAMPLING_X2,
        Adafruit_BMP280::SAMPLING_X16,
        Adafruit_BMP280::FILTER_X4,
        Adafruit_BMP280::STANDBY_MS_500
    );

    _initialized = true;
    Serial.println("[BMP280] Инициализация успешна.");
    return true;
}

// Расчёт тренда давления (гПа/ч) по истории
static float calcTrend() {
    if (_historyCount < 2) return 0.0f;

    // _historyIdx указывает на СЛЕДУЮЩИЙ слот записи, поэтому:
    //   newest = idx - 1
    //   oldest = idx - count        (было idx - count + 1 → off-by-one,
    //                                самая старая точка выпадала из окна)
    uint8_t newest = (_historyIdx + PRESSURE_HISTORY_SIZE - 1) % PRESSURE_HISTORY_SIZE;
    uint8_t oldest = (_historyIdx + PRESSURE_HISTORY_SIZE - _historyCount) % PRESSURE_HISTORY_SIZE;

    float    dp = _pressureHistory[newest] - _pressureHistory[oldest];
    uint32_t dt = _historyTime[newest] - _historyTime[oldest];  // мс (беззнаковое вычитание корректно при переполнении millis)

    if (dt < 1000) return 0.0f;
    return dp / (dt / 3600000.0f);   // гПа/ч
}

// Прогноз по тренду (упрощённый метод Бьюри)
static uint8_t calcForecast(float trendPerHour) {
    if (_historyCount < 3)    return 0;   // недостаточно данных (≥ 10 мин истории)
    if (trendPerHour >  0.5f) return 1;   // ясно
    if (trendPerHour < -0.5f) return 3;   // дождь
    return 2;                             // переменно
}

SensorData sensorRead() {
    SensorData data{};

    if (!_initialized) {
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
    float a = bmp.readAltitude(SEA_LEVEL_HPA);

    if (isnan(t) || isnan(p) || p < 300.0f || p > 1200.0f) {
        data.valid = false;
        Serial.println("[BMP280] Некорректные данные!");
        return data;
    }

    data.temperature = t;
    data.pressure    = p;
    data.altitude    = a;
    data.valid       = true;

    // ── Производные ──────────────────────────────────────────
    // 1. мм.рт.ст.
    data.pressureMmHg = p * 0.75006f;

    // 2. QNH — приведённое давление к уровню моря
    //    P0 = P * (1 - 0.0065*h / (T + 0.0065*h + 273.15))^(-5.257)
    float h = a;
    data.pressureQnh = p * powf(1.0f - 0.0065f * h / (t + 0.0065f * h + 273.15f), -5.257f);

    // 3. Плотность воздуха (сухой воздух): ρ = P / (R * T)
    //    R_specific = 287.05 Дж/(кг·К)
    data.airDensity = (p * 100.0f) / (287.05f * (t + 273.15f));

    // 4. История и тренд
    //    Точка пишется раз в PRESSURE_HISTORY_INTERVAL_MS (5 мин),
    //    а не при каждом опросе. Иначе окно тренда = 6 × 10 с = 1 минута,
    //    и порог ±0.5 гПа/ч ловил бы чистый шум сенсора.
    uint32_t now = millis();
    if (_historyCount == 0 || now - _lastHistoryMs >= PRESSURE_HISTORY_INTERVAL_MS) {
        _lastHistoryMs = now;
        _pressureHistory[_historyIdx] = p;
        _historyTime[_historyIdx]     = now;
        _historyIdx = (_historyIdx + 1) % PRESSURE_HISTORY_SIZE;
        if (_historyCount < PRESSURE_HISTORY_SIZE) _historyCount++;
    }

    data.pressureTrend = calcTrend();
    data.forecastIcon  = calcForecast(data.pressureTrend);

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
