#pragma once

// ============================================================
//  config.h — все настройки объединённого проекта
//  «Часы + метеостанция» на ESP32-C6-Zero
//
//  ┌─ КАРТА ПИНОВ ESP32-C6-Zero ──────────────────────────┐
//  │ GPIO2  → Battery ADC    (делитель 18650, ADC1_CH2)    │
//  │ GPIO4  → BMP280 SDA     (strapping-пин, для I²C ок)   │
//  │ GPIO5  → BMP280 SCL     (strapping-пин, для I²C ок)   │
//  │ GPIO8  → WS2812 RGB LED (встроенный, не трогать)      │
//  │ ── OLED SSD1322 (hardware SPI) ──                     │
//  │ GPIO18 → CLK   GPIO19 → DIN                           │
//  │ GPIO20 → CS    GPIO21 → DC    GPIO22 → RST            │
//  └──────────────────────────────────────────────────────┘
//
//  ВАЖНО — у C6-Zero пины двух сортов, они НЕ равноценны:
//    • Боковые кастеллированные (сюда паяется гребёнка):
//        левый ряд  5V GND 3V3 | 0 1 2 3 4 5
//        правый ряд TX RX | 14 15 18 19 20 21 22
//    • Пятачки на ОБРАТНОЙ стороне платы (провод тянуть под неё):
//        6 7 8 9 12 13 23
//  Поэтому OLED сидит на правом боковом ряду (18..22), а не на 6/7:
//  SPI — самые быстрые линии, тянуть их с изнанки не надо.
//
//  Занято намертво: 8 (WS2812), 9 (кнопка BOOT),
//                   12/13 (USB D-/D+, у нас USB CDC), TX/RX (UART0).
//  Strapping-пины C6: 4,5,8,9,15 — дисплеем не занимать.
//  ADC есть ТОЛЬКО на GPIO0..6 — отсюда батарея на 2.
//  Свободный резерв: 0,1,3,14 (боковые) + 6,7,23 (изнанка).
// ============================================================

// ── WiFi ────────────────────────────────────────────────────
#define WIFI_SSID       "networok"
#define WIFI_PASSWORD   "password"

// Hostname (mDNS: http://clock.local)
#define DEVICE_HOSTNAME "clock"

// ── Время / часовой пояс ────────────────────────────────────
#define NTP_SERVER      "pool.ntp.org"
// POSIX-TZ (авто-переход зима/лето). Варшава/Центральная Европа.
// London: "GMT0BST,M3.5.0/1,M10.5.0" | Kyiv: "EET-2EEST,M3.5.0/3,M10.5.0/4" | UTC: "UTC0"
#define TZ_INFO         "CET-1CEST,M3.5.0,M10.5.0/3"

// ── OLED SSD1322 256x64 (hardware SPI) ──────────────────────
// Пока панель не подпаяна — HAS_DISPLAY 0 (не гонять SPI вслепую).
// Все пять пинов — правый боковой ряд платы, паяются одним шлейфом.
#define HAS_DISPLAY     1
#define OLED_CLK_PIN    18
#define OLED_DIN_PIN    19
#define OLED_CS_PIN     20
#define OLED_DC_PIN     21
#define OLED_RST_PIN    22

// ── BMP280 (I²C) ────────────────────────────────────────────
#define BMP280_SDA_PIN  4
#define BMP280_SCL_PIN  5
#define BMP280_I2C_ADDR 0x76        // 0x76: SDO→GND | 0x77: SDO→VCC
#define SEA_LEVEL_HPA   1013.25f    // для расчёта высоты

// ── Батарея 18650 ────────────────────────────────────────────
//   BAT+ ──[100к]──●──[100к]── GND, точка ● → GPIO2 (ADC1).
#define BATTERY_ENABLED   true
#define BATTERY_ADC_PIN   2
#define BATTERY_DIVIDER   2.0f      // (R1+R2)/R2
#define BATTERY_CAL       1.0f      // V_мультиметр / V_показанное
#define BATTERY_SAMPLES   16
#define BATTERY_LOW_V     3.30f

// ── Onboard WS2812 RGB LED ───────────────────────────────────
#define LED_PIN         8

// ── MQTT (Home Assistant / Mosquitto) ───────────────────────
#define MQTT_ENABLED   false        // false — MQTT отключён полностью
#define MQTT_SERVER    "192.168.1.10"
#define MQTT_PORT      1883
#define MQTT_USER      ""
#define MQTT_PASSWORD  ""
#define MQTT_CLIENT_ID "esp32c6-clock-weather"
#define MQTT_HA_DISCOVERY true

#define MQTT_TOPIC_STATE    "clock/weather/state"
#define MQTT_TOPIC_TEMP     "clock/weather/temperature"
#define MQTT_TOPIC_PRESSURE "clock/weather/pressure"
#define MQTT_TOPIC_ALTITUDE "clock/weather/altitude"
#define MQTT_TOPIC_BATTERY  "clock/weather/battery"
#define MQTT_TOPIC_BAT_V    "clock/weather/battery_voltage"
#define MQTT_TOPIC_STATUS   "clock/weather/status"

// ── Интервалы ────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS   10000UL  // опрос BMP280
#define SW_DRAW_INTERVAL_MS    200UL  // перерисовка OLED при работе секундомера
#define MQTT_INTERVAL_MS     30000UL  // публикация MQTT (≥ SENSOR_INTERVAL)
#define MQTT_RECONNECT_MS     5000UL
#define PRESSURE_HISTORY_INTERVAL_MS 300000UL  // точка истории давления (5 мин)
