#include <unity.h>
#include <string.h>
#include <math.h>
#include "../../src/clock_utils.h"
#include "../../src/display_calc.h"
#include "../../src/battery_calc.h"
#include "../../src/power_calc.h"
#include "../../src/weather_calc.h"
#include "../../src/stopwatch.h"
#include "../../src/origin_check.h"

// ─── Uptime ───────────────────────────────────────────────
void test_uptime_seconds_only() {
    char buf[32];
    formatUptime(45, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00h 00m 45s", buf);
}

void test_uptime_minutes() {
    char buf[32];
    formatUptime(125, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00h 02m 05s", buf);
}

void test_uptime_hours() {
    char buf[32];
    formatUptime(3723, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01h 02m 03s", buf);
}

void test_uptime_days() {
    char buf[32];
    formatUptime(90061, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("1d 01h 01m 01s", buf);
}

void test_uptime_large() {
    char buf[32];
    formatUptime(86400 * 30, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("30d 00h 00m 00s", buf);
}

// ─── Яркость ─────────────────────────────────────────────
void test_brightness_night_22() {
    BrightnessLevel b = brightnessForHour(22);
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_NIGHT, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Night", b.label);
}

void test_brightness_night_3() {
    BrightnessLevel b = brightnessForHour(3);
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_NIGHT, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Night", b.label);
}

void test_brightness_morning() {
    BrightnessLevel b = brightnessForHour(7);
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_MORNING, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Morning", b.label);
}

void test_brightness_day() {
    BrightnessLevel b = brightnessForHour(12);
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_DAY, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Day", b.label);
}

void test_brightness_evening() {
    BrightnessLevel b = brightnessForHour(21);
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_EVENING, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Evening", b.label);
}

// Таблица часов задана на перцептивной шкале, а светимость должна остаться
// той, что была до перехода на гамму: тогда числа уезжали прямо в регистр
// тока. Этот тест и держит пересчёт — иначе таблицу снова однажды прочтут
// как доли тока и ночь опять просядет в тридцать раз.
static void assertAutoLevelDrive(uint8_t level, uint8_t legacyContrast) {
    float want = (float)(legacyContrast + 1) / 256.0f;
    float got  = panelDriveFraction(panelDriveForLevel(level));
    TEST_ASSERT_FLOAT_WITHIN(0.02f, want, got);
}

void test_brightness_levels_keep_pre_gamma_output() {
    assertAutoLevelDrive(CONTRAST_NIGHT,    15);
    assertAutoLevelDrive(CONTRAST_MORNING,  80);
    assertAutoLevelDrive(CONTRAST_EVENING, 120);
    assertAutoLevelDrive(CONTRAST_DAY,     200);
}

// Уровень, на который встаёт «включить экран» при нулевой яркости, обязан быть
// видимым: единица по гамме — это 1/4096 тока, панель на ней неотличима от
// выключенной, и экран числился включённым, оставаясь чёрным.
void test_min_visible_level_is_actually_visible() {
    float f = panelDriveFraction(panelDriveForLevel(CONTRAST_MIN_VISIBLE));
    TEST_ASSERT_TRUE(f > 20.0f * panelDriveFraction(panelDriveForLevel(1)));
    TEST_ASSERT_TRUE(f >= 1.0f / 256.0f);
}

// Часы не встали — уровень всё равно должен быть выставлен, и не максимальный:
// именно на максимуме панель раньше и оставалась без синхронизации NTP.
void test_brightness_no_time_is_not_full_scale() {
    BrightnessLevel b = brightnessNoTime();
    TEST_ASSERT_TRUE(b.contrast > 0);
    TEST_ASSERT_TRUE(b.contrast < CONTRAST_MAX);
    TEST_ASSERT_EQUAL_STRING("No time", b.label);
}

void test_brightness_boundary_6() {
    BrightnessLevel b = brightnessForHour(6);
    TEST_ASSERT_EQUAL_STRING("Morning", b.label);
}

void test_brightness_boundary_8() {
    BrightnessLevel b = brightnessForHour(8);
    TEST_ASSERT_EQUAL_STRING("Day", b.label);
}

void test_brightness_boundary_20() {
    BrightnessLevel b = brightnessForHour(20);
    TEST_ASSERT_EQUAL_STRING("Evening", b.label);
}

void test_brightness_pct_full() {
    TEST_ASSERT_EQUAL_UINT8(78, brightnessPct(200));
}

void test_brightness_pct_night() {
    TEST_ASSERT_EQUAL_UINT8(6, brightnessPct(15));   // 15/255 = 5.88 %, округляем
}

void test_brightness_pct_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, brightnessPct(0));
}

// Ноль — это «выключить», а не «почти не светит»: округление не должно его
// сдвинуть, иначе ползунок в нижнем положении переставал бы гасить панель.
void test_pct_zero_stays_off() {
    TEST_ASSERT_EQUAL_UINT8(0, pctToContrast(0));
    TEST_ASSERT_EQUAL_UINT8(0, pctToContrast(-5));
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_MAX, pctToContrast(100));
    TEST_ASSERT_EQUAL_UINT8(CONTRAST_MAX, pctToContrast(150));
}

// Путь «проценты → уровень → проценты» обязан сходиться на всей шкале. С
// отбрасыванием дробной части он терял единицу на каждом обороте (50 → 127 →
// 49 → 124 → 48): дашборд показывал не то, что выставили, а скрипт, читающий
// brightness_pct и пишущий его обратно, медленно уводил яркость вниз.
void test_pct_contrast_roundtrip_is_stable() {
    for (int pct = 0; pct <= 100; pct++) {
        uint8_t level = pctToContrast(pct);
        TEST_ASSERT_EQUAL_UINT8((uint8_t)pct, brightnessPct(level));
        // и второй оборот не сдвигает уровень
        TEST_ASSERT_EQUAL_UINT8(level, pctToContrast(brightnessPct(level)));
    }
}

// ─── Время ────────────────────────────────────────────────
void test_format_time_normal() {
    char buf[9];
    formatTime(14, 5, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("14:05:03", buf);
}

void test_format_time_midnight() {
    char buf[9];
    formatTime(0, 0, 0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:00:00", buf);
}

void test_format_time_max() {
    char buf[9];
    formatTime(23, 59, 59, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("23:59:59", buf);
}

// ─── Дата ─────────────────────────────────────────────────
void test_format_date() {
    char buf[20];
    formatDate(10, "MAY", 2026, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("10 MAY 2026", buf);
}

void test_format_date_single_digit_day() {
    char buf[20];
    formatDate(1, "JAN", 2026, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01 JAN 2026", buf);
}

// ─── RSSI ─────────────────────────────────────────────────
void test_rssi_excellent() { TEST_ASSERT_EQUAL_INT(4, rssiToLevel(-45)); }
void test_rssi_good()      { TEST_ASSERT_EQUAL_INT(3, rssiToLevel(-55)); }
void test_rssi_fair()      { TEST_ASSERT_EQUAL_INT(2, rssiToLevel(-65)); }
void test_rssi_poor()      { TEST_ASSERT_EQUAL_INT(1, rssiToLevel(-80)); }

void test_rssi_boundary_50() {
    TEST_ASSERT_EQUAL_INT(4, rssiToLevel(-50));
}
void test_rssi_boundary_51() {
    TEST_ASSERT_EQUAL_INT(3, rssiToLevel(-51));
}

// ─── Секундомер ───────────────────────────────────────────
void test_stopwatch_zero() {
    char buf[16];
    formatStopwatch(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:00.000", buf);
}

void test_stopwatch_milliseconds() {
    char buf[16];
    formatStopwatch(1500, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:01.500", buf);
}

void test_stopwatch_minutes() {
    char buf[16];
    formatStopwatch(65432, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:05.432", buf);
}

// Последняя миллисекунда до перехода на формат с часами
void test_stopwatch_before_hour() {
    char buf[16];
    formatStopwatch(3599999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("59:59.999", buf);
}

// Ровно час — формат меняется на HH:MM:SS, миллисекунды пропадают
void test_stopwatch_exactly_hour() {
    char buf[16];
    formatStopwatch(3600000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:00:00", buf);
}

void test_stopwatch_hours() {
    char buf[16];
    formatStopwatch(3661000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:01:01", buf);
}

void test_stopwatch_many_hours() {
    char buf[16];
    formatStopwatch(36000000UL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("10:00:00", buf);
}

// drawOLED() режет строку по 5-му символу: "MM:SS" крупным шрифтом,
// хвост — мелким. Инвариант обоих форматов: ровно 5 символов до разделителя
// и не длиннее 4 символов после. Ломается — едет вся раскладка экрана.
void test_stopwatch_split_layout() {
    char buf[16];
    const uint32_t samples[] = { 0, 1500, 3599999, 3600000, 36000000UL };
    for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        formatStopwatch(samples[i], buf, sizeof(buf));
        TEST_ASSERT_TRUE(buf[5] == '.' || buf[5] == ':');
        TEST_ASSERT_TRUE(strlen(buf + 5) <= 4);
    }
}

// ─── Секундомер: автомат ──────────────────────────────────
void test_sw_starts_idle() {
    Stopwatch sw;
    TEST_ASSERT_TRUE(sw.idle());
    TEST_ASSERT_FALSE(sw.running());
    TEST_ASSERT_EQUAL_UINT32(0, sw.elapsed(123456));
}

void test_sw_runs_from_start_moment() {
    Stopwatch sw;
    TEST_ASSERT_TRUE(sw.start(1000));
    TEST_ASSERT_TRUE(sw.running());
    TEST_ASSERT_EQUAL_UINT32(0,    sw.elapsed(1000));
    TEST_ASSERT_EQUAL_UINT32(2500, sw.elapsed(3500));
}

// Повторный старт не должен сбивать точку отсчёта
void test_sw_restart_is_noop() {
    Stopwatch sw;
    sw.start(1000);
    TEST_ASSERT_FALSE(sw.start(5000));
    TEST_ASSERT_EQUAL_UINT32(4000, sw.elapsed(5000));
}

void test_sw_pause_freezes() {
    Stopwatch sw;
    sw.start(1000);
    TEST_ASSERT_TRUE(sw.pause(4000));
    TEST_ASSERT_EQUAL_UINT32(3000, sw.elapsed(4000));
    TEST_ASSERT_EQUAL_UINT32(3000, sw.elapsed(999999));   // время идёт, счётчик — нет
}

void test_sw_pause_when_not_running() {
    Stopwatch sw;
    TEST_ASSERT_FALSE(sw.pause(1000));       // на месте
    sw.start(1000);
    sw.pause(2000);
    TEST_ASSERT_FALSE(sw.pause(3000));       // уже на паузе
    TEST_ASSERT_EQUAL_UINT32(1000, sw.elapsed(3000));
}

// Возобновление продолжает счёт с накопленного, а не с нуля
void test_sw_resume_accumulates() {
    Stopwatch sw;
    sw.start(1000);
    sw.pause(3000);                          // накоплено 2000
    TEST_ASSERT_TRUE(sw.start(10000));
    TEST_ASSERT_EQUAL_UINT32(2000, sw.elapsed(10000));
    TEST_ASSERT_EQUAL_UINT32(2500, sw.elapsed(10500));
    sw.pause(11000);
    TEST_ASSERT_EQUAL_UINT32(3000, sw.elapsed(99999));
}

void test_sw_reset_clears() {
    Stopwatch sw;
    sw.start(1000);
    sw.pause(5000);
    sw.reset();
    TEST_ASSERT_TRUE(sw.idle());
    TEST_ASSERT_EQUAL_UINT32(0, sw.elapsed(9000));
    // после сброса запускается как новый
    sw.start(20000);
    TEST_ASSERT_EQUAL_UINT32(1000, sw.elapsed(21000));
}

void test_sw_reset_while_running() {
    Stopwatch sw;
    sw.start(1000);
    sw.reset();
    TEST_ASSERT_TRUE(sw.idle());
    TEST_ASSERT_EQUAL_UINT32(0, sw.elapsed(50000));
}

// millis() переполняется каждые ~49 суток; беззнаковая арифметика
// обязана пережить это без скачка показаний
void test_sw_survives_millis_overflow() {
    Stopwatch sw;
    sw.start(0xFFFFF000UL);
    TEST_ASSERT_EQUAL_UINT32(0x1100UL, sw.elapsed(0x00000100UL));
}

// Веб-интерфейс получает состояние числом в поле sw_state —
// значения менять нельзя, иначе кнопки в браузере поедут
// ─── Номер замера (gen) ──────────────────────────────────
// Круги живут в браузере, устройство о них не знает. Отличить «связь моргнула,
// замер тот же» от «замер начали заново» можно только по этому номеру, поэтому
// правило, когда он меняется, а когда нет, закреплено тестами.

// Пауза и продолжение — тот же замер: круги обязаны пережить их.
void test_sw_gen_survives_pause_resume() {
    Stopwatch sw;
    sw.start(1000);
    uint32_t gen = sw.gen;
    sw.pause(2000);
    TEST_ASSERT_EQUAL_UINT32(gen, sw.gen);
    sw.start(3000);
    TEST_ASSERT_EQUAL_UINT32(gen, sw.gen);
}

// Повторный старт на ходу ничего не меняет — не меняет и номер.
void test_sw_gen_unchanged_on_restart_noop() {
    Stopwatch sw;
    sw.start(1000);
    uint32_t gen = sw.gen;
    sw.start(1500);
    TEST_ASSERT_EQUAL_UINT32(gen, sw.gen);
}

// Сброс и старт из простоя — уже другой замер.
void test_sw_gen_changes_on_new_run() {
    Stopwatch sw;
    sw.start(1000);
    uint32_t first = sw.gen;
    sw.reset();
    TEST_ASSERT_NOT_EQUAL(first, sw.gen);
    uint32_t afterReset = sw.gen;
    sw.start(2000);
    TEST_ASSERT_NOT_EQUAL(afterReset, sw.gen);
    TEST_ASSERT_NOT_EQUAL(first, sw.gen);
}

// Номер не переиспользуется: подряд идущие замеры различимы между собой.
void test_sw_gen_is_unique_per_run() {
    Stopwatch sw;
    uint32_t seen[4];
    for (int i = 0; i < 4; i++) {
        sw.start(1000 * (i + 1));
        seen[i] = sw.gen;
        sw.reset();
    }
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
            TEST_ASSERT_NOT_EQUAL(seen[i], seen[j]);
}

void test_sw_state_codes_are_stable() {
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)Stopwatch::IDLE);
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)Stopwatch::RUNNING);
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)Stopwatch::PAUSED);
}

// ─── Батарея ──────────────────────────────────────────────
void test_battery_full() {
    TEST_ASSERT_EQUAL_UINT8(100, batteryVoltageToPercent(4.20f));
}

void test_battery_above_full() {
    TEST_ASSERT_EQUAL_UINT8(100, batteryVoltageToPercent(4.25f));
}

void test_battery_empty() {
    TEST_ASSERT_EQUAL_UINT8(0, batteryVoltageToPercent(3.00f));
}

void test_battery_below_empty() {
    TEST_ASSERT_EQUAL_UINT8(0, batteryVoltageToPercent(2.50f));
}

void test_battery_curve_point() {
    TEST_ASSERT_EQUAL_UINT8(50, batteryVoltageToPercent(3.82f));
}

// Ноль шкалы — на отсечке устройства, а не элемента: ниже ~3.55 В диод и LDO
// уже не держат 3.3 В, и оставшаяся ёмкость банки этим часам недоступна
void test_battery_curve_tail_matches_cutoff() {
    TEST_ASSERT_EQUAL_UINT8(10, batteryVoltageToPercent(3.68f));
    TEST_ASSERT_EQUAL_UINT8(5,  batteryVoltageToPercent(3.60f));
    TEST_ASSERT_EQUAL_UINT8(0,  batteryVoltageToPercent(3.50f));
    TEST_ASSERT_EQUAL_UINT8(0,  batteryVoltageToPercent(3.45f));
}

// Хвост должен быть растянут, а не поджат: на пункт шкалы внизу приходится
// заметно больше милливольт, чем шумит АЦП, иначе индикатор дрожит на месте.
// Между 3.74 В и 3.68 В — десять пунктов, то есть 6 мВ на пункт (было 2 мВ),
// ниже — ещё положе. Проверяем только хвост: середина кривой круче по
// физике элемента (на плато 3.77…3.79 В лежат целых десять процентов),
// и растягивать её было бы не честнее, а наоборот.
void test_battery_tail_is_not_steeper_than_adc_noise() {
    const int NOISE_MV = 4;              // шум медианы набора — единицы мВ
    for (int mv = 3500; mv + NOISE_MV <= 3740; mv++) {
        uint8_t lo = batteryVoltageToPercent(mv / 1000.0f);
        uint8_t hi = batteryVoltageToPercent((mv + NOISE_MV) / 1000.0f);
        TEST_ASSERT_TRUE_MESSAGE(hi - lo <= 1,
                                 "хвост кривой круче одного пункта на 4 мВ");
    }
}

// Середина отрезка 3.68В(10%)…3.74В(20%) — проверяем интерполяцию
void test_battery_interpolation() {
    TEST_ASSERT_UINT8_WITHIN(1, 15, batteryVoltageToPercent(3.71f));
}

// Пороги режимов заданы в процентах, но настраивались по напряжению.
// Кривая и config.h должны сходиться: радио выключается раньше экрана,
// и оба рубежа — выше отсечки железа.
void test_battery_thresholds_land_where_intended() {
    TEST_ASSERT_UINT8_WITHIN(1, 15, batteryVoltageToPercent(3.71f));  // survival
    TEST_ASSERT_EQUAL_UINT8(5,      batteryVoltageToPercent(3.60f));  // экран прочь
    TEST_ASSERT_EQUAL_UINT8(20,     batteryVoltageToPercent(3.74f));  // BATTERY_LOW_V
}

// Кривая обязана быть монотонной: выше напряжение — не меньше процент
void test_battery_monotonic() {
    uint8_t prev = 0;
    for (int mv = 3000; mv <= 4200; mv += 10) {
        uint8_t pct = batteryVoltageToPercent(mv / 1000.0f);
        TEST_ASSERT_TRUE(pct >= prev);
        prev = pct;
    }
    TEST_ASSERT_EQUAL_UINT8(100, prev);
}

// Пересчёт процентов в мА·ч. Шкала идёт по доступной ёмкости, а не по
// паспортной: сотня — это полный бак этих часов (~2600 мА·ч), а не полная
// банка (~3300). Остальные ~700 лежат ниже отсечки 3.5 В.
void test_battery_mah_scale_is_usable_capacity() {
    TEST_ASSERT_EQUAL_UINT16(2600, batteryRemainingMah(100, 2600));
    TEST_ASSERT_EQUAL_UINT16(1300, batteryRemainingMah(50,  2600));
    TEST_ASSERT_EQUAL_UINT16(0,    batteryRemainingMah(0,   2600));
}

// Битый процент выше сотни не должен давать больше полной банки
void test_battery_mah_clamps_above_full() {
    TEST_ASSERT_EQUAL_UINT16(2600, batteryRemainingMah(200, 2600));
}

void test_battery_plausible_usb() {
    TEST_ASSERT_FALSE(batteryVoltagePlausible(0.0f));    // вход висит в воздухе
    TEST_ASSERT_FALSE(batteryVoltagePlausible(2.79f));
    TEST_ASSERT_FALSE(batteryVoltagePlausible(4.61f));
    TEST_ASSERT_FALSE(batteryVoltagePlausible(5.00f));   // делитель цепляет не банку
}

void test_battery_plausible_battery() {
    TEST_ASSERT_TRUE(batteryVoltagePlausible(2.80f));
    TEST_ASSERT_TRUE(batteryVoltagePlausible(3.70f));
    TEST_ASSERT_TRUE(batteryVoltagePlausible(4.60f));
}

// Полная банка на зарядке плюс поправка BATTERY_CAL с середины разряда:
// 4.20 В превращаются в 4.31–4.33, и это ещё батарея, а не «АКБ снята»
void test_battery_plausible_full_with_calibration() {
    TEST_ASSERT_TRUE(batteryVoltagePlausible(4.20f * 1.027f));
    TEST_ASSERT_TRUE(batteryVoltagePlausible(4.25f * 1.03f));
}

// Выше кривой уже некуда: 100 % отдаём и на границе правдоподобия
void test_battery_percent_saturates_above_curve() {
    TEST_ASSERT_EQUAL_UINT8(100, batteryVoltageToPercent(4.31f));
    TEST_ASSERT_EQUAL_UINT8(100, batteryVoltageToPercent(4.60f));
}

// ─── Батарея: фильтр замеров ─────────────────────────────
void test_battery_median_odd() {
    uint32_t v[5] = { 1950, 1948, 1600, 1952, 1949 };   // 1600 — провал от Wi-Fi
    TEST_ASSERT_EQUAL_UINT32(1949, batteryMedianMv(v, 5));
}

void test_battery_median_even() {
    uint32_t v[4] = { 1950, 1946, 1948, 1952 };         // среднее двух средних
    TEST_ASSERT_EQUAL_UINT32(1949, batteryMedianMv(v, 4));
}

void test_battery_median_sorts_in_place() {
    uint32_t v[4] = { 30, 10, 40, 20 };
    batteryMedianMv(v, 4);
    TEST_ASSERT_EQUAL_UINT32(10, v[0]);
    TEST_ASSERT_EQUAL_UINT32(40, v[3]);
}

void test_battery_median_empty() {
    TEST_ASSERT_EQUAL_UINT32(0, batteryMedianMv(nullptr, 0));
}

// Пачка провалов не сдвигает медиану, пока их меньше половины набора
void test_battery_median_ignores_minority_dips() {
    uint32_t v[9] = { 1950, 1950, 1951, 1600, 1580, 1620, 1949, 1950, 1951 };
    TEST_ASSERT_EQUAL_UINT32(1950, batteryMedianMv(v, 9));
}

void test_battery_smooth_pulls_toward_fresh() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.910f, batterySmooth(3.900f, 4.000f, 0.10f));
}

void test_battery_smooth_zero_alpha_holds() {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.900f, batterySmooth(3.900f, 4.000f, 0.0f));
}

// Единичный выброс за 16 наборов сдвигает результат меньше чем на 10 мВ
void test_battery_smooth_suppresses_single_spike() {
    float v = 3.900f;
    v = batterySmooth(v, 3.700f, 0.10f);
    for (int i = 0; i < 15; i++) v = batterySmooth(v, 3.900f, 0.10f);
    TEST_ASSERT_FLOAT_WITHIN(0.010f, 3.900f, v);
}

void test_battery_jump_detects_usb_unplug() {
    TEST_ASSERT_TRUE(batteryJumped(4.010f, 3.700f, 0.25f));
    TEST_ASSERT_TRUE(batteryJumped(3.700f, 4.010f, 0.25f));   // и в обратную сторону
}

void test_battery_jump_ignores_noise() {
    TEST_ASSERT_FALSE(batteryJumped(3.900f, 3.870f, 0.25f));
    TEST_ASSERT_FALSE(batteryJumped(3.900f, 3.900f, 0.25f));
}

// ─── Яркость: уровень шкалы → регистры тока SSD1322 ──────
// Максимум шкалы — максимум обоих регистров
void test_drive_full_scale() {
    PanelDrive d = panelDriveForLevel(255);
    TEST_ASSERT_EQUAL_UINT8(15,  d.master);
    TEST_ASSERT_EQUAL_UINT8(255, d.contrast);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, panelDriveFraction(d));
}

// Нижняя ступень — 1/4096 полного тока, а не 1/256: ради этого и заведён
// master-регистр, иначе между «выключено» и минимумом зияет провал
void test_drive_bottom_step_is_far_below_contrast_floor() {
    PanelDrive d = panelDriveForLevel(1);
    TEST_ASSERT_EQUAL_UINT8(0, d.master);
    TEST_ASSERT_TRUE(panelDriveFraction(d) < 1.0f / 256.0f);
}

// Шкала монотонна: ползунок вверх не должен нигде притушивать экран
void test_drive_is_monotonic() {
    float prev = -1.0f;
    for (int lv = 0; lv <= 255; lv++) {
        float f = panelDriveFraction(panelDriveForLevel((uint8_t)lv));
        TEST_ASSERT_TRUE(f >= prev);
        prev = f;
    }
}

// Середина шкалы — заметно меньше половины тока: восприятие нелинейно,
// линейная шкала оставляла бы почти всю разницу на нижних процентах
void test_drive_midpoint_is_gamma_corrected() {
    float f = panelDriveFraction(panelDriveForLevel(128));
    TEST_ASSERT_TRUE(f > 0.15f);
    TEST_ASSERT_TRUE(f < 0.30f);
}

// Регистры не выходят за диапазон, который принимает панель
void test_drive_registers_in_range() {
    for (int lv = 0; lv <= 255; lv++) {
        PanelDrive d = panelDriveForLevel((uint8_t)lv);
        TEST_ASSERT_TRUE(d.master <= 15);
        TEST_ASSERT_TRUE(panelDriveFraction(d) > 0.0f);
    }
}

// ─── Энергосбережение: автоматический выбор режима ───────
// Порядок аргументов длинный, поэтому обёртка: заряд 80 %, банка есть,
// порог выживания 10 %, гистерезис 5 %.
static PowerMode autoMode(PowerMode cur, bool sw,
                          uint8_t pct = 80, bool valid = true) {
    return powerAutoMode(cur, sw, pct, valid, 10, 5);
}

// Без секундомера и при живом заряде базовый режим — эконом, а не обычный
void test_auto_idle_is_eco() {
    TEST_ASSERT_EQUAL(POWER_ECO, autoMode(POWER_ECO, false));
    TEST_ASSERT_EQUAL(POWER_ECO, autoMode(POWER_SURVIVAL, false));
}

void test_auto_stopwatch_raises_to_normal() {
    TEST_ASSERT_EQUAL(POWER_NORMAL, autoMode(POWER_ECO, true));
}

// Секундомер главнее низкого заряда — иначе замер оборвался бы на середине
void test_auto_stopwatch_beats_low_charge() {
    TEST_ASSERT_EQUAL(POWER_NORMAL, autoMode(POWER_SURVIVAL, true, 4));
}

// Заряд на исходе — выживание в любое время суток
void test_auto_low_charge_is_survival() {
    TEST_ASSERT_EQUAL(POWER_SURVIVAL, autoMode(POWER_ECO, false, 10));
    TEST_ASSERT_EQUAL(POWER_SURVIVAL, autoMode(POWER_ECO, false, 3));
}

void test_auto_charge_hysteresis() {
    TEST_ASSERT_EQUAL(POWER_SURVIVAL, autoMode(POWER_SURVIVAL, false, 12));
    TEST_ASSERT_EQUAL(POWER_SURVIVAL, autoMode(POWER_SURVIVAL, false, 14));
    TEST_ASSERT_EQUAL(POWER_ECO,      autoMode(POWER_SURVIVAL, false, 15));
}

// На USB заряда не знаем — правило по заряду не применяем, остаёмся в экономе
void test_auto_usb_ignores_charge_rule() {
    TEST_ASSERT_EQUAL(POWER_ECO, autoMode(POWER_ECO, false, 0, false));
    TEST_ASSERT_EQUAL(POWER_ECO, autoMode(POWER_SURVIVAL, false, 0, false));
}

// Ночь на режим не влияет: экран гасит расписание, Wi-Fi остаётся поднятым,
// иначе ночью не запустить секундомер и не открыть дашборд
void test_auto_night_stays_eco() {
    TEST_ASSERT_EQUAL(POWER_ECO, autoMode(POWER_ECO, false));
    TEST_ASSERT_EQUAL(POWER_ECO, autoMode(POWER_SURVIVAL, false));
}

// ─── Энергосбережение: профили и расписание ──────────────
void test_power_profile_names() {
    TEST_ASSERT_EQUAL_STRING("normal",   powerProfile(POWER_NORMAL).name);
    TEST_ASSERT_EQUAL_STRING("eco",      powerProfile(POWER_ECO).name);
    TEST_ASSERT_EQUAL_STRING("survival", powerProfile(POWER_SURVIVAL).name);
}

void test_power_profile_tightens_with_level() {
    TEST_ASSERT_TRUE(powerProfile(POWER_ECO).sensorMs > powerProfile(POWER_NORMAL).sensorMs);
    TEST_ASSERT_TRUE(powerProfile(POWER_SURVIVAL).sensorMs > powerProfile(POWER_ECO).sensorMs);
    TEST_ASSERT_TRUE(powerProfile(POWER_ECO).contrastPct < powerProfile(POWER_NORMAL).contrastPct);
    TEST_ASSERT_TRUE(powerProfile(POWER_NORMAL).wifi);
    TEST_ASSERT_FALSE(powerProfile(POWER_SURVIVAL).wifi);
}

void test_power_profile_bad_index_is_normal() {
    TEST_ASSERT_EQUAL_STRING("normal", powerProfile((PowerMode)99).name);
}

void test_window_plain() {
    TEST_ASSERT_TRUE(hourInWindow(7, 7, 23));
    TEST_ASSERT_TRUE(hourInWindow(22, 7, 23));
    TEST_ASSERT_FALSE(hourInWindow(23, 7, 23));   // верхняя граница не входит
    TEST_ASSERT_FALSE(hourInWindow(6, 7, 23));
}

void test_window_over_midnight() {
    TEST_ASSERT_TRUE(hourInWindow(23, 22, 6));
    TEST_ASSERT_TRUE(hourInWindow(2, 22, 6));
    TEST_ASSERT_FALSE(hourInWindow(12, 22, 6));
}

void test_window_whole_day() {
    TEST_ASSERT_TRUE(hourInWindow(0, 7, 7));
    TEST_ASSERT_TRUE(hourInWindow(13, 7, 7));
}

// В обычном режиме расписание не действует — экран горит круглые сутки
void test_power_screen_normal_always_on() {
    TEST_ASSERT_TRUE(powerScreenAllowed(POWER_NORMAL, 3, 7, 23));
}

void test_power_screen_eco_follows_window() {
    TEST_ASSERT_FALSE(powerScreenAllowed(POWER_ECO, 3, 7, 23));
    TEST_ASSERT_TRUE(powerScreenAllowed(POWER_ECO, 12, 7, 23));
}

// Экран гасится по заряду независимо от режима и часа
void test_power_screen_off_on_critical_charge() {
    TEST_ASSERT_FALSE(powerScreenAllowedAt(POWER_NORMAL, 12, 7, 23, 5, true, 5));
    TEST_ASSERT_FALSE(powerScreenAllowedAt(POWER_NORMAL, 12, 7, 23, 2, true, 5));
}

void test_power_screen_on_above_critical() {
    TEST_ASSERT_TRUE(powerScreenAllowedAt(POWER_NORMAL, 12, 7, 23, 6, true, 5));
}

// На USB заряда не знаем — порог не применяем
void test_power_screen_ignores_critical_without_battery() {
    TEST_ASSERT_TRUE(powerScreenAllowedAt(POWER_NORMAL, 12, 7, 23, 0, false, 5));
}

// Расписание эконома при живом заряде продолжает действовать
// Рубеж по заряду обязан считаться без часа: он вынесен из расписания именно
// затем, чтобы работать и тогда, когда время не синхронизировалось.
void test_power_screen_battery_gate_needs_no_hour() {
    TEST_ASSERT_FALSE(powerScreenBatteryOk(5, true, 5));
    TEST_ASSERT_FALSE(powerScreenBatteryOk(0, true, 5));
    TEST_ASSERT_TRUE(powerScreenBatteryOk(6, true, 5));
    TEST_ASSERT_TRUE(powerScreenBatteryOk(0, false, 5));   // банки нет — не наш случай
}

void test_power_screen_critical_and_schedule_combine() {
    TEST_ASSERT_FALSE(powerScreenAllowedAt(POWER_ECO, 3, 7, 23, 80, true, 5));
    TEST_ASSERT_TRUE(powerScreenAllowedAt(POWER_ECO, 12, 7, 23, 80, true, 5));
    TEST_ASSERT_FALSE(powerScreenAllowedAt(POWER_ECO, 12, 7, 23, 4, true, 5));
}

// Секундомер поднимает уровень поверх ручной фиксации: замер запускают,
// чтобы на него смотреть, а зафиксированный эконом ночью гасит экран.
void test_power_manual_stopwatch_beats_choice() {
    PowerHold why = POWER_HOLD_NONE;
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL,
        powerManualMode(POWER_ECO, POWER_ECO, true, 80, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_STOPWATCH, why);
    // Даже на исходе банки: правило то же, что у автоматики
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL,
        powerManualMode(POWER_ECO, POWER_ECO, true, 3, true, 15, 5, &why));
}

// Выбор пользователя не теряется — он возвращается, когда помеха отпала
void test_power_manual_restores_choice() {
    PowerHold why = POWER_HOLD_STOPWATCH;
    TEST_ASSERT_EQUAL_INT(POWER_ECO,
        powerManualMode(POWER_ECO, POWER_ECO, false, 80, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_NONE, why);
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL,
        powerManualMode(POWER_NORMAL, POWER_NORMAL, false, 80, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_NONE, why);
}

// Заряженная банка выпускает из выживания, кто бы его ни включил. Раньше
// ручная фиксация заряд не смотрела вовсе, и выход был только по сроку —
// то есть полная банка ничего не меняла.
void test_power_manual_survival_released_by_charge() {
    PowerHold why = POWER_HOLD_NONE;
    TEST_ASSERT_EQUAL_INT(POWER_ECO,
        powerManualMode(POWER_SURVIVAL, POWER_SURVIVAL, false, 100, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_CHARGED, why);
    TEST_ASSERT_EQUAL_INT(POWER_ECO,
        powerManualMode(POWER_SURVIVAL, POWER_SURVIVAL, false, 20, true, 15, 5, &why));
}

// Но не раньше порога с запасом: на 19 % выбор ещё в силе, иначе режим
// дребезжал бы на границе ровно там, где связь то появляется, то нет
void test_power_manual_survival_holds_inside_hysteresis() {
    PowerHold why = POWER_HOLD_NONE;
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_SURVIVAL, POWER_SURVIVAL, false, 19, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_NONE, why);   // это выбор, а не поправка
}

// Обратная дыра: зафиксированный обычный режим на исходе банки жёг радио
// до brownout — заряд его не трогал вовсе
void test_power_manual_low_charge_forces_survival() {
    PowerHold why = POWER_HOLD_NONE;
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_NORMAL, POWER_NORMAL, false, 10, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_LOW, why);
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_ECO, POWER_ECO, false, 15, true, 15, 5, &why));
}

// Без банки (питание от USB) вето не применяется: процентов просто нет
void test_power_manual_ignores_invalid_battery() {
    PowerHold why = POWER_HOLD_STOPWATCH;
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL,
        powerManualMode(POWER_NORMAL, POWER_NORMAL, false, 0, false, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_NONE, why);
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_SURVIVAL, POWER_SURVIVAL, false, 0, false, 15, 5, &why));
}

// Гистерезис считается от рабочего уровня, а не от выбранного. Зафиксированный
// обычный режим, опущенный в выживание на 15 %, не должен всплывать на 16-ти:
// сглаженное напряжение гуляет, и радио включалось бы и выключалось по кругу.
void test_power_manual_hysteresis_follows_current_mode() {
    PowerHold why = POWER_HOLD_NONE;
    // cur=survival (опустили по заряду), chosen=normal — держим до порога+запас
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_SURVIVAL, POWER_NORMAL, false, 16, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_LOW, why);
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_SURVIVAL, POWER_NORMAL, false, 19, true, 15, 5, &why));
    // 20 % — запас выбран, выбор возвращается
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL,
        powerManualMode(POWER_SURVIVAL, POWER_NORMAL, false, 20, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_NONE, why);
}

// Выпустив из выживания зарядом, обратно не заводим на первом же дрейфе вниз:
// вернуться туда можно только через сам порог, а не через его окрестность
void test_power_manual_release_is_sticky() {
    PowerHold why = POWER_HOLD_NONE;
    TEST_ASSERT_EQUAL_INT(POWER_ECO,
        powerManualMode(POWER_ECO, POWER_SURVIVAL, false, 19, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_CHARGED, why);
    // А вот ниже порога выбор снова в силе — он и был про исход заряда
    TEST_ASSERT_EQUAL_INT(POWER_SURVIVAL,
        powerManualMode(POWER_ECO, POWER_SURVIVAL, false, 15, true, 15, 5, &why));
    TEST_ASSERT_EQUAL_INT(POWER_HOLD_NONE, why);
}

// Причина уходит наружу строкой — дашборду нечем иначе объяснить несовпадение
void test_power_hold_names() {
    TEST_ASSERT_EQUAL_STRING("",          powerHoldName(POWER_HOLD_NONE));
    TEST_ASSERT_EQUAL_STRING("stopwatch", powerHoldName(POWER_HOLD_STOPWATCH));
    TEST_ASSERT_EQUAL_STRING("low",       powerHoldName(POWER_HOLD_LOW));
    TEST_ASSERT_EQUAL_STRING("charged",   powerHoldName(POWER_HOLD_CHARGED));
}

// why не обязателен — вызов без него должен собираться и считать так же
void test_power_manual_without_reason_out() {
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL,
        powerManualMode(POWER_ECO, POWER_ECO, true, 80, true, 15, 5));
}

// Экран под замером горит в любой час: профиль «обычный» окна не знает
void test_power_screen_on_at_night_while_stopwatch_runs() {
    PowerMode m = powerManualMode(POWER_ECO, POWER_ECO, true, 80, true, 15, 5);
    TEST_ASSERT_TRUE(powerScreenAllowed(m, 3, 6, 22));    // 03:00, ночь
    TEST_ASSERT_TRUE(powerScreenAllowed(m, 23, 6, 22));   // 23:00
    // Без замера тот же час экран гасит — правило именно про замер
    TEST_ASSERT_FALSE(powerScreenAllowed(
        powerManualMode(POWER_ECO, POWER_ECO, false, 80, true, 15, 5), 3, 6, 22));
}

void test_power_mode_from_name() {
    PowerMode m = POWER_NORMAL;
    TEST_ASSERT_TRUE(powerModeFromName("survival", &m));
    TEST_ASSERT_EQUAL(POWER_SURVIVAL, m);
    TEST_ASSERT_TRUE(powerModeFromName("eco", &m));
    TEST_ASSERT_EQUAL(POWER_ECO, m);
}

void test_power_mode_from_name_rejects_garbage() {
    PowerMode m = POWER_ECO;
    TEST_ASSERT_FALSE(powerModeFromName("turbo", &m));
    TEST_ASSERT_FALSE(powerModeFromName("", &m));
    TEST_ASSERT_FALSE(powerModeFromName(nullptr, &m));
    TEST_ASSERT_EQUAL(POWER_ECO, m);              // при отказе не трогаем
}

// ─── Метео: производные ───────────────────────────────────
void test_pressure_mmhg() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 760.0f, pressureToMmHg(1013.25f));
}

// На нулевой высоте QNH равен измеренному давлению
void test_qnh_at_sea_level() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, pressureToQnh(1000.0f, 15.0f, 0.0f));
}

// Выше над уровнем моря → приведённое давление больше измеренного
void test_qnh_above_sea_level() {
    TEST_ASSERT_TRUE(pressureToQnh(1000.0f, 15.0f, 100.0f) > 1000.0f);
}

// Стандартная атмосфера: 1013.25 гПа при 15 °C → 1.225 кг/м³
void test_air_density_standard() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.225f, airDensityOf(1013.25f, 15.0f));
}

void test_weather_plausible_ok() {
    TEST_ASSERT_TRUE(weatherPlausible(20.0f, 1013.0f));
    TEST_ASSERT_TRUE(weatherPlausible(-40.0f, 300.0f));
}

void test_weather_plausible_rejects_garbage() {
    TEST_ASSERT_FALSE(weatherPlausible(20.0f, 250.0f));
    TEST_ASSERT_FALSE(weatherPlausible(20.0f, 1300.0f));
    TEST_ASSERT_FALSE(weatherPlausible(NAN, 1013.0f));
    TEST_ASSERT_FALSE(weatherPlausible(20.0f, NAN));
}

// ─── Метео: история давления и тренд ──────────────────────
void test_history_empty_trend() {
    PressureHistory h;
    TEST_ASSERT_EQUAL_UINT8(0, h.count);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, h.trendPerHour());
}

// Одна точка — тренд считать не из чего
void test_history_single_point() {
    PressureHistory h;
    TEST_ASSERT_TRUE(h.maybePush(1013.0f, 0, 300000));
    TEST_ASSERT_EQUAL_UINT8(1, h.count);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, h.trendPerHour());
}

// Первая точка пишется всегда, следующая — только по истечении интервала
void test_history_respects_interval() {
    PressureHistory h;
    TEST_ASSERT_TRUE(h.maybePush(1013.0f, 1000, 300000));
    TEST_ASSERT_FALSE(h.maybePush(1014.0f, 100000, 300000));
    TEST_ASSERT_EQUAL_UINT8(1, h.count);
    TEST_ASSERT_TRUE(h.maybePush(1014.0f, 301000, 300000));
    TEST_ASSERT_EQUAL_UINT8(2, h.count);
}

void test_history_trend_rising() {
    PressureHistory h;
    h.maybePush(1000.0f, 0, 300000);
    h.maybePush(1001.0f, 3600000, 300000);   // +1 гПа за час
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, h.trendPerHour());
}

void test_history_trend_falling() {
    PressureHistory h;
    h.maybePush(1002.0f, 0, 300000);
    h.maybePush(1000.0f, 1800000, 300000);   // -2 гПа за полчаса → -4 гПа/ч
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -4.0f, h.trendPerHour());
}

// Слишком короткая база (< 1 с) — деление дало бы абсурдный наклон
void test_history_trend_needs_time_base() {
    PressureHistory h;
    h.maybePush(1000.0f, 0, 0);
    h.maybePush(1005.0f, 500, 0);
    TEST_ASSERT_EQUAL_UINT8(2, h.count);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, h.trendPerHour());
}

// Переполнение кольца: окно должно съезжать целиком, а самая старая
// точка — попадать в расчёт (был off-by-one, терявший её).
void test_history_wraps_full_window() {
    PressureHistory h;
    // Ступенька ровно на границе окна: давление прыгает после 3-й точки.
    // Линейный рост тут не годится — при сдвиге окна на единицу наклон
    // остался бы прежним, и off-by-one прошёл бы незамеченным.
    for (int i = 0; i < 15; i++)
        h.maybePush(i <= 3 ? 1000.0f : 1010.0f, (uint32_t)i * 60000, 0);   // раз в минуту

    TEST_ASSERT_EQUAL_UINT8(PRESSURE_HISTORY_SIZE, h.count);
    // Окно — точки i=3..14: +10 гПа за 11 минут ≈ 54.55 гПа/ч.
    // Потеря самой старой точки дала бы ровный 0.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 54.55f, h.trendPerHour());
}

// ─── Метео: прогноз ───────────────────────────────────────
void test_forecast_needs_history() {
    TEST_ASSERT_EQUAL_UINT8(0, forecastFromTrend(1.0f, 2));
}

void test_forecast_clear()    { TEST_ASSERT_EQUAL_UINT8(1, forecastFromTrend( 1.0f, 3)); }
void test_forecast_variable() { TEST_ASSERT_EQUAL_UINT8(2, forecastFromTrend( 0.0f, 12)); }
void test_forecast_rain()     { TEST_ASSERT_EQUAL_UINT8(3, forecastFromTrend(-1.0f, 5)); }

void test_forecast_boundaries() {
    TEST_ASSERT_EQUAL_UINT8(2, forecastFromTrend( 0.5f, 3));   // строго больше
    TEST_ASSERT_EQUAL_UINT8(2, forecastFromTrend(-0.5f, 3));   // строго меньше
    TEST_ASSERT_EQUAL_UINT8(1, forecastFromTrend( 0.51f, 3));
    TEST_ASSERT_EQUAL_UINT8(3, forecastFromTrend(-0.51f, 3));
}

// ─── Origin изменяющих запросов ───────────────────────────
static const char* IP   = "192.168.1.42";
static const char* HOST = "clock";

void test_origin_own_mdns_name() {
    TEST_ASSERT_TRUE(originIsLocalDevice("http://clock.local", IP, HOST));
}

void test_origin_own_ip() {
    TEST_ASSERT_TRUE(originIsLocalDevice("http://192.168.1.42", IP, HOST));
}

// Браузер порт по умолчанию не пишет, но явный :80 — тот же самый origin
void test_origin_explicit_port_80() {
    TEST_ASSERT_TRUE(originIsLocalDevice("http://clock.local:80", IP, HOST));
    TEST_ASSERT_TRUE(originIsLocalDevice("http://192.168.1.42:80", IP, HOST));
}

void test_origin_hostname_without_suffix() {
    TEST_ASSERT_TRUE(originIsLocalDevice("http://clock", IP, HOST));
}

// Регистр хоста значения не имеет
void test_origin_case_insensitive_host() {
    TEST_ASSERT_TRUE(originIsLocalDevice("http://CLOCK.local", IP, HOST));
}

void test_origin_foreign_site() {
    TEST_ASSERT_FALSE(originIsLocalDevice("http://evil.com", IP, HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice("https://evil.com", IP, HOST));
}

// Главная ловушка: сравнивать хост по префиксу нельзя
void test_origin_rejects_suffix_trick() {
    TEST_ASSERT_FALSE(originIsLocalDevice("http://clock.local.evil.com", IP, HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice("http://192.168.1.42.evil.com", IP, HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice("http://notclock.local", IP, HOST));
}

// Соседнее устройство в той же сети — тоже чужой origin
void test_origin_rejects_neighbour_ip() {
    TEST_ASSERT_FALSE(originIsLocalDevice("http://192.168.1.43", IP, HOST));
}

// https нам взяться неоткуда: сертификата у устройства нет
void test_origin_rejects_https_own_name() {
    TEST_ASSERT_FALSE(originIsLocalDevice("https://clock.local", IP, HOST));
}

// Песочница iframe и file:// шлют строку "null"
void test_origin_rejects_null_literal() {
    TEST_ASSERT_FALSE(originIsLocalDevice("null", IP, HOST));
}

void test_origin_rejects_empty_and_garbage() {
    TEST_ASSERT_FALSE(originIsLocalDevice("", IP, HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice(nullptr, IP, HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice("http://", IP, HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice("clock.local", IP, HOST));
}

// Другой порт — по правилам origin это другой источник
void test_origin_rejects_other_port() {
    TEST_ASSERT_FALSE(originIsLocalDevice("http://clock.local:8080", IP, HOST));
}

// До подключения к сети IP ещё пуст — имя всё равно должно работать,
// а пустая строка не должна совпадать со всем подряд
void test_origin_without_known_ip() {
    TEST_ASSERT_TRUE(originIsLocalDevice("http://clock.local", "", HOST));
    TEST_ASSERT_FALSE(originIsLocalDevice("http://192.168.1.42", "", HOST));
}

// Слишком длинный хост не должен переполнить буфер разбора
void test_origin_rejects_overlong_host() {
    char origin[200];
    snprintf(origin, sizeof(origin), "http://%0*d", 150, 0);
    TEST_ASSERT_FALSE(originIsLocalDevice(origin, IP, HOST));
}

// ─── JSON ─────────────────────────────────────────────────
void test_json_contains_time_key() {
    const char* json = "{\"time\":\"12:00:00\",\"date\":\"10 MAY 2026\"}";
    TEST_ASSERT_TRUE(jsonContainsKey(json, "\"time\""));
}

void test_json_contains_date_key() {
    const char* json = "{\"time\":\"12:00:00\",\"date\":\"10 MAY 2026\"}";
    TEST_ASSERT_TRUE(jsonContainsKey(json, "\"date\""));
}

void test_json_missing_key() {
    const char* json = "{\"time\":\"12:00:00\"}";
    TEST_ASSERT_FALSE(jsonContainsKey(json, "\"cpu\""));
}

// ─── Runner ───────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_uptime_seconds_only);
    RUN_TEST(test_uptime_minutes);
    RUN_TEST(test_uptime_hours);
    RUN_TEST(test_uptime_days);
    RUN_TEST(test_uptime_large);

    RUN_TEST(test_brightness_night_22);
    RUN_TEST(test_brightness_night_3);
    RUN_TEST(test_brightness_morning);
    RUN_TEST(test_brightness_day);
    RUN_TEST(test_brightness_evening);
    RUN_TEST(test_brightness_levels_keep_pre_gamma_output);
    RUN_TEST(test_min_visible_level_is_actually_visible);
    RUN_TEST(test_brightness_no_time_is_not_full_scale);
    RUN_TEST(test_brightness_boundary_6);
    RUN_TEST(test_brightness_boundary_8);
    RUN_TEST(test_brightness_boundary_20);
    RUN_TEST(test_brightness_pct_full);
    RUN_TEST(test_brightness_pct_night);
    RUN_TEST(test_brightness_pct_zero);
    RUN_TEST(test_pct_zero_stays_off);
    RUN_TEST(test_pct_contrast_roundtrip_is_stable);

    RUN_TEST(test_format_time_normal);
    RUN_TEST(test_format_time_midnight);
    RUN_TEST(test_format_time_max);

    RUN_TEST(test_format_date);
    RUN_TEST(test_format_date_single_digit_day);

    RUN_TEST(test_rssi_excellent);
    RUN_TEST(test_rssi_good);
    RUN_TEST(test_rssi_fair);
    RUN_TEST(test_rssi_poor);
    RUN_TEST(test_rssi_boundary_50);
    RUN_TEST(test_rssi_boundary_51);

    RUN_TEST(test_stopwatch_zero);
    RUN_TEST(test_stopwatch_milliseconds);
    RUN_TEST(test_stopwatch_minutes);
    RUN_TEST(test_stopwatch_before_hour);
    RUN_TEST(test_stopwatch_exactly_hour);
    RUN_TEST(test_stopwatch_hours);
    RUN_TEST(test_stopwatch_many_hours);
    RUN_TEST(test_stopwatch_split_layout);

    RUN_TEST(test_sw_starts_idle);
    RUN_TEST(test_sw_runs_from_start_moment);
    RUN_TEST(test_sw_restart_is_noop);
    RUN_TEST(test_sw_pause_freezes);
    RUN_TEST(test_sw_pause_when_not_running);
    RUN_TEST(test_sw_resume_accumulates);
    RUN_TEST(test_sw_reset_clears);
    RUN_TEST(test_sw_reset_while_running);
    RUN_TEST(test_sw_survives_millis_overflow);
    RUN_TEST(test_sw_gen_survives_pause_resume);
    RUN_TEST(test_sw_gen_unchanged_on_restart_noop);
    RUN_TEST(test_sw_gen_changes_on_new_run);
    RUN_TEST(test_sw_gen_is_unique_per_run);
    RUN_TEST(test_sw_state_codes_are_stable);

    RUN_TEST(test_battery_full);
    RUN_TEST(test_battery_above_full);
    RUN_TEST(test_battery_empty);
    RUN_TEST(test_battery_below_empty);
    RUN_TEST(test_battery_curve_point);
    RUN_TEST(test_battery_curve_tail_matches_cutoff);
    RUN_TEST(test_battery_tail_is_not_steeper_than_adc_noise);
    RUN_TEST(test_battery_interpolation);
    RUN_TEST(test_battery_thresholds_land_where_intended);
    RUN_TEST(test_battery_monotonic);
    RUN_TEST(test_battery_mah_scale_is_usable_capacity);
    RUN_TEST(test_battery_mah_clamps_above_full);
    RUN_TEST(test_battery_plausible_usb);
    RUN_TEST(test_battery_plausible_battery);
    RUN_TEST(test_battery_plausible_full_with_calibration);
    RUN_TEST(test_battery_percent_saturates_above_curve);

    RUN_TEST(test_battery_median_odd);
    RUN_TEST(test_battery_median_even);
    RUN_TEST(test_battery_median_sorts_in_place);
    RUN_TEST(test_battery_median_empty);
    RUN_TEST(test_battery_median_ignores_minority_dips);
    RUN_TEST(test_battery_smooth_pulls_toward_fresh);
    RUN_TEST(test_battery_smooth_zero_alpha_holds);
    RUN_TEST(test_battery_smooth_suppresses_single_spike);
    RUN_TEST(test_battery_jump_detects_usb_unplug);
    RUN_TEST(test_battery_jump_ignores_noise);

    RUN_TEST(test_drive_full_scale);
    RUN_TEST(test_drive_bottom_step_is_far_below_contrast_floor);
    RUN_TEST(test_drive_is_monotonic);
    RUN_TEST(test_drive_midpoint_is_gamma_corrected);
    RUN_TEST(test_drive_registers_in_range);

    RUN_TEST(test_auto_idle_is_eco);
    RUN_TEST(test_auto_stopwatch_raises_to_normal);
    RUN_TEST(test_auto_stopwatch_beats_low_charge);
    RUN_TEST(test_auto_low_charge_is_survival);
    RUN_TEST(test_auto_charge_hysteresis);
    RUN_TEST(test_auto_usb_ignores_charge_rule);
    RUN_TEST(test_auto_night_stays_eco);
    RUN_TEST(test_power_profile_names);
    RUN_TEST(test_power_profile_tightens_with_level);
    RUN_TEST(test_power_profile_bad_index_is_normal);
    RUN_TEST(test_window_plain);
    RUN_TEST(test_window_over_midnight);
    RUN_TEST(test_window_whole_day);
    RUN_TEST(test_power_screen_normal_always_on);
    RUN_TEST(test_power_screen_eco_follows_window);
    RUN_TEST(test_power_screen_off_on_critical_charge);
    RUN_TEST(test_power_screen_on_above_critical);
    RUN_TEST(test_power_screen_ignores_critical_without_battery);
    RUN_TEST(test_power_screen_battery_gate_needs_no_hour);
    RUN_TEST(test_power_screen_critical_and_schedule_combine);
    RUN_TEST(test_power_manual_stopwatch_beats_choice);
    RUN_TEST(test_power_manual_restores_choice);
    RUN_TEST(test_power_manual_survival_released_by_charge);
    RUN_TEST(test_power_manual_survival_holds_inside_hysteresis);
    RUN_TEST(test_power_manual_low_charge_forces_survival);
    RUN_TEST(test_power_manual_ignores_invalid_battery);
    RUN_TEST(test_power_manual_hysteresis_follows_current_mode);
    RUN_TEST(test_power_manual_release_is_sticky);
    RUN_TEST(test_power_hold_names);
    RUN_TEST(test_power_manual_without_reason_out);
    RUN_TEST(test_power_screen_on_at_night_while_stopwatch_runs);
    RUN_TEST(test_power_mode_from_name);
    RUN_TEST(test_power_mode_from_name_rejects_garbage);

    RUN_TEST(test_pressure_mmhg);
    RUN_TEST(test_qnh_at_sea_level);
    RUN_TEST(test_qnh_above_sea_level);
    RUN_TEST(test_air_density_standard);
    RUN_TEST(test_weather_plausible_ok);
    RUN_TEST(test_weather_plausible_rejects_garbage);

    RUN_TEST(test_history_empty_trend);
    RUN_TEST(test_history_single_point);
    RUN_TEST(test_history_respects_interval);
    RUN_TEST(test_history_trend_rising);
    RUN_TEST(test_history_trend_falling);
    RUN_TEST(test_history_trend_needs_time_base);
    RUN_TEST(test_history_wraps_full_window);

    RUN_TEST(test_forecast_needs_history);
    RUN_TEST(test_forecast_clear);
    RUN_TEST(test_forecast_variable);
    RUN_TEST(test_forecast_rain);
    RUN_TEST(test_forecast_boundaries);

    RUN_TEST(test_origin_own_mdns_name);
    RUN_TEST(test_origin_own_ip);
    RUN_TEST(test_origin_explicit_port_80);
    RUN_TEST(test_origin_hostname_without_suffix);
    RUN_TEST(test_origin_case_insensitive_host);
    RUN_TEST(test_origin_foreign_site);
    RUN_TEST(test_origin_rejects_suffix_trick);
    RUN_TEST(test_origin_rejects_neighbour_ip);
    RUN_TEST(test_origin_rejects_https_own_name);
    RUN_TEST(test_origin_rejects_null_literal);
    RUN_TEST(test_origin_rejects_empty_and_garbage);
    RUN_TEST(test_origin_rejects_other_port);
    RUN_TEST(test_origin_without_known_ip);
    RUN_TEST(test_origin_rejects_overlong_host);

    RUN_TEST(test_json_contains_time_key);
    RUN_TEST(test_json_contains_date_key);
    RUN_TEST(test_json_missing_key);

    return UNITY_END();
}
