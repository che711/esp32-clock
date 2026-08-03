#include <unity.h>
#include <string.h>
#include <math.h>
#include "../../src/clock_utils.h"
#include "../../src/battery_calc.h"
#include "../../src/weather_calc.h"
#include "../../src/stopwatch.h"

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
    TEST_ASSERT_EQUAL_UINT8(15, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Night", b.label);
}

void test_brightness_night_3() {
    BrightnessLevel b = brightnessForHour(3);
    TEST_ASSERT_EQUAL_UINT8(15, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Night", b.label);
}

void test_brightness_morning() {
    BrightnessLevel b = brightnessForHour(7);
    TEST_ASSERT_EQUAL_UINT8(80, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Morning", b.label);
}

void test_brightness_day() {
    BrightnessLevel b = brightnessForHour(12);
    TEST_ASSERT_EQUAL_UINT8(200, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Day", b.label);
}

void test_brightness_evening() {
    BrightnessLevel b = brightnessForHour(21);
    TEST_ASSERT_EQUAL_UINT8(120, b.contrast);
    TEST_ASSERT_EQUAL_STRING("Evening", b.label);
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
    TEST_ASSERT_EQUAL_UINT8(5, brightnessPct(15));
}

void test_brightness_pct_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, brightnessPct(0));
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

// Середина отрезка 3.68В(10%)…3.74В(20%) — проверяем интерполяцию
void test_battery_interpolation() {
    TEST_ASSERT_UINT8_WITHIN(1, 15, batteryVoltageToPercent(3.71f));
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

void test_battery_plausible_usb() {
    TEST_ASSERT_FALSE(batteryVoltagePlausible(0.0f));    // вход висит в воздухе
    TEST_ASSERT_FALSE(batteryVoltagePlausible(2.79f));
    TEST_ASSERT_FALSE(batteryVoltagePlausible(4.36f));
}

void test_battery_plausible_battery() {
    TEST_ASSERT_TRUE(batteryVoltagePlausible(2.80f));
    TEST_ASSERT_TRUE(batteryVoltagePlausible(3.70f));
    TEST_ASSERT_TRUE(batteryVoltagePlausible(4.35f));
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
    RUN_TEST(test_brightness_boundary_6);
    RUN_TEST(test_brightness_boundary_8);
    RUN_TEST(test_brightness_boundary_20);
    RUN_TEST(test_brightness_pct_full);
    RUN_TEST(test_brightness_pct_night);
    RUN_TEST(test_brightness_pct_zero);

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
    RUN_TEST(test_sw_state_codes_are_stable);

    RUN_TEST(test_battery_full);
    RUN_TEST(test_battery_above_full);
    RUN_TEST(test_battery_empty);
    RUN_TEST(test_battery_below_empty);
    RUN_TEST(test_battery_curve_point);
    RUN_TEST(test_battery_interpolation);
    RUN_TEST(test_battery_monotonic);
    RUN_TEST(test_battery_plausible_usb);
    RUN_TEST(test_battery_plausible_battery);

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

    RUN_TEST(test_json_contains_time_key);
    RUN_TEST(test_json_contains_date_key);
    RUN_TEST(test_json_missing_key);

    return UNITY_END();
}
