#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "app.h"
#include "clock_utils.h"
#include "display.h"
#include "web_api.h"
#include "sensor.h"
#include "battery.h"
#include "power.h"
#include "net.h"
#include "mqtt.h"
#include "schedule_calc.h"
#include "screen_calc.h"

// ============================================================
//  main.cpp — жизненный цикл устройства: время, датчики, экран
//  по расписанию и главный цикл. Экран — в display.cpp, веб —
//  в web_api.cpp, сеть — в net.cpp.
// ============================================================

// ── Общее состояние (объявлено в app.h) ──────────────────
SensorData  weather{};
BatteryData battery{};
Stopwatch   stopwatch;
TrendHistory trendHistory;

char   timeBuf[9];
char   dateBuf[20];
char   dayShortBuf[5];
char   dayFullBuf[12];
bool   timeSynced = false;
String localIP    = "";

// Замок состояния (app.h). Создаётся первым делом в setup() — до того, как
// поднимется веб-сервер, которому он нужен.
static SemaphoreHandle_t appMutex = nullptr;
void appLock()   { xSemaphoreTake(appMutex, portMAX_DELAY); }
void appUnlock() { xSemaphoreGive(appMutex); }

// Будильник паузы в конце loop(). В простое цикл спит до смены секунды
// (LOOP_IDLE_MS в config.h), и команда секундомера, пришедшая посреди этой
// паузы, увидела бы экран только на следующей секунде: старт — с опозданием
// до секунды, маркер паузы — тоже. Поэтому команды будят цикл сами.
// Семафор, а не уведомление задачи: слот уведомлений у loopTask один, и его
// может ждать какой-нибудь вызов ядра — наш сигнал оборвал бы чужое ожидание.
static SemaphoreHandle_t loopWake = nullptr;
static void wakeLoop() { if (loopWake) xSemaphoreGive(loopWake); }

// Поправка на тепло панели (BMP280_SCREEN_HEAT_C в config.h). Берётся по
// состоянию экрана в момент замера. HAS_DISPLAY проверяется отдельно: без
// подпаянной панели displayIsOn() всё равно отвечает «горит», а греть
// датчик нечему.
static float sensorTempOffset() {
    return (HAS_DISPLAY && displayIsOn()) ? -BMP280_SCREEN_HEAT_C : 0.0f;
}

// Точка истории кладётся ровно там, где датчик прочитан, — и только там.
// Копить её из broadcast-кадров нельзя: те уходят раз в секунду и минуту
// подряд несут одно и то же число, из чего график получался ступенчатым.
static void historyPush(uint32_t nowMs) {
    // Напряжение банки едет в той же точке: это единственный журнал разряда,
    // который переживает закрытую вкладку, и из него же берётся кривая для
    // замера реальной ёмкости окна (BATTERY_USABLE_MAH в config.h).
    trendHistory.push(nowMs, weather.valid, weather.temperature,
                      weather.pressure, weather.pressureTrend,
                      battery.valid ? battery.voltage : NAN);
}

// Когда в следующий раз мерить (schedule_calc.h)
static SensorClock sensorClock;

// Взводится, когда кадр надо перерисовать не дожидаясь смены секунды
static bool     forceRedraw  = false;

static const char* DAYS_SHORT[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
static const char* DAYS_FULL[]  = { "Sunday","Monday","Tuesday","Wednesday",
                                    "Thursday","Friday","Saturday" };
static const char* MONTHS[]     = { "JAN","FEB","MAR","APR","MAY","JUN",
                                    "JUL","AUG","SEP","OCT","NOV","DEC" };

// Локальное время «прямо сейчас», без ожидания.
// Своя реализация вместо getLocalTime(&t, 0): та отмеряет таймаут как
// while (millis() - start <= ms) и при ms == 0 возвращает false, ни разу
// не прочитав часы, если между двумя millis() успел смениться тик, —
// а вытеснение loop-задачи планировщиком делает это регулярно. Экран
// в такие моменты на секунду показывал прочерки при исправных часах.
static bool localTimeNow(struct tm* t, time_t now) {
    localtime_r(&now, t);
    return t->tm_year > (2016 - 1900);   // тот же критерий «время задано»
}

static bool localTimeNow(struct tm* t) {
    return localTimeNow(t, time(nullptr));
}

// WS2812 на GPIO8: pinMode, пока нога отдана RMT, НЕ вызывать — сломает RMT
// адресного LED. После rmtDeinit() — можно, см. ledColor().
//
// Что горит прямо сейчас, помним сами: прочитать состояние адресного диода
// нечем, а гасить его вслепую на каждом обороте loop() — это лишняя посылка
// по RMT там, где гасить обычно нечего.
static bool ledOn = false;

static void ledColor(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(LED_PIN, r, g, b);
#if CONFIG_PM_ENABLE
    // Канал RMT после записи отпускаем. Ядро включает его на первом
    // rgbLedWrite() и больше не выключает, а включённый канал держит PM-замок
    // ESP_PM_CPU_FREQ_MAX (esp_driver_rmt, rmt_common.c). Light sleep тогда
    // не наступал бы никогда — с первой же вспышки синего в setup(), и весь
    // PM молча стоял бы без дела.
    //
    // Цвет WS2812 держит сам. Ногу вместо RMT держим низким уровнем: в воздухе
    // на ней ловились бы помехи, которые диод прочтёт как данные. Следующий
    // rgbLedWrite() заберёт её у GPIO сам — rmtInit() начинает с отвязки.
    // Цена — заново заводить канал на каждую запись, а записей здесь
    // единицы в минуту.
    rmtDeinit(LED_PIN);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
#endif
    ledOn = (r || g || b);
}

// Температура кристалла: читаем не чаще раза в 10 с (temperatureRead
// на C6 может блокировать на десятки мс — незачем дёргать каждую секунду).
//
// NAN в кэш не пускаем. Если temperatureRead() вернёт его (ошибка драйвера
// датчика), число уйдёт в снимок как `nan` — это не JSON, и дашборд отбросит
// каждый кадр до следующего чтения через 10 с — замрут все плитки, а не одна.
// Прошлое значение тут почти не врёт: кристалл за десять секунд не остывает.
float dieTempC() {
    static float    cached = 0.0f;
    static uint32_t last   = 0;
    uint32_t now = millis();
    if (last == 0 || now - last >= 10000) {
        last = now ? now : 1;
        float t = temperatureRead();
        if (!isnan(t)) cached = t;
    }
    return cached;
}

// ─── Яркость и расписание экрана ─────────────────────────
// Когда панели гореть, решает ScreenSchedule (screen_calc.h): окно по часу,
// ночная подсветка на POWER_SCREEN_PEEK_MS и рубеж по заряду. Здесь — только
// исполнение: панель, яркость и строки в журнал.
static ScreenSchedule screen;

// Час известен — ставим уровень по расписанию, нет — уровень «времени нет».
// В ручном режиме обе функции внутри ничего не делают.
static void applyAutoLevelFor(bool haveHour, int hour) {
    if (haveHour) displayAutoForHour(hour);
    else          displayAutoNoTime();
}

void applyAutoBrightness() {
    struct tm t;
    // Раньше функция выходила первой же строкой по !timeSynced — и вместе с
    // расписанием отключались и яркость, и рубеж по заряду: без синхронизации
    // NTP панель оставалась на стартовом уровне, а порог POWER_SCREEN_OFF_PCT
    // не срабатывал никогда. Устройство без сети жгло экран до brownout.
    // Теперь без времени пропускается только то, что без часа не считается.
    bool haveHour = timeSynced && localTimeNow(&t);
    int  hour     = haveHour ? t.tm_hour : 0;

    // Две причины гасить экран, и подсветка вправе перебить только одну.
    bool battOk  = powerScreenBatteryOkNow();
    bool schedOk = !haveHour || powerScreenScheduleAllowsNow(hour);

    ScreenStep s = screen.tick(millis(), battOk, schedOk, displayIsOn());
    if (s.peekExpired)                  Serial.println("Display: night peek expired");
    if (s.action == SCREEN_TURN_OFF)    displaySetPower(false);
    else if (s.action == SCREEN_TURN_ON) displaySetPower(true);

    applyAutoLevelFor(haveHour, hour);
}

uint32_t screenPeekLeftS() { return screen.peekLeftS(millis()); }

// Команда питания экрана из дашборда. Отдельно от displaySetPower(), потому
// что кроме самой панели трогает расписание: включение в запрещённый час
// заводит подсветку, выключение снимает её досрочно.
//
// Возвращает причину отказа или nullptr, если команда выполнена (почему
// причина, а не просто «не вышло» — у ScreenRefusal). Отказ виден снаружи и
// так: display_on в ответе и в снимке — фактическое состояние панели, а не
// то, что попросили.
const char* screenSetPower(bool on) {
    if (!on) {
        displaySetPower(false);
        screen.requestOff();
        return nullptr;
    }

    struct tm t;
    bool nightNow = timeSynced && localTimeNow(&t)
                 && !powerScreenScheduleAllowsNow(t.tm_hour);

    switch (screen.requestOn(millis(), powerScreenBatteryOkNow(),
                             displayLevel() == 0, nightNow, POWER_SCREEN_PEEK_MS)) {
    case SCREEN_REFUSED_BATTERY:
        Serial.println("Display: ON refused, battery critical");
        return "battery";
    case SCREEN_REFUSED_BRIGHTNESS:
        Serial.println("Display: ON refused, brightness is 0");
        return "brightness";
    case SCREEN_REFUSED_NONE:
        break;
    }

    displaySetPower(true);
    if (screen.peeking())
        Serial.printf("Display: night peek %lus\n",
                      (unsigned long)(POWER_SCREEN_PEEK_MS / 1000));
    return nullptr;
}

// ─── Глубокий сон на пустой банке ─────────────────────────
//
// Ноль шкалы (3.65 В, см. battery_calc.h) — это команда «стоп». Раньше он
// означал только «индикатор упёрся»: часы продолжали работать с погашенным
// экраном примерно до 3.55 В, где чип уходит в brownout, — то есть поднятый
// ради ресурса банки ноль защищал её лишь наполовину. Теперь на нуле мы
// перестаём тянуть из элемента совсем: ~20 мкА во сне по даташиту против
// измеренных 60–80 мА с погашенным экраном (config.h, POWER_SLEEP_PCT).
//
// Будильник — таймер: физической кнопки нет, а подключение USB чип не
// перезагружает (питание идёт через тот же диод). Поэтому раз в
// POWER_SLEEP_CHECK_MS просыпаемся, читаем АЦП и решаем — вставать или спать
// дальше. Проверка стоит доли секунды: ни экран, ни радио на этом пути не
// поднимаются, полный setup() до неё не доходит.
static void deepSleepNow(const char* why) {
    // Диод гасим здесь, а не на вызывающей стороне: WS2812 держит защёлкнутый
    // цвет, пока на него подано питание, а GPIO в глубоком сне просто уходит в
    // высокий импеданс. Путь из loop() гасил диод сам, путь проверки заряда —
    // нет, и «выключенные» часы светили синим со старта setup() все пять минут.
    // Собственное потребление самого WS2812 гашение не снимает: для этого нужен
    // ключ по питанию, которого в схеме нет.
    ledColor(0, 0, 0);
    Serial.printf("[Battery] %s -> deep sleep, проверка через %lu мин\n",
                  why, (unsigned long)(POWER_SLEEP_CHECK_MS / 60000UL));
    Serial.flush();
    esp_sleep_enable_timer_wakeup((uint64_t)POWER_SLEEP_CHECK_MS * 1000ULL);
    esp_deep_sleep_start();
}

// Полный путь: гасим то, что успели поднять, и засыпаем.
static void sleepUntilCharged() {
    screenSetPower(false);
    netShutdown();
    deepSleepNow("банка пуста");
}

// Ноль должен продержаться POWER_SLEEP_CONFIRM_MS: одиночный выброс АЦП не
// должен стоить пяти минут темноты. Ноль на пустом месте маловероятен —
// медиана набора и сглаживание его давят, — но цена ошибки тут велика.
static void checkBatteryEmpty() {
    static HoldTimer empty;
    bool zero = powerShouldSleep(battery.percent, battery.valid, POWER_SLEEP_PCT);
    switch (empty.step(zero, millis(), POWER_SLEEP_CONFIRM_MS)) {
    case HoldTimer::STARTED:
        Serial.println("[Battery] ноль шкалы, подтверждаем...");
        break;
    case HoldTimer::ELAPSED:
        sleepUntilCharged();
        break;
    case HoldTimer::NONE:
        break;
    }
}

// ─── Секундомер: команды ─────────────────────────────────
// Глубину сна радио под замер выбирает powerApplyRadio(): на ходу MIN_MODEM
// ради отзывчивости кнопок, на паузе и в простое — по профилю. Раньше здесь
// жила своя копия этого выбора (setRadioSaving), и две копии расходились:
// переподключение посреди паузы оставляло радио не в той глубине.
//
// Все три вызываются из задачи веб-сервера и будят loop(): иначе замер начал
// бы тикать на экране только со следующей секунды.

void swStart() {
    if (stopwatch.start(millis())) {
        displayInvalidateStopwatch();   // первый кадр после старта — полный
        powerApplyRadio();

        // Замер обязан быть виден, из какого бы режима и часа его ни начали.
        // powerLoop() здесь, а не на следующем обороте цикла: он поднимает
        // уровень до обычного (в том числе поверх ручной фиксации), а вместе
        // с ним снимает ночное гашение экрана — иначе первые кадры уходили бы
        // в темноту. screenSetPower() добирает случай, когда панель погасили
        // руками кнопкой в дашборде.
        //
        // Ползунок яркости в нуле — единственное, чего это не перебивает:
        // screenSetPower() там откажет, и отсчёт останется только в браузере.
        // Ноль пользователь выставил руками, и молча переехать с него значило
        // бы соврать дашборду о яркости (см. displaySetPower).
        //
        // Порядок важен: после powerLoop() режим уже «обычный», расписание
        // экран не ограничивает, и screenSetPower() не заводит ночную
        // подсветку на POWER_SCREEN_PEEK_MS — иначе она погасила бы панель
        // через полминуты посреди отсчёта.
        powerLoop();
        screenSetPower(true);
        wakeLoop();

        Serial.println("Stopwatch START");
    }
}

void swPause() {
    if (stopwatch.pause(millis())) {
        forceRedraw = true;             // маркер «II» — сразу, а не со сменой секунды
        powerApplyRadio();              // счётчик заморожен, торопиться некуда
        wakeLoop();
        Serial.println("Stopwatch PAUSE");
    }
}

void swReset() {
    stopwatch.reset();
    forceRedraw = true;                 // вернуть часы на экран сразу, а не через секунду
    displayInvalidateStopwatch();
    powerApplyRadio();
    wakeLoop();
    Serial.println("Stopwatch RESET");
}

static bool updateTimeStrings() {
    static time_t lastEpoch = 0;
    static bool   rendered  = false;

    time_t now = time(nullptr);
    if (rendered && now == lastEpoch) return false;
    lastEpoch = now;
    rendered  = true;

    struct tm t;
    // timeSynced — живой признак «часы идут», а не «синк на старте прошёл»:
    // иначе после неудачного старта флаг оставался бы ложным даже с верным
    // временем, а после потери часов — истинным с прочерками на экране.
    // Раскладываем тот же now, что дал шаг «секунда сменилась», — иначе
    // кадр мог бы отрисовать соседнюю секунду.
    bool ok = localTimeNow(&t, now);
    if (ok != timeSynced) {
        timeSynced = ok;
        Serial.println(ok ? "Clock: time acquired" : "Clock: time LOST");
    }

    if (ok) {
        // Форматы — те, что покрыты тестами (clock_utils.h). Раньше здесь
        // стояли свои копии snprintf, и тесты проверяли код, который прошивка
        // не вызывала.
        formatTime(t.tm_hour, t.tm_min, t.tm_sec, timeBuf, sizeof(timeBuf));
        formatDate(t.tm_mday, MONTHS[t.tm_mon], t.tm_year + 1900,
                   dateBuf, sizeof(dateBuf));
        snprintf(dayShortBuf, sizeof(dayShortBuf), "%s", DAYS_SHORT[t.tm_wday]);
        snprintf(dayFullBuf,  sizeof(dayFullBuf),  "%s", DAYS_FULL[t.tm_wday]);
    } else {
        snprintf(timeBuf,     sizeof(timeBuf),     "--:--:--");
        snprintf(dateBuf,     sizeof(dateBuf),     "-- --- ----");
        snprintf(dayShortBuf, sizeof(dayShortBuf), "---");
        snprintf(dayFullBuf,  sizeof(dayFullBuf),  "---");
    }
    return true;
}

// ─── Метео: опрос датчика + индикация LED ────────────────
// Мигок LED без delay(): гасим на следующих итерациях loop(). Длительность
// нужна и паузе цикла: в простое он спит до смены секунды, и без неё мигок
// растянулся бы до секунды.
static const uint32_t LED_BLINK_MS = 30;
static uint32_t ledBlinkStart = 0;
static bool     ledBlinking   = false;

static void ledBlink(uint8_t r, uint8_t g, uint8_t b) {
    ledColor(r, g, b);
    ledBlinkStart = millis();
    ledBlinking   = true;
}

// Минута по часам для SensorClock; -1 — часы не встали.
static int clockMinute() {
    struct tm t;
    return localTimeNow(&t) ? t.tm_min : -1;
}

static void updateWeather() {
    uint32_t now = millis();

    if (ledBlinking && now - ledBlinkStart >= LED_BLINK_MS) {
        ledColor(0, 0, 0);
        ledBlinking = false;
    }

    // Эконом гасит индикацию сразу, а не на ближайшем опросе датчика. Пока эта
    // проверка стояла только внутри опроса, ровный красный «датчик умер»,
    // зажжённый в обычном режиме, переживал возврат в эконом на целую минуту —
    // ровно в режиме, про который сказано «индикация молчит вся». Тем же
    // заходом снимается синий из setup(), если первый опрос ещё не подошёл.
    if (!powerLedEnabled() && ledOn) {
        ledColor(0, 0, 0);
        ledBlinking = false;
    }

    // Заряд забираем каждую итерацию: batteryLoop() всё равно считает его
    // непрерывно, и копия структуры бесплатна. Иначе показания зависели бы
    // от периода опроса датчика, а от него зависят пороги гашения экрана.
    battery = batteryRead();

    // Погоду спрашиваем редко: температура и давление за минуту никуда
    // не убегут, а каждый опрос I²C — это работа шины и ядра.
    if (sensorClock.due(now, clockMinute(), powerSensorIntervalMs())) {
        weather = sensorRead(sensorTempOffset());
        historyPush(now);
        // Вся индикация под профилем разом: в экономе она молчит, включая
        // аварийную. Гашение вынесено наверх функции — здесь остаётся только
        // зажигание, и ветка «эконом» больше не нужна.
        if (powerLedEnabled()) {
            if (!weather.valid) {
                ledColor(4, 0, 0);               // красный — ошибка датчика
                ledBlinking = false;             // горит ровно, не мигок
            } else if (battery.valid && battery.low) {
                ledBlink(6, 3, 0);               // жёлтый — АКБ разряжена
            } else {
                ledBlink(0, 3, 0);               // зелёный — норма
            }
        }
    }
}

// Сколько спать в конце оборота loop(). На ходу секундомера — коротко, кадр
// тикает 25 fps. В простое — до смены секунды (schedule_calc.h), но так,
// чтобы не проспать конец мигка LED.
static uint32_t loopPauseMs() {
    if (stopwatch.running()) return LOOP_STOPWATCH_MS;

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint32_t pause = loopIdlePauseMs((uint32_t)(tv.tv_usec / 1000), LOOP_IDLE_MS);

    if (ledBlinking) {
        uint32_t lit  = millis() - ledBlinkStart;
        uint32_t left = lit < LED_BLINK_MS ? LED_BLINK_MS - lit + 1 : 1;
        if (left < pause) pause = left;
    }
    return pause;
}

// ── Причина последнего сброса ─────────────────────────────
//  Перезагрузившиеся ночью часы выглядят в дашборде ровно как часы, которые
//  не перезагружались: аптайм обнулился, и всё. А различать тут есть что —
//  просадка питания на исходе банки, паника прошивки и сторожевой таймер
//  требуют разных действий, и узнавать о них, подключившись к USB задним
//  числом, поздно. Причина сброса переживает и brownout (в отличие от core
//  dump: писать во флеш на падающем питании уже нечем), поэтому она едет
//  в снимок и оседает в журнале дашборда.
static esp_reset_reason_t bootReason = ESP_RST_UNKNOWN;

const char* resetReasonName() {
    switch (bootReason) {
        case ESP_RST_POWERON:    return "power-on";
        case ESP_RST_EXT:        return "external pin";
        case ESP_RST_SW:         return "software";      // ESP.restart(), в т.ч. /api/reboot
        case ESP_RST_PANIC:      return "panic";
        case ESP_RST_INT_WDT:    return "interrupt watchdog";
        case ESP_RST_TASK_WDT:   return "task watchdog";
        case ESP_RST_WDT:        return "watchdog";
        case ESP_RST_DEEPSLEEP:  return "deep sleep";
        case ESP_RST_BROWNOUT:   return "brownout";
        case ESP_RST_SDIO:       return "SDIO";
        case ESP_RST_USB:        return "USB";
        case ESP_RST_JTAG:       return "JTAG";
        case ESP_RST_EFUSE:      return "efuse error";
        case ESP_RST_PWR_GLITCH: return "power glitch";
        case ESP_RST_CPU_LOCKUP: return "CPU lockup";
        default:                 return "unknown";
    }
}

// Штатное — это включили питание, нажали reset, перезагрузились по своей же
// команде, проснулись из сна. Всё прочее авария, и дашборд поднимет её в
// журнале до warn, вместо того чтобы утопить в потоке info.
//
// USB — тоже штатный: так C6 перезагружает хост через USB-Serial-JTAG, то есть
// esptool после прошивки и монитор при открытии порта. Пока он числился
// аварией, warn в журнале появлялся после каждой прошивки и приучал его не
// читать.
bool resetWasAbnormal() {
    switch (bootReason) {
        case ESP_RST_POWERON:
        case ESP_RST_EXT:
        case ESP_RST_SW:
        case ESP_RST_USB:
        case ESP_RST_DEEPSLEEP: return false;
        default:                return true;
    }
}

// Экран на старте поднимается в одном из двух мест setup(): сразу или после
// проверки заряда. Функция одна, чтобы эти места не разъехались.
static void bootSplash() {
    displayBegin();
    displaySplash("Starting...");
}

// ─────────────────────────────────────────────────────────
void setup() {
    // Весь setup() — под замком: обработчики веб-сервера ждут, пока старт
    // не закончится, и не видят недособранного состояния.
    appMutex = xSemaphoreCreateMutex();
    loopWake = xSemaphoreCreateBinary();
    appLock();

    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    // Без этого write() в USB-CDC блокирует loop() на секунды,
    // когда порт открыт, но никто не вычитывает.
    Serial.setTxTimeoutMs(0);
#endif

    // Пробуждение по таймеру — не старт, а проверка заряда, и стоить она должна
    // единицы миллисекунд: платит за неё пустая банка, двенадцать раз в час.
    // Поэтому решение принимается здесь, выше всего остального: ни диода, ни
    // заставки, ни паузы под USB CDC, ни I²C с датчиком — только АЦП.
    //
    // Раньше проверка стояла ниже delay(1500) и sensorInit(), то есть обходилась
    // в полторы секунды при 60–80 мА, а с неответившим датчиком — в две с
    // лишним. Это около 0.4 мА среднего тока: на порядок больше самого сна,
    // ради которого всё и затевалось.
    //
    // Тот же вопрос задаётся после сброса по питанию — brownout или глитч.
    // Раньше такой сброс шёл сразу в обычный старт: заставка на OLED, полторы
    // секунды ожидания и подъём Wi-Fi — самый крупный бросок тока за всю работу.
    // Банка, просевшая до сброса на пике передачи, на нём же проседала снова,
    // и часы грузились по кругу. До нуля в checkBatteryEmpty() дело при этом не
    // доходило: медиана и сглаживание прячут короткие провалы, и шкала на таком
    // сбросе ещё выше нуля, — так что банка разряжалась ниже нуля шкалы, ровно
    // туда, куда сон её не пускает. Порог тот же, что у пробуждения: сброс снял
    // нагрузку, банка отскочила, и вставать стоит, только если заряд с запасом.
    const esp_reset_reason_t rst = esp_reset_reason();
    const bool timerWake   = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
    const bool powerReset  = rst == ESP_RST_BROWNOUT || rst == ESP_RST_PWR_GLITCH;
    const bool chargeCheck = timerWake || powerReset;
    if (chargeCheck) {
        batteryInit();
        battery = batteryRead();
        if (!powerShouldWake(battery.percent, battery.valid, POWER_WAKE_PCT))
            deepSleepNow(timerWake ? "заряд всё ещё на нуле"
                                   : "сброс по питанию на разряженной банке");
    }

    // Отклик на RST — до всякого ожидания. Раньше синий и заставка шли после
    // паузы под USB ниже, а экран до своей инициализации не трогался: полторы
    // секунды после нажатия на плате не менялось ничего, будто кнопка не
    // сработала. Сюда доходят оба пути одинаково: решили вставать — дальше
    // обычный старт.
    ledColor(0, 0, 5);   // dim синий на старте
    bootSplash();

    delay(1500);   // ждём поднятия USB CDC на хосте, иначе стартовый лог теряется

    Serial.println("\n=== ESP32-C6 Clock + Weather boot ===");
    bootReason = esp_reset_reason();
    Serial.printf("Reset reason: %s%s\n", resetReasonName(),
                  resetWasAbnormal() ? "  <-- аварийный" : "");

    // 80 МГц вместо 160: для часов + веб-сервера хватает с запасом,
    // а нагрев кристалла и потребление заметно ниже. 80 — минимум для WiFi.
    setCpuFrequencyMhz(80);
    Serial.printf("CPU @ %u MHz\n", (unsigned)getCpuFrequencyMhz());

    // Датчик BMP280 и батарея
    if (!sensorInit()) {
        for (int i = 0; i < 4; i++) { ledColor(20,0,0); delay(150); ledColor(0,0,0); delay(150); }
    }
    // На пути проверки заряда АЦП настроен и замерен выше — второй раз незачем.
    if (!chargeCheck) batteryInit();
    battery = batteryRead();

    // Время сон переживает: RTC идёт и в нём. Если часы показывают
    // правдоподобную дату, синхронизация уже была — иначе экран рисовал бы
    // прочерки при исправных часах, пока не дойдёт очередь до NTP.
    struct tm t;
    if (localTimeNow(&t)) {
        timeSynced = true;
        Serial.println("RTC пережил перезагрузку, время на месте");
    }

    weather = sensorRead(sensorTempOffset());
    historyPush(millis());

    netBegin();            // первым: WiFi.mode() поднимает стек под SNTP и веб
    powerBegin();          // радио профиль получит в wifiOnConnected() (net.cpp)
    webApiBegin();

#if MQTT_ENABLED
    mqttInit();
#endif

    // Синий значит «стартуем», и старт на этом закончен. Подключение к WiFi
    // дальше идёт в loop(), но держать синий до него не выйдет: стартовый
    // уровень — эконом, индикация в нём молчит, и updateWeather() погасил бы
    // синий первым же оборотом. Раньше его не снимал никто, и гас он только
    // на первом опросе датчика — через минуту-две, когда дашборд давно отвечал.
    ledColor(0, 0, 0);

    // Сон — последним: все ноги, которые он должен оставить в покое, к этому
    // моменту уже настроены, а стартовые delay() выше спят и без него.
    powerEnableLightSleep();

    // Сторожевой таймер на loop(). Ядро по умолчанию его не включает
    // (loopTaskWDTEnabled = false), и проверка idle-задачи в сборке тоже
    // выключена: зависни цикл на I²C или на замке — часы молча замерли бы до
    // ручного сброса. Сам таймер в сборке уже заведён — 5 с и паника, — так
    // что зависание кончится перезагрузкой с причиной «task watchdog», которую
    // дашборд поднимает до warn. Самый долгий законный оборот — подключение к
    // MQTT, до MQTT_CONNECT_TIMEOUT_MS + MQTT_SOCKET_TIMEOUT_S, то есть 3 с, —
    // в эти 5 с укладывается. Включаем в конце: стартовые delay() и sensorInit()
    // на мёртвой шине ему незачем.
    enableLoopWDT();

    appUnlock();
}

void loop() {
    appLock();
    webApiLoop();
    batteryLoop();            // копит отсчёты АЦП по одному, без задержек
    checkBatteryEmpty();      // ноль шкалы -> deep sleep, дальше не возвращаемся
    powerLoop();              // уровень энергосбережения
    netLoop();
    updateWeather();          // BMP280 + батарея по таймеру + LED

    bool frameDue = updateTimeStrings() || forceRedraw;
    forceRedraw = false;

    if (stopwatch.running()) {
        static uint32_t lastSwDraw = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastSwDraw >= SW_DRAW_INTERVAL_MS) {
            lastSwDraw = nowMs;
            displayStopwatchFrame();
        }
        // Часы/аптайм в браузере: broadcast раз в секунду
        if (frameDue) {
            applyAutoBrightness();
            webApiBroadcast();
        }
    } else if (frameDue) {
        applyAutoBrightness();
        displayDraw();
        webApiBroadcast();
    }

#if MQTT_ENABLED
    mqttLoop(weather, battery);
#endif

    // Пульс в Serial раз в 5 с — монитор покажет жизнь, когда бы его ни открыли
    static uint32_t lastHb = 0;
    if (millis() - lastHb >= 5000) {
        lastHb = millis();
        Serial.printf("[hb] up=%lus wifi=%s ip=%s rssi=%d heap=%u clients=%u "
                      "chip=%.1fC bmp=%.1fC p=%.0fhPa bat=%d%% v=%.2f adc=%umV pm=%s\n",
                      (unsigned long)(millis() / 1000),
                      netConnected() ? "OK" : "DOWN",
                      localIP.length() ? localIP.c_str() : "-",
                      netRssi(),
                      (unsigned)esp_get_free_heap_size(),
                      (unsigned)webApiClientCount(),
                      dieTempC(),
                      weather.valid ? weather.temperature : 0.0f,
                      weather.valid ? weather.pressure : 0.0f,
                      battery.valid ? battery.percent : -1,
                      batteryRawVoltage(), (unsigned)batteryAdcMv(),
                      powerModeName());
    }

    // Пауза — окно для обработчиков веб-сервера: замок отпущен только на неё.
    // Будит её конец отсчёта или wakeLoop() — команда, которой экран нужен
    // сейчас, а не со следующей секундой.
    const uint32_t pauseMs = loopPauseMs();
    appUnlock();
    xSemaphoreTake(loopWake, pdMS_TO_TICKS(pauseMs));
}
