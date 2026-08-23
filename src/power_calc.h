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

// Тот же ответ, но для уровня, выбранного руками. powerAutoMode правило
// «идёт замер — обычный режим» уже знает, а ручная фиксация раньше его
// обходила: зафиксированный эконом ночью держал экран погашенным, и
// секундомер шёл вслепую — при том, что запускали его именно чтобы смотреть.
// Выбор пользователя при этом не теряется: он ждёт сброса секундомера.
inline PowerMode powerEffectiveMode(PowerMode chosen, bool stopwatchActive) {
    return stopwatchActive ? POWER_NORMAL : chosen;
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
