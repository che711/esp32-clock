#pragma once
#include <stdint.h>
#include <string.h>

// ============================================================
//  power_calc.h — выбор режима энергосбережения и профили.
//
//  Без Arduino: собирается нативно и покрыто тестами.
//  Применение режима к железу — в power.cpp.
//
//  Три уровня. Эконом — режим по умолчанию: он ужимает то, что
//  не видно на глаз (реже опрос датчика, тусклее экран, радио
//  спит дольше между маячками), и ночью гасит экран по расписанию.
//  Обычный включается, когда с устройством работают, — то есть
//  на время секундомера. Выживание выключает Wi-Fi совсем и
//  включается только на исходе заряда. Часы при этом идут, время
//  поправляется коротким выходом в сеть раз в сутки.
// ============================================================

enum PowerMode : uint8_t {
    POWER_NORMAL   = 0,
    POWER_ECO      = 1,
    POWER_SURVIVAL = 2,
};

struct PowerProfile {
    const char* name;
    uint32_t    sensorMs;        // период опроса BMP280 (батарея обновляется чаще)
    uint8_t     contrastPct;     // масштаб авто-яркости, % от штатной
    bool        wifi;            // радио включено постоянно
    bool        led;             // мигок WS2812 на каждый опрос
    bool        screenWindow;    // гасить экран вне рабочего окна
    uint8_t     listenInterval;  // маячков между пробуждениями радио
    int8_t      txDbm;           // мощность передатчика
};

inline const PowerProfile& powerProfile(PowerMode m) {
    static const PowerProfile P[3] = {
        //  name        sensorMs  contrast  wifi   led    window  listen  tx
        { "normal",     60000UL,      100,  true,  true,  false,      1,  20 },
        { "eco",       120000UL,       60,  true,  false, true,       5,  11 },
        { "survival",  300000UL,       35,  false, false, true,       5,  11 },
    };
    return P[m > POWER_SURVIVAL ? POWER_NORMAL : m];
}

// Попадает ли час в окно [onHour, offHour). Окно через полночь поддержано,
// совпадающие границы означают «круглые сутки».
inline bool hourInWindow(int hour, int onHour, int offHour) {
    if (onHour == offHour) return true;
    if (onHour <  offHour) return hour >= onHour && hour < offHour;
    return hour >= onHour || hour < offHour;
}

// Автоматический выбор режима. Правила по убыванию приоритета:
//
//   1. Секундомер начат — обычный режим. С устройством прямо сейчас работают:
//      нужны и точный отсчёт, и живая связь с браузером. Пауза считается
//      работой — замер не окончен, и ронять связь посреди него незачем.
//   2. Заряд на исходе — выживание, в какое бы время суток это ни случилось.
//   3. Всё остальное — эконом. Он и есть режим по умолчанию: полная мощность
//      нужна только когда с часами работают, а не круглые сутки.
//
// Время суток на выбор режима не влияет намеренно. Ночью экономить нечего
// сверх того, что уже делает эконом: экран он гасит по расписанию сам
// (см. powerScreenAllowed), а гасить ещё и радио значило бы, что ночью
// устройства нет ни в сети, ни под рукой — секундомер не запустить, дашборд
// не открыть, — ради выигрыша в единицы миллиампер.
inline PowerMode powerAutoMode(PowerMode cur, bool stopwatchActive,
                               uint8_t pct, bool valid,
                               uint8_t survPct, uint8_t hyst) {
    if (stopwatchActive) return POWER_NORMAL;

    if (valid) {
        if (pct <= survPct) return POWER_SURVIVAL;
        // Вверх — только с запасом: сглаженное напряжение всё равно гуляет
        // на единицы милливольт, а в средней части кривой это целые проценты.
        if (cur == POWER_SURVIVAL && pct < (uint16_t)survPct + hyst)
            return POWER_SURVIVAL;
    }

    return POWER_ECO;
}

// Почему рабочий уровень разошёлся с выбранным. Наружу это идёт строкой:
// без причины несовпадение выглядит как незалипшая кнопка.
enum PowerHold : uint8_t {
    POWER_HOLD_NONE      = 0,
    POWER_HOLD_STOPWATCH = 1,   // идёт замер — держим обычный
    POWER_HOLD_LOW       = 2,   // заряд на исходе — держим выживание
    POWER_HOLD_CHARGED   = 3,   // банка поднялась — выпускаем из выживания
};

inline const char* powerHoldName(PowerHold h) {
    switch (h) {
        case POWER_HOLD_STOPWATCH: return "stopwatch";
        case POWER_HOLD_LOW:       return "low";
        case POWER_HOLD_CHARGED:   return "charged";
        default:                   return "";
    }
}

// Рабочий уровень при ручной фиксации: выбор пользователя, поправленный двумя
// обстоятельствами, которые сильнее предпочтения.
//
//   1. Секундомер. Замер запускают, чтобы на него смотреть, а зафиксированный
//      эконом ночью держал бы экран погашенным. Порядок тот же, что у
//      powerAutoMode: замер идёт первым, даже поверх низкого заряда.
//   2. Заряд, и это право вето в обе стороны.
//
//      Вниз: зафиксированный обычный режим на исходе банки жёг радио до
//      brownout — ручная фиксация не смотрела на заряд вовсе. Выбор уровня —
//      это предпочтение, а не разрешение игнорировать банку.
//
//      Вверх, и это важнее. Выживание выключает радио, то есть уносит с собой
//      дашборд — единственный способ отдать команду. Пока выходом был только
//      срок ручной фиксации, полностью заряженная банка ничего не меняла:
//      устройство сидело в выживании час, хотя причина исчезла. Теперь заряд
//      выше порога с запасом выпускает из выживания независимо от того, кто
//      его включил. Выживание определяется зарядом, а не вкусом: если хочется
//      тихого устройства на полной банке — это эконом, и он фиксируется
//      бессрочно.
//
// Сам выбор при этом не теряется: он остаётся в chosen и возвращается, как
// только обстоятельство отпадёт.
//
// cur — рабочий уровень прямо сейчас, и он здесь ровно за тем же, зачем в
// powerAutoMode: за гистерезис. Привязывать запас к chosen нельзя — тогда
// зафиксированный обычный режим, опущенный в выживание на 15 %, возвращался бы
// наверх уже на 16-ти, и на дрейфе сглаженного напряжения в единицы милливольт
// радио включалось бы и выключалось по кругу. Ровно от этого дребезга запас
// и заведён.
inline PowerMode powerManualMode(PowerMode cur, PowerMode chosen,
                                 bool stopwatchActive,
                                 uint8_t pct, bool valid,
                                 uint8_t survPct, uint8_t hyst,
                                 PowerHold* why = 0) {
    PowerMode m = chosen;

    if (stopwatchActive) {
        m = POWER_NORMAL;
    } else if (valid) {
        if (pct <= survPct)
            m = POWER_SURVIVAL;                       // вниз — сразу по порогу
        else if (cur == POWER_SURVIVAL && pct < (uint16_t)survPct + hyst)
            m = POWER_SURVIVAL;                       // вверх — только с запасом
        else if (chosen == POWER_SURVIVAL)
            m = POWER_ECO;                            // выпущено зарядом
        else
            m = chosen;
    }

    if (why) {
        if      (m == chosen)     *why = POWER_HOLD_NONE;
        else if (stopwatchActive) *why = POWER_HOLD_STOPWATCH;
        else if (m == POWER_SURVIVAL) *why = POWER_HOLD_LOW;
        else                      *why = POWER_HOLD_CHARGED;
    }
    return m;
}

// Можно ли держать экран включённым. В обычном режиме — всегда.
inline bool powerScreenAllowed(PowerMode m, int hour, int onHour, int offHour) {
    if (!powerProfile(m).screenWindow) return true;
    return hourInWindow(hour, onHour, offHour);
}

// Последний рубеж: когда заряда почти не осталось, панель гасится независимо
// от режима и расписания. Она — самый крупный потребитель, и её выключение
// и продлевает работу, и снимает просадку напряжения, из-за которой
// устройство ушло бы в brownout раньше времени.
//
// Вынесено отдельно от расписания намеренно: расписание требует знать час, а
// этот рубеж — нет. Пока оба жили в одной функции, вызывающая сторона не могла
// проверить заряд, не имея времени, и без синхронизации NTP гашение по заряду
// не работало вовсе.
inline bool powerScreenBatteryOk(uint8_t pct, bool valid, uint8_t offPct) {
    return !(valid && pct <= offPct);
}

// Оба условия сразу — для случая, когда час известен.
inline bool powerScreenAllowedAt(PowerMode m, int hour, int onHour, int offHour,
                                 uint8_t pct, bool valid, uint8_t offPct) {
    if (!powerScreenBatteryOk(pct, valid, offPct)) return false;
    return powerScreenAllowed(m, hour, onHour, offHour);
}

// Разбор имени режима для API: "normal" | "eco" | "survival".
inline bool powerModeFromName(const char* s, PowerMode* out) {
    if (!s || !out) return false;
    for (uint8_t m = POWER_NORMAL; m <= POWER_SURVIVAL; m++) {
        if (strcmp(s, powerProfile((PowerMode)m).name) == 0) {
            *out = (PowerMode)m;
            return true;
        }
    }
    return false;
}
