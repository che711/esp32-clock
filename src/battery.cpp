#include "battery.h"
#include "config.h"
#include "battery_calc.h"

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

    if (!batteryVoltagePlausible(v)) {
        b.valid = false;
        return b;
    }

    b.voltage = v;
    b.percent = batteryVoltageToPercent(v);
    b.low     = (v <= BATTERY_LOW_V);
    b.valid   = true;
    return b;
}

#else  // BATTERY_ENABLED == false — заглушки

void batteryInit() {}
BatteryData batteryRead() { return BatteryData{}; }

#endif
