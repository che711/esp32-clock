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

// Поиск датчика и настройка. Молча: говорить, нашёлся ли он, — дело
// вызывающих, у первого старта и у повторной попытки слова разные.
static bool bmpBegin() {
    Wire.begin(BMP280_SDA_PIN, BMP280_SCL_PIN);   // повторный вызов безвреден
    if (!bmp.begin(BMP280_I2C_ADDR)) return false;

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
    return true;
}

bool sensorInit() {
    bmpInitialized = bmpBegin();
    if (!bmpInitialized) {
        Serial.println("[BMP280] Устройство не найдено!");
        Serial.printf("[BMP280] Ожидаемый адрес: 0x%02X\n", BMP280_I2C_ADDR);
        return false;
    }
    Serial.println("[BMP280] Инициализация успешна.");
    return true;
}

SensorData sensorRead(float tempOffsetC) {
    SensorData data{};

    // Датчика нет — ищем его заново на каждом плановом замере. Раньше
    // bmpInitialized ставился один раз в setup(), и датчик, не ответивший на
    // старте (контакт, питание модуля поднялось позже) или отвалившийся на
    // ходу, пропадал до перезагрузки: «no sensor» на экране при исправном
    // BMP280. Попытка стоит одну транзакцию I²C раз в минуту-две, а дашборд
    // возврат уже умеет показать («BMP280 sensor online»).
    if (!bmpInitialized) {
        bmpInitialized = bmpBegin();
        if (!bmpInitialized) {
            data.valid = false;
            return data;
        }
        Serial.println("[BMP280] Датчик снова на связи.");
    }

    // Сбой замера — повод начать с поиска: датчик, переживший провал питания,
    // встаёт со сброшенными регистрами, и bmp.begin() заново читает и его
    // калибровку. Следующий плановый замер так и сделает.
    if (!bmp.takeForcedMeasurement()) {
        data.valid = false;
        bmpInitialized = false;
        Serial.println("[BMP280] Измерение не завершилось!");
        return data;
    }

    // Поправку вносим до QNH и плотности: они считаются от температуры
    // воздуха, а не подогретого корпуса. Давление она не трогает —
    // компенсацию библиотека ведёт от t_fine, температуры самого кристалла,
    // по которой датчик калибровали на заводе.
    float t = bmp.readTemperature() + tempOffsetC;
    float p = bmp.readPressure() / 100.0f;

    // Мусор вместо чисел — тот же случай: отвалившийся датчик отвечает
    // единицами на шине, и из них выходит давление вне всякого диапазона.
    if (!weatherPlausible(t, p)) {
        data.valid = false;
        bmpInitialized = false;
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

    history.maybePush(p, millis(),
                      PRESSURE_HISTORY_INTERVAL_MS - PRESSURE_HISTORY_SLACK_MS);
    data.pressureTrend = history.trendPerHour();
    data.forecastIcon  = forecastFromTrend(data.pressureTrend, history.count);

    return data;
}
