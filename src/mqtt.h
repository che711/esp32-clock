#pragma once
#include "sensor.h"
#include "battery.h"

// ============================================================
//  mqtt.h  —  публикация данных через PubSubClient
// ============================================================

void mqttInit();
void mqttLoop(const SensorData& data, const BatteryData& bat);  // вызывать в loop()
