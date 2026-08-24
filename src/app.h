#pragma once
#include <Arduino.h>
#include "sensor.h"
#include "battery.h"
#include "stopwatch.h"
#include "history.h"

// ============================================================
//  app.h — состояние прошивки, общее для модулей.
//
//  Всё перечисленное определено в main.cpp: он владеет циклом
//  и обновляет эти данные. display.cpp и web_api.cpp их только
//  читают, а команды секундомера дёргают через функции ниже,
//  потому что помимо автомата те трогают радио и экран.
// ============================================================

extern SensorData  weather;      // последнее чтение BMP280
extern BatteryData battery;
extern Stopwatch   stopwatch;

// История показаний для графиков дашборда: точка на каждое чтение датчика,
// отдаётся в /api/history. Дашборд поднимает её при подключении — иначе
// свежеоткрытая вкладка рисовала бы тренд по своим первым пяти минутам.
extern TrendHistory trendHistory;

// Буферы времени, обновляются в loop() (см. updateTimeStrings)
extern char timeBuf[9];          // "HH:MM:SS"
extern char dateBuf[20];         // "10 MAY 2026"
extern char dayShortBuf[5];      // "SUN"
extern char dayFullBuf[12];      // "Sunday"
extern bool   timeSynced;
extern String localIP;

float dieTempC();                // температура кристалла, кэш на 10 с

// Причина последнего сброса, снята в setup(). Штатные сбросы от аварийных
// отделены здесь же: дашборду нужно решить, info это или warn.
const char* resetReasonName();
bool        resetWasAbnormal();

void swStart();
void swPause();
void swReset();

void applyAutoBrightness();      // уровень по текущему часу, если режим авто
void screenSetPower(bool on);    // питание экрана из UI; ночью — на POWER_SCREEN_PEEK_MS
uint32_t screenPeekLeftS();      // секунд до авто-гашения ночью, 0 — не горит по подсветке
