#pragma once
#include <Arduino.h>

// ============================================================
//  battery.h  —  мониторинг 18650 через резистивный делитель
// ============================================================

struct BatteryData {
    float   voltage;   // В на аккумуляторе (после пересчёта делителя)
    uint8_t percent;   // 0–100 %, по кривой разряда Li-ion
    bool    low;       // ниже BATTERY_LOW_V
    bool    valid;     // false — АКБ не обнаружена (питание от USB)
};

void        batteryInit();   // вызвать в setup(): настроит АЦП и снимет первый замер
void        batteryLoop();   // вызывать из loop(): по отсчёту за раз, без задержек
BatteryData batteryRead();   // последний посчитанный результат
