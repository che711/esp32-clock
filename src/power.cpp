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
    if (!autoBy) return;
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
    if (m == mode) { applyProfile(); return; }
    mode = m;
    applyProfile();
}

void powerSetAuto() {
    autoBy = true;
    mode = evaluateAuto();
    applyProfile();
}

uint32_t powerSensorIntervalMs() { return powerProfile(mode).sensorMs; }
bool     powerLedEnabled()       { return powerProfile(mode).led; }
bool     powerWifiWanted()       { return powerProfile(mode).wifi; }

bool powerScreenAllowedNow(int hour) {
    // Ночное окно осталось только за экраном: сам режим от часа не зависит,
    // а подсветке разрешено гореть ровно в «не ночь» — границы те же,
    // вывернутые.
    return powerScreenAllowedAt(mode, hour,
                                POWER_NIGHT_OFF_HOUR, POWER_NIGHT_ON_HOUR,
                                battery.percent, battery.valid,
                                POWER_SCREEN_OFF_PCT);
}
