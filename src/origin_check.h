#pragma once
#include <string.h>

// ============================================================
//  origin_check.h — свой ли Origin у изменяющего запроса.
//
//  Заголовок Origin проставляет сам браузер, и подделать его со
//  страницы нельзя. Поэтому запрос с чужой вкладки («открыл сайт
//  с рекламой — со страницы улетел POST /api/reboot») отличается
//  от запроса своего дашборда ровно этим полем.
//
//  Без Arduino: собирается нативно и покрыто тестами.
// ============================================================

// Дашборд открывают только по IPv4: http://<IP устройства>, поэтому и Origin
// у него такой же. Всё остальное — чужая страница.
//
// Имён здесь больше нет. Раньше принимались "clock" и "clock.local" (mDNS), а
// потом и список имён от роутера, — но mDNS в сети так и не заработал, и его
// убрали из прошивки целиком. Имя, которое устройство не объявляет, в Origin
// попасть может только от чужого DNS, так что держать его в списке «своих»
// было бы уже не удобством, а дырой.
//
// Хост сравнивается ЦЕЛИКОМ, а не по префиксу: иначе
// "http://192.168.1.42.evil.com" прошёл бы проверку. Цифры и точки регистра
// не знают — сравнение обычное, побайтное.
//
// deviceIp пустой (сеть ещё не поднялась) — своих нет: открыть дашборд без
// адреса всё равно не из чего.
inline bool originIsLocalDevice(const char* origin, const char* deviceIp) {
    if (!origin || !*origin) return false;
    if (!deviceIp || !*deviceIp) return false;

    // Устройство отдаёт только http. Origin вида https://… — точно не наш,
    // а "null" (песочница iframe, file://) не подходит под схему и отсеется тут же.
    static const char SCHEME[] = "http://";
    const size_t SCHEME_LEN = sizeof(SCHEME) - 1;
    if (strncmp(origin, SCHEME, SCHEME_LEN) != 0) return false;

    const char* host = origin + SCHEME_LEN;

    // Отрезаем порт. Дашборд живёт на 80-м, браузер его в Origin не пишет,
    // но явное ":80" — это тот же origin, принимаем.
    const char* colon = strchr(host, ':');
    size_t len = colon ? (size_t)(colon - host) : strlen(host);
    if (colon && strcmp(colon + 1, "80") != 0) return false;

    return len == strlen(deviceIp) && strncmp(host, deviceIp, len) == 0;
}
