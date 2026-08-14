#pragma once

// ============================================================
//  config.h — все настройки проекта «Часы + метеостанция».
//  Разводка и обоснование выбора пинов — в README.
//  Не занимать: 6,7,8,9,12,13,23 (не на гребёнках),
//  12/13 (USB), 16/17 (UART0), 4,5,8,9,15 (strapping).
// ============================================================

// ── WiFi ────────────────────────────────────────────────────
#define WIFI_SSID       "SkyNet"
#define WIFI_PASSWORD   "password"

// Hostname (mDNS: http://clock.local)
#define DEVICE_HOSTNAME "clock"

// ── Время / часовой пояс ────────────────────────────────────
#define NTP_SERVER      "pool.ntp.org"
// POSIX-TZ (авто-переход зима/лето). Варшава/Центральная Европа.
// London: "GMT0BST,M3.5.0/1,M10.5.0" | Kyiv: "EET-2EEST,M3.5.0/3,M10.5.0/4" | UTC: "UTC0"
#define TZ_INFO         "CET-1CEST,M3.5.0,M10.5.0/3"

// ── OLED SSD1322 256x64 (hardware SPI) ──────────────────────
#define HAS_DISPLAY     1       // 0, пока панель не подпаяна
#define OLED_CLK_PIN    18
#define OLED_DIN_PIN    19
#define OLED_DC_PIN     20
#define OLED_RST_PIN    21
#define OLED_CS_PIN     22

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
#define SW_DRAW_INTERVAL_MS     40UL  // кадр секундомера, 25 fps

// NTP: рутинный ре-синк и частый ретрай, пока часы не встали.
// Ретрай короткий намеренно — пока синка нет, на экране прочерки вместо
// времени, и это единственный путь из них выбраться.
#define NTP_RESYNC_MS   (6UL * 3600UL * 1000UL)
#define NTP_RETRY_MS         60000UL

// Пауза в конце loop(). Задаёт задержку отклика на HTTP-запрос и на
// команду из браузера: пакет ждёт следующего оборота цикла.
// 20 мс вместо прежних 100 — заметно живее веб-интерфейс, а расход
// не меняется: тик FreeRTOS всё равно идёт каждую миллисекунду,
// и пустой оборот стоит десятки микросекунд против радио WiFi.
#define LOOP_IDLE_MS            20UL
#define LOOP_STOPWATCH_MS        5UL  // на ходу — короче, счётчик тикает 25 fps
#define MQTT_INTERVAL_MS     30000UL  // публикация MQTT (≥ SENSOR_INTERVAL)
#define MQTT_RECONNECT_MS     5000UL
#define PRESSURE_HISTORY_INTERVAL_MS 300000UL  // точка истории давления (5 мин)
