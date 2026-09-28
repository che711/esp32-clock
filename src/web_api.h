#pragma once
#include <stdint.h>

// ============================================================
//  web_api.h — HTTP и WebSocket, оба на :80 (WebSocket — /ws).
//
//  Модуль отдаёт наружу состояние из app.h и принимает команды
//  яркости, питания экрана и секундомера. Сервер работает в своей
//  задаче; состояние из app.h трогает только под appLock().
// ============================================================

void webApiBegin();       // маршруты + запуск сервера
void webApiLoop();        // heartbeat WebSocket, вызывать из loop()
void webApiBroadcast();   // разослать снимок по WebSocket; вызывать под appLock()

uint8_t  webApiClientCount();
