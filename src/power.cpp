#include "power.h"
#include "config.h"
#include "app.h"
#include "display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ============================================================
//  power.cpp — состояние режима и его применение к железу.
// ============================================================

static PowerMode mode   = POWER_NORMAL;
static bool      autoBy = POWER_AUTO_DEFAULT;

// Срок ручной фиксации выживания; 0 — фиксации нет. Только у этого режима есть
// срок: он один выключает радио, а значит и путь обратно (см. config.h).
// Сравнение со знаковой разностью, поэтому переполнение millis() безопасно.
static uint32_t  manualSurvivalUntil = 0;

// Мощность передатчика задаётся перечислением, не числом: округляем
// вниз до ближайшего доступного шага, чтобы профиль оставался просто
// числом в dBm и не тянул за собой заголовки Arduino.
static wifi_power_t txPowerFor(int8_t dbm) {
    if (dbm >= 19) return WIFI_POWER_19_5dBm;
    if (dbm >= 17) return WIFI_POWER_17dBm;
    if (dbm >= 15) return WIFI_POWER_15dBm;
    if (dbm >= 13) return WIFI_POWER_13dBm;
    if (dbm >= 11) return WIFI_POWER_11dBm;
    if (dbm >=  8) return WIFI_POWER_8_5dBm;
    return WIFI_POWER_7dBm;
}

void powerApplyRadio() {
    if (WiFi.status() != WL_CONNECTED) return;
    const PowerProfile& p = powerProfile(mode);

    WiFi.setTxPower(txPowerFor(p.txDbm));

    // listen_interval — сколько маячков радио пропускает между
    // пробуждениями. Больше интервал — холоднее радио, но входящий
    // пакет ждёт дольше. Работает только вместе с MAX_MODEM.
    wifi_config_t cfg{};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
        cfg.sta.listen_interval = p.listenInterval;
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    // Пока идёт замер секундомера, сон радио выключен ради точности отсчёта
    // (см. setRadioSaving в main.cpp) — обратно его здесь не включаем.
    if (stopwatch.idle()) esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

// Применить профиль целиком. Радио трогаем только если оно поднято —
// иначе настройки применит main.cpp через powerApplyRadio() после связи.
static void applyProfile() {
    const PowerProfile& p = powerProfile(mode);
    displaySetAutoScale(p.contrastPct);
    applyAutoBrightness();          // пересчитать контраст под новый масштаб
    powerApplyRadio();
    Serial.printf("Power mode -> %s (%s)\n", p.name, autoBy ? "auto" : "manual");
}

void powerBegin() {
    applyProfile();
}

// Решение автоматики на текущий момент. Время суток здесь не участвует:
// ночью экран гасит расписание эконома, режим от часа не зависит.
static PowerMode evaluateAuto() {
    // Пауза секундомера — тоже работа: замер не окончен.
    return powerAutoMode(mode, !stopwatch.idle(),
                         battery.percent, battery.valid,
                         POWER_SURVIVAL_PCT, POWER_HYSTERESIS_PCT);
}

void powerLoop() {
    if (!autoBy) {
        if (manualSurvivalUntil == 0) return;
        if ((int32_t)(millis() - manualSurvivalUntil) < 0) return;
        Serial.println("Power: manual survival expired -> auto");
        powerSetAuto();          // он же снимет срок и поднимет радио обратно
        return;
    }
    // Без троттлинга: старт секундомера должен поднимать режим сразу, а не
    // через несколько секунд. Сама проверка — пара сравнений, профиль
    // применяется только при смене режима.
    PowerMode next = evaluateAuto();
    if (next == mode) return;
    mode = next;
    applyProfile();
}

PowerMode   powerCurrent()  { return mode; }
const char* powerModeName() { return powerProfile(mode).name; }
bool        powerIsAuto()   { return autoBy; }

void powerSetMode(PowerMode m) {
    autoBy = false;
    mode   = m;

    // Выживание фиксируем со сроком: радио в нём выключено, и снять фиксацию
    // из дашборда потом уже нечем — вернуть автоматику должны мы сами.
    if (m == POWER_SURVIVAL) {
        manualSurvivalUntil = millis() + POWER_MANUAL_SURVIVAL_MS;
        if (manualSurvivalUntil == 0) manualSurvivalUntil = 1;  // 0 занят
        Serial.printf("Power: survival locked for %lu min, then back to auto\n",
                      (unsigned long)(POWER_MANUAL_SURVIVAL_MS / 60000UL));
    } else {
        manualSurvivalUntil = 0;
    }

    applyProfile();
}

void powerSetAuto() {
    autoBy = true;
    manualSurvivalUntil = 0;
    mode = evaluateAuto();
    applyProfile();
}

uint32_t powerSensorIntervalMs() { return powerProfile(mode).sensorMs; }
bool     powerLedEnabled()       { return powerProfile(mode).led; }
bool     powerWifiWanted()       { return powerProfile(mode).wifi; }

bool powerScreenBatteryOkNow() {
    return powerScreenBatteryOk(battery.percent, battery.valid,
                                POWER_SCREEN_OFF_PCT);
}

bool powerScreenScheduleAllowsNow(int hour) {
    // Ночное окно осталось только за экраном: сам режим от часа не зависит,
    // а подсветке разрешено гореть ровно в «не ночь» — границы те же,
    // вывернутые.
    return powerScreenAllowed(mode, hour,
                              POWER_NIGHT_OFF_HOUR, POWER_NIGHT_ON_HOUR);
}
