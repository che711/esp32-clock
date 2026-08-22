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
//
//  Отсчёты копятся по одному за итерацию loop(), набор из
//  BATTERY_SAMPLES растянут на ~0.4 с. Медиана набора гасит
//  провалы от передачи Wi-Fi, сглаживание между наборами —
//  остаточный шум АЦП. Арифметика фильтра в battery_calc.h.
// ============================================================

#if BATTERY_ENABLED

static uint32_t   samples[BATTERY_SAMPLES];
static uint8_t    nSamples     = 0;
static uint32_t   lastSampleMs = 0;
static float      smoothV      = 0.0f;   // 0 — фильтр пуст
static float      rawV         = 0.0f;   // до проверки правдоподобия
static BatteryData latest{};

// Пересчёт набора в результат. Массив сортируется медианой на месте.
static void applySamples() {
    float vAdc = batteryMedianMv(samples, BATTERY_SAMPLES) / 1000.0f;
    float v    = vAdc * BATTERY_DIVIDER * BATTERY_CAL;
    rawV       = v;               // запоминаем до отбраковки, для диагностики

    if (!batteryVoltagePlausible(v)) {
        // Молча отбрасывать нельзя: снаружи пропадает и иконка на экране, и
        // плитка в дашборде, а причина — цифра, которой никто не видит.
        // Раз в 10 с, в ритме heartbeat: чаще незачем, реже — можно не застать.
        static uint32_t lastComplain = 0;
        uint32_t now = millis();
        if (lastComplain == 0 || now - lastComplain >= 10000) {
            lastComplain = now ? now : 1;
            Serial.printf("[Battery] %.2f В вне %.2f..%.2f — считаем, что АКБ нет\n",
                          v, BATTERY_PLAUSIBLE_MIN_V, BATTERY_PLAUSIBLE_MAX_V);
        }
        smoothV = 0.0f;                  // АКБ сняли — фильтр начать заново
        latest  = BatteryData{};
        return;
    }

    if (smoothV <= 0.0f || batteryJumped(smoothV, v, BATTERY_JUMP_V)) {
        smoothV = v;                     // первый замер или реальное событие
    } else {
        smoothV = batterySmooth(smoothV, v, BATTERY_SMOOTH);
    }

    latest.voltage = smoothV;
    latest.percent = batteryVoltageToPercent(smoothV);
    latest.low     = (smoothV <= BATTERY_LOW_V);
    latest.valid   = true;
}

void batteryInit() {
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);  // диапазон до ~3.1В

    // Первый набор — синхронно: к концу setup() значение уже должно быть.
    // Шаг мельче рабочего, чтобы не растягивать старт на полсекунды.
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        samples[i] = analogReadMilliVolts(BATTERY_ADC_PIN);
        delayMicroseconds(200);
    }
    applySamples();
    lastSampleMs = millis();

    Serial.printf("[Battery] АЦП на GPIO%d, делитель 1:%.1f\n",
                  BATTERY_ADC_PIN, BATTERY_DIVIDER);
}

void batteryLoop() {
    uint32_t now = millis();
    if (now - lastSampleMs < BATTERY_SAMPLE_GAP_MS) return;
    lastSampleMs = now;

    samples[nSamples++] = analogReadMilliVolts(BATTERY_ADC_PIN);
    if (nSamples < BATTERY_SAMPLES) return;

    nSamples = 0;
    applySamples();
}

BatteryData batteryRead()   { return latest; }
float       batteryRawVoltage() { return rawV; }

#else  // BATTERY_ENABLED == false — заглушки

void batteryInit() {}
void batteryLoop() {}
BatteryData batteryRead()   { return BatteryData{}; }
float       batteryRawVoltage() { return 0.0f; }

#endif
