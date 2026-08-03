#pragma once
#include <string.h>
#include <stdio.h>
#include <ctype.h>

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

// DNS-имена регистронезависимы, сравниваем соответственно.
inline bool originHostEquals(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

// Дашборд открыт как http://<имя или IP устройства>, поэтому и Origin у него
// такой же. Всё остальное — чужая страница.
//
// Хост сравнивается ЦЕЛИКОМ, а не по префиксу: иначе
// "http://clock.local.evil.com" прошёл бы проверку.
//
// deviceIp может быть пустым (сеть ещё не поднялась) — тогда сверяемся
// только с mDNS-именем.
inline bool originIsLocalDevice(const char* origin,
                                const char* deviceIp,
                                const char* deviceHostname) {
    if (!origin || !*origin) return false;

    // Устройство отдаёт только http. Origin вида https://… — точно не наш,
    // а "null" (песочница iframe, file://) не подходит под схему и отсеется тут же.
    static const char SCHEME[] = "http://";
    const size_t SCHEME_LEN = sizeof(SCHEME) - 1;
    if (strncmp(origin, SCHEME, SCHEME_LEN) != 0) return false;

    const char* host = origin + SCHEME_LEN;

    // Отрезаем порт. Дашборд живёт на 80-м, браузер его в Origin не пишет,
    // но явное ":80" — это тот же origin, принимаем.
    char buf[64];
    const char* colon = strchr(host, ':');
    size_t len = colon ? (size_t)(colon - host) : strlen(host);
    if (colon && strcmp(colon + 1, "80") != 0) return false;
    if (len == 0 || len >= sizeof(buf)) return false;
    memcpy(buf, host, len);
    buf[len] = '\0';

    if (deviceIp && *deviceIp && originHostEquals(buf, deviceIp)) return true;

    if (deviceHostname && *deviceHostname) {
        if (originHostEquals(buf, deviceHostname)) return true;
        char mdns[64];
        int n = snprintf(mdns, sizeof(mdns), "%s.local", deviceHostname);
        if (n > 0 && n < (int)sizeof(mdns) && originHostEquals(buf, mdns)) return true;
    }
    return false;
}
