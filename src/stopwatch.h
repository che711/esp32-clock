#pragma once
#include <stdint.h>

// ============================================================
//  stopwatch.h — автомат секундомера.
//
//  Время приходит снаружи параметром: в прошивке это millis(),
//  в тестах — любые значения. Внутри ни Arduino, ни железа,
//  поэтому переходы состояний покрыты нативными тестами.
//
//  Побочные эффекты старта/паузы (радио, дисплей, лог) — в main.cpp.
// ============================================================

struct Stopwatch {
    enum State : uint8_t { IDLE = 0, RUNNING = 1, PAUSED = 2 };

    State    state   = IDLE;
    uint32_t startMs = 0;   // момент старта текущего отрезка
    uint32_t accumMs = 0;   // сумма предыдущих отрезков, мс

    // start/pause возвращают true, только если состояние реально
    // изменилось: повторное нажатие не должно сбивать отсчёт и
    // дёргать радио заново.
    bool start(uint32_t nowMs) {
        if (state == RUNNING) return false;
        startMs = nowMs;
        state   = RUNNING;
        return true;
    }

    bool pause(uint32_t nowMs) {
        if (state != RUNNING) return false;
        accumMs += nowMs - startMs;
        state    = PAUSED;
        return true;
    }

    void reset() {
        state   = IDLE;
        startMs = 0;
        accumMs = 0;
    }

    // Вычитание беззнаковое, поэтому переполнение millis() (49 суток)
    // считается верно само по себе.
    uint32_t elapsed(uint32_t nowMs) const {
        return state == RUNNING ? accumMs + (nowMs - startMs) : accumMs;
    }

    bool idle()    const { return state == IDLE; }
    bool running() const { return state == RUNNING; }
};
