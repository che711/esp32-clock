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
// Origin проверяет изменяющие запросы, If-None-Match нужен handleRoot() для
// ответа 304. Без заказа сюда server.header() вернёт пустую строку.
static const char* COLLECTED_HEADERS[] = { "Origin", "If-None-Match" };

// У рукопожатия WebSocket свой список: там интересен только Origin, и
// подмешивать в него заголовки кеширования незачем.
static const char* WS_HEADERS[] = { "Origin" };

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
        "\"trend\":%.2f,"
        "\"forecast\":%d,"
        "\"bat_valid\":%s,"
        "\"bat_pct\":%d,"
        "\"bat_v\":%.2f,"
        "\"bat_raw_v\":%.2f,"
        "\"bat_state\":\"%s\","
        "\"bat_mah\":%d,"
        "\"bat_mah_full\":%d,"
        "\"bat_warn_pct\":%d,"
        "\"bat_crit_pct\":%d,"
        "\"bat_low\":%s,"
        "\"power_mode\":\"%s\","
        "\"power_chosen\":\"%s\","
        "\"power_pinned\":%s,"
        "\"reset_reason\":\"%s\","
        "\"reset_abnormal\":%s,"
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
        weather.pressureTrend,
        (int)weather.forecastIcon,
        battery.valid ? "true" : "false",
        (int)battery.percent,
        battery.voltage,
        batteryRawVoltage(),
        batteryState(),
        (int)batteryRemainingMah(battery.percent, BATTERY_USABLE_MAH),
        (int)BATTERY_USABLE_MAH,
        // Пороги едут в снимок, а не зашиты в дашборде: он обязан говорить
        // ровно то же, что показывают сами часы. Зашитые копии уже разъезжались
        // с прошивкой — после правки кривой «< 30 %» в UI перестало значить
        // что-либо. Два лишних числа в секунду дешевле такого расхождения.
        (int)BATTERY_CRITICAL_PCT,
        (int)POWER_SCREEN_OFF_PCT,
        battery.low ? "true" : "false",
        powerModeName(),
        powerProfile(powerChosenMode()).name,
        powerIsHeld() ? "true" : "false",
        resetReasonName(),
        resetWasAbnormal() ? "true" : "false",
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

    // Кеширование. Раньше страница уходила вообще без заголовков на этот счёт,
    // и браузер по стандарту вправе был держать копию эвристически, сколько
    // сочтёт нужным. Так и выходило: после перепрошивки устройство раздавало
    // новый дашборд, а вкладка показывала прошлый, и лечилось это только
    // Ctrl+Shift+R — про который надо ещё догадаться.
    //
    // no-cache здесь не значит «не кешируй»: копию держать можно, но перед
    // показом обязательно спросить. Спрашивает браузер через ETag, а тот
    // считается из содержимого страницы при сборке (gen_web_ui.py). Совпал —
    // отвечаем 304 и не гоняем 26 КБ; не совпал — страница поменялась, и
    // отдать надо новую.
    server.sendHeader("Cache-Control", "no-cache");
    server.sendHeader("ETag", INDEX_HTML_ETAG);

    if (server.header("If-None-Match") == INDEX_HTML_ETAG) {
        server.send(304, "text/html", "");
        return;
    }

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
        "\"pressure_mmhg\":%.1f,\"qnh\":%.2f,"
        "\"air_density\":%.4f,\"trend\":%.2f,\"forecast\":%d,"
        "\"battery_valid\":%s,\"battery_pct\":%d,\"battery_v\":%.2f}",
        weather.valid ? "true" : "false",
        weather.temperature, weather.pressure, weather.pressureMmHg,
        weather.pressureQnh, weather.airDensity,
        weather.pressureTrend, (int)weather.forecastIcon,
        battery.valid ? "true" : "false",
        (int)battery.percent, battery.voltage);
    server.send(200, "application/json", json);
}

// ─── История для графиков ─────────────────────────────────
// Ответ здесь на порядок больше снимка: 5 рядов по TREND_HISTORY_SIZE точек —
// это ~12 КБ. Столько не собрать ни на стеке, ни в String, не разодрав кучу
// ровно в тот момент, когда через неё же идёт раздача страницы. Поэтому
// ответ уходит chunked: текст копится в буфере на полкилобайта и улетает
// кусками по мере заполнения.
struct ChunkWriter {
    char   buf[512];
    size_t len = 0;

    void put(const char* s) {
        size_t n = strlen(s);
        if (len + n > sizeof(buf)) flush();
        if (n > sizeof(buf)) { server.sendContent(s, n); return; }  // не влезет и в пустой
        memcpy(buf + len, s, n);
        len += n;
    }
    void printf(const char* fmt, float v) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), fmt, v);
        put(tmp);
    }
    void flush() {
        if (!len) return;
        server.sendContent(buf, len);
        len = 0;
    }
};

// Один ряд значений: [1.23,null,4.56]. NAN — это «датчик молчал»,
// и в JSON он обязан стать null: NaN литералом стандарт не знает,
// JSON.parse на такой ответ падает целиком.
static void writeSeries(ChunkWriter& out, const char* key, const char* fmt,
                        float TrendSample::*field) {
    out.put(",\"");
    out.put(key);
    out.put("\":[");
    for (uint16_t i = 0; i < trendHistory.size(); i++) {
        if (i) out.put(",");
        float v = trendHistory.at(i).*field;
        if (isnan(v)) out.put("null"); else out.printf(fmt, v);
    }
    out.put("]");
}

static void handleApiHistory() {
    requestCount++;
    const uint32_t now = millis();
    const uint16_t n   = trendHistory.size();

    server.sendHeader("Cache-Control", "no-store");   // история живая, кешировать нечего
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    ChunkWriter out;
    char head[96];
    // step_s — ожидаемый шаг между точками (период опроса датчика), чтобы
    // дашборд знал, когда ждать следующую. Возраст точек отдаём в секундах
    // назад от «сейчас»: время устройства зависит от NTP, а разность millis()
    // верна всегда, и часы браузера с часами прошивки сводить не приходится.
    snprintf(head, sizeof(head), "{\"n\":%u,\"step_s\":%lu",
             (unsigned)n, (unsigned long)(powerSensorIntervalMs() / 1000UL));
    out.put(head);

    out.put(",\"age\":[");
    for (uint16_t i = 0; i < n; i++) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%s%lu", i ? "," : "",
                 (unsigned long)trendHistory.ageS(i, now));
        out.put(tmp);
    }
    out.put("]");

    writeSeries(out, "temp",  "%.2f", &TrendSample::temp);
    writeSeries(out, "press", "%.2f", &TrendSample::press);
    writeSeries(out, "trend", "%.2f", &TrendSample::trend);

    out.put("}");
    out.flush();
    server.sendContent("", 0);   // пустой чанк закрывает ответ
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

// Уровень энергосбережения: mode=normal|eco.
static void handleApiPowerMode() {
    requestCount++;
    if (!originAccepted()) return sendForeignOrigin();

    if (!server.hasArg("mode")) {
        server.send(400, "application/json", "{\"error\":\"missing mode\"}");
        return;
    }
    PowerMode m;
    if (!powerModeFromName(server.arg("mode").c_str(), &m)) {
        server.send(400, "application/json", "{\"error\":\"bad mode\"}");
        return;
    }
    powerSetMode(m);

    // mode — что работает сейчас, chosen — что выбрано. Расходятся они на
    // время замера: секундомер держит обычный уровень, а выбор ждёт сброса.
    // Дашборду надо показать принятую команду, а не поднятый уровень.
    char resp[144];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"mode\":\"%s\",\"chosen\":\"%s\",\"pinned\":%s}",
             powerModeName(),
             powerProfile(powerChosenMode()).name,
             powerIsHeld() ? "true" : "false");
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
    server.on("/api/history",    HTTP_GET,  handleApiHistory);
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
    webSocket.onValidateHttpHeader(wsValidateHeader, WS_HEADERS, 0);

    Serial.println("HTTP :80  WS :81");
}

void webApiLoop() {
    server.handleClient();
    webSocket.loop();
}

uint8_t  webApiClientCount()  { return webSocket.connectedClients(); }
uint32_t webApiRequestCount() { return requestCount; }
