#include "web_api.h"
#include "config.h"
#include "app.h"
#include "display.h"
#include "power.h"
#include "clock_utils.h"
#include "battery_calc.h"
#include "origin_check.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "web_ui_gz.h"   // генерируется из web/index.html при сборке

// ============================================================
//  web_api.cpp — веб-сервер, WebSocket и сборка JSON.
// ============================================================

static WebServer        server(80);
static WebSocketsServer webSocket(81);
static uint32_t         requestCount = 0;

// Буфер снимка. Один на все три места, где он собирается, — иначе при
// добавлении поля легко нарастить формат и забыть один из них: snprintf
// обрежет строку молча, и дашборд получит JSON без закрывающей скобки.
// Сейчас снимок занимает ~730 байт; запас — на длинный SSID и на пару
// будущих полей.
static const size_t JSON_BUF = 1280;

// ─── Защита изменяющих запросов ───────────────────────────
// Любая открытая в браузере страница может отправить нам POST или открыть
// WebSocket — локальная сеть тут не граница, потому что код выполняется
// в браузере, который в этой сети уже находится. Отличить свой дашборд
// от чужой вкладки позволяет Origin: его ставит браузер, и подделать его
// со страницы нельзя. Логика сравнения — в origin_check.h.
//
// WebServer хранит только заранее заказанные заголовки, иначе
// server.header("Origin") вернёт пустую строку.
static const char* COLLECTED_HEADERS[] = { "Origin" };

// Пустой Origin — это не браузер (curl, Home Assistant, скрипты): пропускаем.
// Защищаемся от чужой вкладки, а не от осознанного запроса из консоли.
static bool originAccepted() {
    String origin = server.header("Origin");
    if (origin.length() == 0) return true;
    return originIsLocalDevice(origin.c_str(), localIP.c_str(), DEVICE_HOSTNAME);
}

// Булев параметр запроса. Раньше на месте вызовов стояло `arg(...) != "0"`,
// то есть истиной считалось всё, кроме строки "0": и "false", и "off", и просто
// пустое значение включали экран. Признаём истинными только явные написания.
static bool argIsTrue(const String& v) {
    return v == "1" || v.equalsIgnoreCase("true")
        || v.equalsIgnoreCase("on") || v.equalsIgnoreCase("yes");
}

static void sendForeignOrigin() {
    server.send(403, "application/json", "{\"error\":\"foreign origin\"}");
}

// Вызывается на каждый заголовок рукопожатия WebSocket. Origin не помечен
// обязательным, поэтому клиент без него (не браузер) подключится, а вот
// чужой Origin рукопожатие завалит.
static bool wsValidateHeader(String headerName, String headerValue) {
    if (!headerName.equalsIgnoreCase("Origin")) return true;
    return originIsLocalDevice(headerValue.c_str(), localIP.c_str(), DEVICE_HOSTNAME);
}

// Почему заряда нет. «Батареи нет» и «напряжение выше нормы» — разные беды:
// первая штатна (питание от USB), вторая означает, что на линию банки лезет
// что-то постороннее, и списывать её на отсутствие банки нельзя.
static const char* batteryState() {
    if (battery.valid) return "ok";
    return batteryRawVoltage() > BATTERY_PLAUSIBLE_MAX_V ? "over" : "none";
}

// ─── JSON ─────────────────────────────────────────────────
static void buildJson(char* buf, size_t sz) {
    char uptimeBuf[32];
    formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
    snprintf(buf, sz,
        "{"
        "\"time\":\"%s\","
        "\"date\":\"%s\","
        "\"day\":\"%s\","
        "\"uptime\":\"%s\","
        "\"ssid\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"temp\":\"%.1f\","
        "\"clients\":%d,"
        "\"ram_free\":%lu,"
        "\"ram_total\":%lu,"
        "\"brightness_pct\":%d,"
        "\"brightness_label\":\"%s\","
        "\"brightness_manual\":%s,"
        "\"display_on\":%s,"
        "\"screen_peek\":%lu,"
        "\"sw_state\":%d,"
        "\"sw_ms\":%lu,"
        "\"sw_gen\":%lu,"
        "\"bmp_valid\":%s,"
        "\"bmp_temp\":%.2f,"
        "\"pressure\":%.2f,"
        "\"pressure_mmhg\":%.1f,"
        "\"altitude\":%.1f,"
        "\"trend\":%.2f,"
        "\"forecast\":%d,"
        "\"bat_valid\":%s,"
        "\"bat_pct\":%d,"
        "\"bat_v\":%.2f,"
        "\"bat_raw_v\":%.2f,"
        "\"bat_state\":\"%s\","
        "\"bat_mah\":%d,"
        "\"bat_mah_full\":%d,"
        "\"bat_low\":%s,"
        "\"power_mode\":\"%s\","
        "\"power_auto\":%s,"
        "\"power_chosen\":\"%s\","
        "\"power_pinned\":%s,"
        "\"requests\":%lu"
        "}",
        timeBuf, dateBuf, dayFullBuf,
        uptimeBuf,
        WIFI_SSID, localIP.c_str(),
        (int)WiFi.RSSI(),
        (float)dieTempC(),
        (int)webSocket.connectedClients(),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long)ESP.getHeapSize(),
        brightnessPct(displayLevel()),
        displayBrightnessLabel(),
        displayIsManual() ? "true" : "false",
        displayIsOn()     ? "true" : "false",
        (unsigned long)screenPeekLeftS(),
        (int)stopwatch.state,
        (unsigned long)stopwatch.elapsed(millis()),
        (unsigned long)stopwatch.gen,
        weather.valid ? "true" : "false",
        weather.temperature,
        weather.pressure,
        weather.pressureMmHg,
        weather.altitude,
        weather.pressureTrend,
        (int)weather.forecastIcon,
        battery.valid ? "true" : "false",
        (int)battery.percent,
        battery.voltage,
        batteryRawVoltage(),
        batteryState(),
        (int)batteryRemainingMah(battery.percent, BATTERY_USABLE_MAH),
        (int)BATTERY_USABLE_MAH,
        battery.low ? "true" : "false",
        powerModeName(),
        powerIsAuto() ? "true" : "false",
        powerProfile(powerChosenMode()).name,
        powerStopwatchPinned() ? "true" : "false",
        (unsigned long)requestCount
    );
}

void webApiBroadcast() {
    if (webSocket.connectedClients() == 0) return;  // некому слать — не тратим CPU
    char json[JSON_BUF];
    buildJson(json, sizeof(json));
    webSocket.broadcastTXT(json);
}

// ─── HTTP ─────────────────────────────────────────────────
static void handleRoot() {
    requestCount++;
    // Страница лежит во флеше уже сжатой — распаковывает её браузер.
    // 75 КБ → 17 КБ и по сети, и во флеше.
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html", (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

static void handleApiStats() {
    requestCount++;
    char json[JSON_BUF];
    buildJson(json, sizeof(json));
    server.send(200, "application/json", json);
}

static void handleApiTime() {
    requestCount++;
    char uptimeBuf[32];
    formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
    char json[256];
    snprintf(json, sizeof(json),
        "{\"time\":\"%s\",\"date\":\"%s\",\"day\":\"%s\","
        "\"uptime\":\"%s\",\"timestamp\":%lu}",
        timeBuf, dateBuf, dayFullBuf,
        uptimeBuf, (unsigned long)time(nullptr));
    server.send(200, "application/json", json);
}

static void handleApiWeather() {
    requestCount++;
    char json[320];
    snprintf(json, sizeof(json),
        "{\"valid\":%s,\"temperature\":%.2f,\"pressure\":%.2f,"
        "\"pressure_mmhg\":%.1f,\"altitude\":%.1f,\"qnh\":%.2f,"
        "\"air_density\":%.4f,\"trend\":%.2f,\"forecast\":%d,"
        "\"battery_valid\":%s,\"battery_pct\":%d,\"battery_v\":%.2f}",
        weather.valid ? "true" : "false",
        weather.temperature, weather.pressure, weather.pressureMmHg,
        weather.altitude, weather.pressureQnh, weather.airDensity,
        weather.pressureTrend, (int)weather.forecastIcon,
        battery.valid ? "true" : "false",
        (int)battery.percent, battery.voltage);
    server.send(200, "application/json", json);
}

static void handleApiBrightness() {
    requestCount++;
    if (!originAccepted()) return sendForeignOrigin();
    if (server.hasArg("auto") && argIsTrue(server.arg("auto"))) {
        displaySetAuto();
        server.send(200, "application/json", "{\"ok\":true,\"mode\":\"auto\"}");
        applyAutoBrightness();       // сразу применяем авто-уровень
        webApiBroadcast();
        return;
    }
    if (server.hasArg("value")) {
        int pct = constrain(server.arg("value").toInt(), 0, 100);
        displaySetManualPct(pct);
        char resp[64];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"mode\":\"manual\",\"pct\":%d}", pct);
        server.send(200, "application/json", resp);
        webApiBroadcast();
        return;
    }
    server.send(400, "application/json", "{\"error\":\"missing value or auto\"}");
}

static void handleApiPower() {
    requestCount++;
    if (!originAccepted()) return sendForeignOrigin();
    if (server.hasArg("on")) {
        bool on = argIsTrue(server.arg("on"));
        screenSetPower(on);      // не displaySetPower: ночью включаем с таймером
        // Отдаём фактическое состояние панели, а не запрошенное: включение
        // могут и не выполнить (заряд на исходе), и ответ обязан это показать.
        char resp[48];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"display_on\":%s}",
                 displayIsOn() ? "true" : "false");
        server.send(200, "application/json", resp);
        webApiBroadcast();
        return;
    }
    server.send(400, "application/json", "{\"error\":\"missing on param\"}");
}

// Режим энергосбережения: mode=normal|eco|survival фиксирует уровень,
// auto=1 возвращает автоматику по заряду.
static void handleApiPowerMode() {
    requestCount++;
    if (!originAccepted()) return sendForeignOrigin();

    if (server.hasArg("auto") && argIsTrue(server.arg("auto"))) {
        powerSetAuto();
    } else if (server.hasArg("mode")) {
        PowerMode m;
        if (!powerModeFromName(server.arg("mode").c_str(), &m)) {
            server.send(400, "application/json", "{\"error\":\"bad mode\"}");
            return;
        }
        powerSetMode(m);
    } else {
        server.send(400, "application/json", "{\"error\":\"missing mode or auto\"}");
        return;
    }

    // mode — что работает сейчас, chosen — что выбрано. Под замером они
    // расходятся: секундомер держит «обычный» поверх ручного выбора, и
    // дашборду надо показать принятую команду, а не поднятый уровень.
    char resp[144];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"mode\":\"%s\",\"auto\":%s,"
             "\"chosen\":\"%s\",\"pinned\":%s}",
             powerModeName(), powerIsAuto() ? "true" : "false",
             powerProfile(powerChosenMode()).name,
             powerStopwatchPinned() ? "true" : "false");
    server.send(200, "application/json", resp);
    webApiBroadcast();
}

static void handleReboot() {
    requestCount++;
    if (!originAccepted()) return sendForeignOrigin();
    server.send(200, "text/plain", "Rebooting...");
    delay(300);
    ESP.restart();
}

static void handleNotFound() {
    requestCount++;
    server.send(404, "text/plain", "Not found");
}

// ─── WebSocket ────────────────────────────────────────────
static void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    if (type == WStype_CONNECTED) {
        requestCount++;
        char json[JSON_BUF];
        buildJson(json, sizeof(json));
        webSocket.sendTXT(num, json);
        Serial.printf("WS client #%d connected\n", num);
    } else if (type == WStype_TEXT) {
        // Замер задержки: без логов и рассылки, иначе исказим RTT.
        if (length >= 5 && strncmp((char*)payload, "ping:", 5) == 0) {
            size_t tokLen = length - 5;
            if (tokLen > 12) tokLen = 12;
            char reply[48];
            snprintf(reply, sizeof(reply), "pong:%.*s:%d:%lu",
                     (int)tokLen, (char*)payload + 5,
                     (int)stopwatch.state,
                     (unsigned long)stopwatch.elapsed(millis()));
            webSocket.sendTXT(num, reply);
            return;
        }

        // Живая яркость: "br:<0..100>" или "br:auto". Ползунок шлёт значение
        // на каждый шаг жеста, поэтому этот путь обрабатывается до логов и до
        // рассылки — десяток строк в Serial и десяток килобайт JSON в секунду
        // стоили бы дороже самой регулировки. Отправитель значение и так знает,
        // остальные вкладки увидят его ближайшим снимком (раз в секунду).
        if (length > 3 && strncmp((char*)payload, "br:", 3) == 0) {
            char val[8] = {0};
            size_t n = length - 3;
            if (n > sizeof(val) - 1) n = sizeof(val) - 1;
            memcpy(val, payload + 3, n);
            if (strcmp(val, "auto") == 0) {
                displaySetAuto();
                applyAutoBrightness();
            } else {
                int pct = atoi(val);
                if (pct < 0)   pct = 0;
                if (pct > 100) pct = 100;
                displaySetManualPct(pct);
            }
            return;
        }

        Serial.printf("WS #%d TEXT: %.*s\n", num, (int)length, (char*)payload);
        // Команды секундомера: "sw:start" / "sw:pause" / "sw:reset"
        if      (length >= 8 && strncmp((char*)payload, "sw:start", 8) == 0) swStart();
        else if (length >= 8 && strncmp((char*)payload, "sw:pause", 8) == 0) swPause();
        else if (length >= 8 && strncmp((char*)payload, "sw:reset", 8) == 0) swReset();
        webApiBroadcast();          // мгновенно рассылаем новое состояние
    }
}

// ─── Публичный API ────────────────────────────────────────
void webApiBegin() {
    server.collectHeaders(COLLECTED_HEADERS,
                          sizeof(COLLECTED_HEADERS) / sizeof(COLLECTED_HEADERS[0]));

    server.on("/",               HTTP_GET,  handleRoot);
    server.on("/api/stats",      HTTP_GET,  handleApiStats);
    server.on("/api/time",       HTTP_GET,  handleApiTime);
    server.on("/api/weather",    HTTP_GET,  handleApiWeather);
    server.on("/api/brightness", HTTP_POST, handleApiBrightness);
    server.on("/api/power",      HTTP_POST, handleApiPower);
    server.on("/api/powermode",  HTTP_POST, handleApiPowerMode);
    server.on("/api/reboot",     HTTP_POST, handleReboot);
    server.onNotFound(handleNotFound);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    // Счётчик обязательных заголовков 0: без Origin (не браузер) пускаем,
    // с чужим Origin — отказ в рукопожатии.
    webSocket.onValidateHttpHeader(wsValidateHeader, COLLECTED_HEADERS, 0);

    Serial.println("HTTP :80  WS :81");
}

void webApiLoop() {
    server.handleClient();
    webSocket.loop();
}

uint8_t  webApiClientCount()  { return webSocket.connectedClients(); }
uint32_t webApiRequestCount() { return requestCount; }
