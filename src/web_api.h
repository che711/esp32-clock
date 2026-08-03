#pragma once
#include <stdint.h>

// ============================================================
//  web_api.h — HTTP (:80) и WebSocket (:81).
//
//  Модуль отдаёт наружу состояние из app.h и принимает команды
//  яркости, питания экрана и секундомера.
// ============================================================

void webApiBegin();       // маршруты + запуск обоих серверов
void webApiLoop();        // обслуживание клиентов, вызывать из loop()
void webApiBroadcast();   // разослать снимок состояния по WebSocket

uint8_t  webApiClientCount();
uint32_t webApiRequestCount();
