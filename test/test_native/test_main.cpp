#include <unity.h>
#include <string.h>
#include "../../src/clock_utils.h"

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

    RUN_TEST(test_json_contains_time_key);
    RUN_TEST(test_json_contains_date_key);
    RUN_TEST(test_json_missing_key);

    return UNITY_END();
}
