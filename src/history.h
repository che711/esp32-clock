#pragma once
#include <stdint.h>
#include <math.h>

// ============================================================
//  history.h — кольцо показаний для графиков дашборда.
//
//  Без Arduino и без обращений к железу: время приходит
//  параметром, память — статический массив. Собирается нативно
//  и покрыто тестами (test/test_native).
//
//  Зачем оно вообще. Снимок в WebSocket уходит раз в секунду, но
//  BMP280 опрашивается раз в минуту (powerSensorIntervalMs), и
//  60 подряд идущих кадров несут ОДНО И ТО ЖЕ показание. Браузер,
//  копивший историю из этих кадров, рисовал не кривую, а лестницу
//  из пяти ступеней шириной в минуту каждая. Точка сюда кладётся
//  по факту чтения датчика, поэтому здесь их ровно столько,
//  сколько было измерений.
// ============================================================

// 360 точек: в normal (чтение раз в минуту) это 6 часов, в eco
// (раз в две) — 12. Точка занимает 20 байт, всё кольцо — 7 КБ.
#define TREND_HISTORY_SIZE 360

struct TrendSample {
    uint32_t stampMs;   // millis() момента чтения
    float    temp;      // °C    | NAN во всех четырёх, если датчик
    float    press;     // гПа   | не ответил: дырка в графике честнее
    float    alt;       // м     | склейки соседних точек прямой
    float    trend;     // гПа/ч |
};

struct TrendHistory {
    TrendSample pts[TREND_HISTORY_SIZE] = {};
    uint16_t    idx   = 0;   // слот СЛЕДУЮЩЕЙ записи
    uint16_t    count = 0;

    void push(uint32_t nowMs, bool valid,
              float t, float p, float a, float tr) {
        TrendSample& s = pts[idx];
        s.stampMs = nowMs;
        s.temp    = valid ? t  : NAN;
        s.press   = valid ? p  : NAN;
        s.alt     = valid ? a  : NAN;
        s.trend   = valid ? tr : NAN;
        idx = (idx + 1) % TREND_HISTORY_SIZE;
        if (count < TREND_HISTORY_SIZE) count++;
    }

    uint16_t size() const { return count; }

    // i = 0 — самая старая точка, i = size()-1 — свежая. idx смотрит на
    // СЛЕДУЮЩИЙ слот, поэтому отсчёт ведём от idx - count (та же арифметика,
    // на которой в PressureHistory уже ловили off-by-one).
    const TrendSample& at(uint16_t i) const {
        return pts[(idx + TREND_HISTORY_SIZE - count + i) % TREND_HISTORY_SIZE];
    }

    // Возраст точки в секундах. Отдаём именно возраст, а не время: часы
    // устройства зависят от NTP, а разность millis() верна всегда — в том
    // числе после переполнения (беззнаковое вычитание).
    uint32_t ageS(uint16_t i, uint32_t nowMs) const {
        return (nowMs - at(i).stampMs) / 1000UL;
    }

    void clear() { idx = 0; count = 0; }
};
