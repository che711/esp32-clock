#include "mqtt.h"
#include "config.h"

// ============================================================
//  mqtt.cpp  —  MQTT через PubSubClient
//  + Home Assistant MQTT Discovery (автосоздание сенсоров)
// ============================================================

#if MQTT_ENABLED

#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient   wifiClient;
static PubSubClient mqttClient(wifiClient);

static unsigned long _lastPublish   = 0;
static unsigned long _lastReconnect = 0;

// ── Home Assistant Discovery ──────────────────────────────────
#if MQTT_HA_DISCOVERY
// Общий блок устройства — HA сгруппирует все сущности в одну карточку
static const char* DEV_JSON =
    "\"dev\":{\"ids\":[\"" MQTT_CLIENT_ID "\"],"
    "\"name\":\"Метеостанция\","
    "\"mdl\":\"ESP32-C6-Zero + BMP280\","
    "\"mf\":\"DIY\"}";

static void publishDiscoveryOne(const char* key, const char* name,
                                const char* devClass, const char* unit,
                                const char* stateTopic) {
    char topic[96];
    snprintf(topic, sizeof(topic),
             "homeassistant/sensor/" MQTT_CLIENT_ID "/%s/config", key);

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\","
             "\"uniq_id\":\"" MQTT_CLIENT_ID "_%s\","
             "\"stat_t\":\"%s\","
             "\"unit_of_meas\":\"%s\","
             "\"dev_cla\":\"%s\","
             "\"stat_cla\":\"measurement\","
             "\"avty_t\":\"" MQTT_TOPIC_STATUS "\","
             "%s}",
             name, key, stateTopic, unit, devClass, DEV_JSON);

    mqttClient.publish(topic, payload, true);  // retain
}

static void publishDiscovery() {
    publishDiscoveryOne("temperature", "Температура", "temperature", "°C",
                        MQTT_TOPIC_TEMP);
    publishDiscoveryOne("pressure", "Давление", "pressure", "hPa",
                        MQTT_TOPIC_PRESSURE);
    publishDiscoveryOne("qnh", "Давление (QNH)", "pressure", "hPa",
                        MQTT_TOPIC_QNH);
#if BATTERY_ENABLED
    publishDiscoveryOne("battery", "Батарея", "battery", "%",
                        MQTT_TOPIC_BATTERY);
    publishDiscoveryOne("battery_v", "Напряжение АКБ", "voltage", "V",
                        MQTT_TOPIC_BAT_V);
#endif
    Serial.println("[MQTT] HA Discovery опубликован.");
}
#endif  // MQTT_HA_DISCOVERY

// ── Реконнект (неблокирующий) ─────────────────────────────────
static bool mqttReconnect() {
    if (mqttClient.connected()) return true;

    unsigned long now = millis();
    if (now - _lastReconnect < MQTT_RECONNECT_MS) return false;
    _lastReconnect = now;

    Serial.printf("[MQTT] Подключение к %s:%d ...\n", MQTT_SERVER, MQTT_PORT);

    bool ok;
    if (strlen(MQTT_USER) > 0) {
        ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD,
                                MQTT_TOPIC_STATUS, 0, true, "offline");
    } else {
        ok = mqttClient.connect(MQTT_CLIENT_ID, nullptr, nullptr,
                                MQTT_TOPIC_STATUS, 0, true, "offline");
    }

    if (ok) {
        mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
        Serial.println("[MQTT] Подключено.");
#if MQTT_HA_DISCOVERY
        publishDiscovery();
#endif
    } else {
        Serial.printf("[MQTT] Ошибка rc=%d, повтор через %lu с\n",
                      mqttClient.state(), MQTT_RECONNECT_MS / 1000);
    }
    return ok;
}

// ── Публикация JSON + отдельные топики ───────────────────────
static void publishData(const SensorData& d, const BatteryData& bat) {
    char buf[192];

    // Отдельные топики (удобно для Home Assistant sensors)
    snprintf(buf, sizeof(buf), "%.2f", d.temperature);
    mqttClient.publish(MQTT_TOPIC_TEMP, buf, false);

    snprintf(buf, sizeof(buf), "%.2f", d.pressure);
    mqttClient.publish(MQTT_TOPIC_PRESSURE, buf, false);

    // Публикуем QNH, а не высоту: высота — константа из config.h, слать её
    // каждый цикл незачем, а приведённое к морю давление как раз меняется
    // и сходится с METAR ближайшего аэропорта.
    snprintf(buf, sizeof(buf), "%.2f", d.pressureQnh);
    mqttClient.publish(MQTT_TOPIC_QNH, buf, false);

#if BATTERY_ENABLED
    if (bat.valid) {
        snprintf(buf, sizeof(buf), "%u", bat.percent);
        mqttClient.publish(MQTT_TOPIC_BATTERY, buf, false);
        snprintf(buf, sizeof(buf), "%.2f", bat.voltage);
        mqttClient.publish(MQTT_TOPIC_BAT_V, buf, false);
    }
#endif

    // JSON payload в один топик (удобно для Node-RED / InfluxDB)
    if (bat.valid) {
        snprintf(buf, sizeof(buf),
                 "{\"temperature\":%.2f,\"pressure\":%.2f,\"qnh\":%.2f,"
                 "\"battery\":%u,\"battery_v\":%.2f}",
                 d.temperature, d.pressure, d.pressureQnh,
                 bat.percent, bat.voltage);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"temperature\":%.2f,\"pressure\":%.2f,\"qnh\":%.2f}",
                 d.temperature, d.pressure, d.pressureQnh);
    }
    mqttClient.publish(MQTT_TOPIC_STATE, buf, false);

    Serial.printf("[MQTT] Опубликовано: T=%.2f°C P=%.2fгПа QNH=%.2fгПа\n",
                  d.temperature, d.pressure, d.pressureQnh);
}

// ── Публичный API ─────────────────────────────────────────────

void mqttInit() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setKeepAlive(60);
    mqttClient.setBufferSize(768);  // discovery-payload крупнее данных
    Serial.println("[MQTT] Клиент настроен.");
}

void mqttLoop(const SensorData& data, const BatteryData& bat) {
    if (!mqttReconnect()) return;
    mqttClient.loop();  // обслуживание keep-alive

    if (!data.valid) return;

    unsigned long now = millis();
    if (now - _lastPublish >= MQTT_INTERVAL_MS) {
        _lastPublish = now;
        publishData(data, bat);
    }
}

#endif  // MQTT_ENABLED
