#include "sysinfo.h"
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================
//  sysinfo.cpp  —  системные метрики + измерение загрузки CPU
//
//  CPU load: низкоприоритетная задача крутит счётчик короткими
//  «вспышками» по 100 мс. Чем меньше итераций относительно
//  калибровки — тем выше загрузка.
//
//  ВАЖНО: между вспышками обязателен vTaskDelay() — иначе
//  задача с приоритетом 1 навсегда вытесняет idle-задачу
//  (приоритет 0), idle не кормит task watchdog, и C6 уходит
//  в ребут по WDT. taskYIELD() уступает только равным и выше.
// ============================================================

static volatile uint8_t  _cpuLoad    = 0;
static volatile uint32_t _calibrated = 0;  // итераций за вспышку при idle

static void cpuMonitorTask(void* pv) {
    const uint32_t BURST_MS = 100;   // окно измерения
    const uint32_t REST_MS  = 400;   // пауза — idle-задача дышит, WDT сыт

    // Даём системе доинициализироваться перед калибровкой
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Калибровка: сколько итераций во «вспышке» при простое
    uint32_t t = millis();
    uint32_t cal = 0;
    while (millis() - t < BURST_MS) { cal++; taskYIELD(); }
    _calibrated = cal ? cal : 1;

    // Основной цикл
    while (true) {
        t = millis();
        uint32_t cnt = 0;
        while (millis() - t < BURST_MS) { cnt++; taskYIELD(); }

        uint8_t load = 0;
        if (cnt < _calibrated) {
            load = (uint8_t)(100UL - (cnt * 100UL / _calibrated));
        }
        // Экспоненциальное сглаживание, чтобы цифра не дёргалась
        _cpuLoad = (uint8_t)((_cpuLoad * 3 + load) / 4);

        vTaskDelay(pdMS_TO_TICKS(REST_MS));
    }
}

void sysInfoInit() {
    // Стек 1024 байта был на грани (FreeRTOS + millis + локальные
    // переменные) — 2048 с запасом.
    xTaskCreate(cpuMonitorTask, "cpu_mon", 2048, nullptr, 1, nullptr);
    Serial.println("[SysInfo] CPU monitor task started.");
}

SysInfo sysInfoRead() {
    SysInfo s;
    s.chipTempC    = temperatureRead();
    s.freeHeap     = ESP.getFreeHeap();
    s.totalHeap    = ESP.getHeapSize();
    s.heapUsedPct  = (uint8_t)(100 - (s.freeHeap * 100UL / s.totalHeap));
    s.cpuLoadPct   = _cpuLoad;
    s.cpuFreqMHz   = ESP.getCpuFreqMHz();
    s.rssi         = WiFi.RSSI();
    s.rssiPct      = (uint8_t)constrain(2 * (s.rssi + 100), 0, 100);
    s.ssid         = WiFi.SSID();
    s.ip           = WiFi.localIP().toString();
    s.mac          = WiFi.macAddress();
    s.uptimeS      = millis() / 1000UL;
    return s;
}

const char* rssiQuality(int32_t rssi) {
    if (rssi >= -55) return "Отлично";
    if (rssi >= -67) return "Хорошо";
    if (rssi >= -80) return "Слабый";
    return "Критический";
}
