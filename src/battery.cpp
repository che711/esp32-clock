#include "battery.h"
#include "config.h"

// ============================================================
//  battery.cpp
//
//  Схема: BAT+ ──[100к]──●──[100к]── GND, точка ● → GPIO2.
//  Максимум на АЦП: 4.2В / 2 = 2.1В — с запасом в диапазоне
//  аттенюации 12 dB (~0…3.1В на ESP32-C6).
//
//  Используем analogReadMilliVolts() — заводская калибровка
//  АЦП (eFuse), точность заметно лучше сырого analogRead().
// ============================================================

#if BATTERY_ENABLED

// Кривая разряда Li-ion 18650 под малой нагрузкой (V → %)
static const struct { float v; uint8_t pct; } CURVE[] = {
    {4.20f, 100}, {4.06f, 90}, {3.98f, 80}, {3.92f, 70},
    {3.87f, 60},  {3.82f, 50}, {3.79f, 40}, {3.77f, 30},
    {3.74f, 20},  {3.68f, 10}, {3.45f, 5},  {3.00f, 0},
};
static const size_t CURVE_N = sizeof(CURVE) / sizeof(CURVE[0]);

static uint8_t voltageToPercent(float v) {
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

void batteryInit() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);  // диапазон до ~3.1В
    Serial.printf("[Battery] АЦП на GPIO%d, делитель 1:%.1f\n",
                  BATTERY_ADC_PIN, BATTERY_DIVIDER);
}

BatteryData batteryRead() {
    BatteryData b{};

    // Усреднение: гасим шум АЦП и пульсации
    uint32_t mvSum = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        mvSum += analogReadMilliVolts(BATTERY_ADC_PIN);
    }
    float vAdc = (mvSum / (float)BATTERY_SAMPLES) / 1000.0f;
    float v    = vAdc * BATTERY_DIVIDER * BATTERY_CAL;

    // Вне диапазона Li-ion → скорее всего АКБ не подключена
    // (вход висит в воздухе или питание чисто от USB)
    if (v < 2.80f || v > 4.35f) {
        b.valid = false;
        return b;
    }

    b.voltage = v;
    b.percent = voltageToPercent(v);
    b.low     = (v <= BATTERY_LOW_V);
    b.valid   = true;
    return b;
}

#else  // BATTERY_ENABLED == false — заглушки

void batteryInit() {}
BatteryData batteryRead() { return BatteryData{}; }

#endif
