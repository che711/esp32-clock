#pragma once
#include <Arduino.h>

struct SysInfo {
    float    chipTempC;
    uint32_t freeHeap;
    uint32_t totalHeap;
    uint8_t  heapUsedPct;
    uint8_t  cpuLoadPct;     // загрузка CPU, %
    uint32_t cpuFreqMHz;
    int32_t  rssi;
    uint8_t  rssiPct;
    String   ssid;
    String   ip;
    String   mac;
    uint32_t uptimeS;
};

void    sysInfoInit();                   // вызвать в setup() — запускает задачу CPU
SysInfo sysInfoRead();
const char* rssiQuality(int32_t rssi);
