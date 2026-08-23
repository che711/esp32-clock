#include "power.h"
#include "config.h"
#include "app.h"
#include "display.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ============================================================
//  power.cpp — состояние уровня и его применение к железу.
// ============================================================

// Выбор пользователя и то, что работает. Расходятся они ровно в одном случае:
// на время замера секундомер поднимает уровень до обычного, а выбор ждёт
// сброса (powerEffectiveMode). Эконом — стартовый уровень: полная мощность
// нужна, когда с часами работают, а не круглые сутки.
static PowerMode chosen = POWER_DEFAULT_MODE;
static PowerMode mode   = POWER_DEFAULT_MODE;

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
    // пакет ждёт дольше. Работает при любом modem sleep: под секундомером
    // профиль normal ставит сюда 1, и вместе с MIN_MODEM это даёт задержку
    // около десятой секунды.
    wifi_config_t cfg{};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK) {
        cfg.sta.listen_interval = p.listenInterval;
        esp_wifi_set_config(WIFI_IF_STA, &cfg);
    }
    // Пока идёт замер, сон ужат до MIN_MODEM ради отзывчивости кнопок
    // (см. setRadioSaving в main.cpp) — обратно углублять его здесь нельзя.
    if (stopwatch.idle()) esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}

// Применить профиль целиком. Радио трогаем только если оно поднято —
// иначе настройки применит main.cpp через powerApplyRadio() после связи.
static void applyProfile() {
    const PowerProfile& p = powerProfile(mode);

    // Яркость — единственный параметр, который берётся от ВЫБРАННОГО уровня,
    // а не от рабочего. Секундомер поднимает уровень до обычного ради одной
    // вещи: профиль normal не знает ночного окна, и экран перестаёт гаснуть по
    // расписанию. Полная яркость при этом ехала прицепом — и стоила дороже
    // всего остального вместе взятого: масштаб 60 % против 100 % по гамме 2.2
    // это 25 % против 78 % тока панели, то есть разница втрое на крупнейшем
    // потребителе. Читаемости она не добавляла: эконом-уровень виден прекрасно.
    displaySetAutoScale(powerProfile(chosen).contrastPct);
    applyAutoBrightness();          // пересчитать контраст под новый масштаб
    powerApplyRadio();
    Serial.printf("Power mode -> %s%s\n", p.name,
                  mode != chosen ? " (held, stopwatch)" : "");
}

void powerBegin() {
    applyProfile();
}

void powerLoop() {
    // Без троттлинга: старт секундомера должен поднимать уровень сразу, а не
    // через несколько секунд. Сама проверка — пара сравнений, профиль
    // применяется только при смене.
    PowerMode want = powerEffectiveMode(chosen, !stopwatch.idle());
    if (want == mode) return;
    mode = want;
    applyProfile();
}

PowerMode   powerCurrent()    { return mode; }
const char* powerModeName()   { return powerProfile(mode).name; }
PowerMode   powerChosenMode() { return chosen; }
bool        powerIsHeld()     { return mode != chosen; }

void powerSetMode(PowerMode m) {
    chosen = m;

    // Команда принимается всегда, но пока идёт замер — откладывается: гасить
    // экран и ужимать радио посреди отсчёта нельзя. Отказывать при этом
    // незачем, иначе кнопка выглядела бы залипшей; выбор лежит в chosen и
    // вступит в силу со сбросом секундомера.
    mode = powerEffectiveMode(chosen, !stopwatch.idle());
    if (mode != chosen)
        Serial.printf("Power: %s queued, held at normal until stopwatch reset\n",
                      powerProfile(chosen).name);

    applyProfile();
}

uint32_t powerSensorIntervalMs() { return powerProfile(mode).sensorMs; }
bool     powerLedEnabled()       { return powerProfile(mode).led; }

bool powerScreenBatteryOkNow() {
    return powerScreenBatteryOk(battery.percent, battery.valid,
                                POWER_SCREEN_OFF_PCT);
}

bool powerScreenScheduleAllowsNow(int hour) {
    // Ночное окно осталось только за экраном: сам уровень от часа не зависит,
    // а подсветке разрешено гореть ровно в «не ночь» — границы те же,
    // вывернутые.
    return powerScreenAllowed(mode, hour,
                              POWER_NIGHT_OFF_HOUR, POWER_NIGHT_ON_HOUR);
}
