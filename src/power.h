#pragma once
#include <stdint.h>
#include "power_calc.h"

// ============================================================
//  power.h — режимы энергосбережения.
//
//  Модуль хранит текущий режим, переключает его по заряду и
//  применяет к железу то, что относится к нему самому: радио
//  и яркость экрана. Сеть остаётся за main.cpp — он владеет
//  подключением, поэтому спрашивает powerWifiWanted(), а не
//  получает команду отсюда.
// ============================================================

void powerBegin();      // применить стартовый режим, вызвать в setup()
void powerLoop();       // авто-переключение по заряду, вызывать из loop()

PowerMode   powerCurrent();
const char* powerModeName();
bool        powerIsAuto();

void powerSetMode(PowerMode m);   // зафиксировать режим вручную
void powerSetAuto();              // вернуть автоматику по заряду

// Параметры текущего профиля — их читает main.cpp
uint32_t powerSensorIntervalMs();
bool     powerLedEnabled();
bool     powerWifiWanted();               // false — радио должно быть выключено
// Два условия гашения экрана, и спрашиваются они всегда порознь. Расписание
// требует знать час, рубеж по заряду — нет, поэтому он работает и до первой
// синхронизации NTP. И перебить подсветкой можно только расписание: рубеж по
// заряду не обходится ничем (см. applyAutoBrightness/screenSetPower).
bool     powerScreenBatteryOkNow();       // false — заряд на исходе, панель гасим
bool     powerScreenScheduleAllowsNow(int hour);

// Радио сконфигурировать под текущий профиль. Вызывать после
// подключения: до него esp_wifi_* возвращают ошибку.
void powerApplyRadio();
