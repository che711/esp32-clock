#include "web_api.h"
#include "config.h"
#include "app.h"
#include "display.h"
#include "power.h"
#include "clock_utils.h"
#include "battery_calc.h"
#include "origin_check.h"
#include "net.h"
#include <esp_http_server.h>
#include <algorithm>
#include <atomic>
#include <new>
#include <strings.h>
#include <unistd.h>
#include "web_ui_gz.h"   // генерируется из web/index.html при сборке

// ============================================================
//  web_api.cpp — веб-сервер, WebSocket и сборка JSON.
//
//  Сервер — esp_http_server из IDF, а не WebServer из Arduino.
//  WebServer обслуживал соединения строго по одному и на каждом
//  молчащем ждал запрос до 5 с. Браузер такие соединения открывает
//  сам, про запас, — и страница с API вставали: /api/stats при одном
//  пустом сокете рядом отвечал за 4.9 с вместо 0.03. esp_http_server
//  ждёт все сокеты разом через select(), и молчащий никому не мешает.
//  WebSocket живёт в нём же, на /ws порта 80, — отдельный сервер на
//  :81 (links2004/WebSockets) больше не нужен.
//
//  Цена — свой поток: обработчики работают в задаче сервера, а не
//  в loop(). Всё, чем владеет main.cpp, они трогают только под
//  замком (appLock, app.h). Сеть под замком не ждём: loop() на это
//  время встаёт, и часы на экране пропускали бы секунды.
//
//  Таблицу WebSocket-клиентов трогает только задача сервера:
//  обработчики, close_fn и работа из httpd_queue_work() идут в ней
//  же, по очереди, поэтому замок таблице не нужен.
// ============================================================

// 7 — значение IDF по умолчанию. Браузер держит до шести соединений на
// хост, плюс WebSocket; лишнее закрывает lru_purge — первым уходит самое
// давно молчавшее, то есть запасной сокет, а не живой WebSocket: тот
// получает pong каждые WS_PING_INTERVAL_MS.
static const int HTTPD_MAX_SOCKETS = 7;

static httpd_handle_t httpd = nullptr;

// Пишет задача сервера, читает и loop() — в снимке.
static std::atomic<uint32_t> requestCount{0};

// Буфер снимка. Один на все три места, где он собирается, — иначе при
// добавлении поля легко нарастить формат и забыть один из них: snprintf
// обрежет строку молча, и дашборд получит JSON без закрывающей скобки.
// Сейчас снимок занимает ~830 байт; запас — на длинный SSID и на пару
// будущих полей.
static const size_t JSON_BUF = 1280;

// SSID в снимке — уже экранированный: имя сети задаёт пользователь, и кавычка
// в нём рвала JSON. Строка постоянная, поэтому готовится один раз в
// webApiBegin(), а не на каждый кадр. 32 байта SSID × 6 на худший символ.
static char ssidJson[32 * 6 + 1] = "";

// ─── Замок состояния ──────────────────────────────────────
struct AppGuard {
    AppGuard()  { appLock(); }
    ~AppGuard() { appUnlock(); }
    AppGuard(const AppGuard&) = delete;
    AppGuard& operator=(const AppGuard&) = delete;
};

// ─── Защита изменяющих запросов ───────────────────────────
// Любая открытая в браузере страница может отправить нам POST или открыть
// WebSocket — локальная сеть тут не граница, потому что код выполняется
// в браузере, который в этой сети уже находится. Отличить свой дашборд
// от чужой вкладки позволяет Origin: его ставит браузер, и подделать его
// со страницы нельзя. Логика сравнения — в origin_check.h.
//
// Пустой Origin — это не браузер (curl, Home Assistant, скрипты): пропускаем.
// Защищаемся от чужой вкладки, а не от осознанного запроса из консоли.
// Не влезший в буфер Origin длиннее любого своего — он чужой.
//
// Вызывать под замком: localIP переписывает loop() при переподключении.
static bool originAccepted(httpd_req_t* req) {
    char origin[64];
    esp_err_t r = httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
    if (r == ESP_ERR_NOT_FOUND) return true;
    if (r != ESP_OK)            return false;
    return originIsLocalDevice(origin, localIP.c_str());
}

// Та же проверка, но замок берёт сама и сразу отпускает. Для изменяющих ручек:
// отказ (403, 400) они отправляют уже без замка. Раньше отказы уходили прямо
// из-под него — против правила из шапки файла: отправка ждёт сеть, а loop()
// всё это время стоит.
static bool originAcceptedLocked(httpd_req_t* req) {
    AppGuard lock;
    return originAccepted(req);
}

// Булев параметр запроса. Раньше на месте вызовов стояло `arg(...) != "0"`,
// то есть истиной считалось всё, кроме строки "0": и "false", и "off", и просто
// пустое значение включали экран. Признаём истинными только явные написания.
static bool argIsTrue(const char* v) {
    return strcmp(v, "1") == 0 || strcasecmp(v, "true") == 0
        || strcasecmp(v, "on") == 0 || strcasecmp(v, "yes") == 0;
}

// Параметры запроса. Дашборд шлёт их телом (x-www-form-urlencoded), curl и
// Home Assistant — часто строкой запроса; WebServer::arg() смотрел в оба
// места, и ручки обязаны принимать то же, что раньше. Значения у нас —
// числа и короткие слова, 128 байт на каждое место с запасом; хвост тела
// длиннее сервер вычитает и выбросит сам.
struct ReqArgs {
    char query[128] = "";
    char body[128]  = "";

    explicit ReqArgs(httpd_req_t* req) {
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK)
            query[0] = '\0';
        size_t want = req->content_len;
        if (want > sizeof(body) - 1) want = sizeof(body) - 1;
        size_t got = 0;
        while (got < want) {
            int r = httpd_req_recv(req, body + got, want - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        body[got] = '\0';
    }

    bool get(const char* key, char* val, size_t sz) const {
        return httpd_query_key_value(body,  key, val, sz) == ESP_OK
            || httpd_query_key_value(query, key, val, sz) == ESP_OK;
    }
};

static esp_err_t sendJson(httpd_req_t* req, const char* status, const char* body) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t sendForeignOrigin(httpd_req_t* req) {
    return sendJson(req, "403 Forbidden", "{\"error\":\"foreign origin\"}");
}

// Почему заряда нет. «Батареи нет» и «напряжение выше нормы» — разные беды:
// первая штатна (питание от USB), вторая означает, что на линию банки лезет
// что-то постороннее, и списывать её на отсутствие банки нельзя.
static const char* batteryState() {
    if (battery.valid) return "ok";
    return batteryRawVoltage() > BATTERY_PLAUSIBLE_MAX_V ? "over" : "none";
}

// ─── WebSocket: клиенты ───────────────────────────────────
// Клиент — сокет, прошедший рукопожатие на /ws. lastSeenMs — когда от него
// пришёл последний кадр (pong или команда): по нему heartbeat отличает
// живую вкладку от пропавшей, см. WS_PING_INTERVAL_MS в config.h.
struct WsClient {
    int      fd;
    uint32_t lastSeenMs;
};
static WsClient wsClients[HTTPD_MAX_SOCKETS];
static std::atomic<int> wsCount{0};   // пишет задача сервера, читает и loop()

static WsClient* wsFind(int fd) {
    WsClient* const end = wsClients + HTTPD_MAX_SOCKETS;
    WsClient* it = std::find_if(wsClients, end,
                                [fd](const WsClient& c) { return c.fd == fd; });
    return it == end ? nullptr : it;
}

// Сессия закрыта — любая, не только WebSocket: сервер зовёт это для всех.
// Раз обработчик свой, сокет закрываем сами, так требует esp_http_server.
static void onSessionClose(httpd_handle_t, int fd) {
    WsClient* c = wsFind(fd);
    if (c) {
        c->fd = -1;
        wsCount--;
    }
    close(fd);
}

// Отправка, которая не вышла, значит, что сокет мёртв или забит. Держать
// такого клиента дальше — ждать на каждой следующей отправке.
static void wsSendOrDrop(int fd, httpd_ws_frame_t& frame) {
    if (httpd_ws_send_frame_async(httpd, fd, &frame) != ESP_OK)
        httpd_sess_trigger_close(httpd, fd);
}

// ─── JSON ─────────────────────────────────────────────────
// Вызывать под замком: читает всё, чем владеет loop().
static void buildJson(char* buf, size_t sz) {
    char uptimeBuf[32];
    formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
    // Строка с экрана часов: дашборд показывает её как есть, а не округляет
    // bmp_temp второй раз (почему — у displayTempText()). Сам bmp_temp с
    // сотыми остаётся для графиков, min/avg/max и °F.
    char tempDisp[12];
    displayTempText(tempDisp, sizeof(tempDisp));
    snprintf(buf, sz,
        "{"
        "\"time\":\"%s\","
        "\"date\":\"%s\","
        "\"day\":\"%s\","
        "\"uptime\":\"%s\","
        "\"ssid\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"temp\":%.1f,"
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
        "\"bmp_temp_disp\":\"%s\","
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
        "\"bat_sign_pct\":%d,"
        "\"bat_screen_off_pct\":%d,"
        "\"bat_low\":%s,"
        "\"power_mode\":\"%s\","
        "\"power_chosen\":\"%s\","
        "\"power_pinned\":%s,"
        "\"night_on\":%d,"
        "\"night_off\":%d,"
        "\"sensor_normal_s\":%lu,"
        "\"sensor_eco_s\":%lu,"
        "\"reset_reason\":\"%s\","
        "\"reset_abnormal\":%s,"
        "\"requests\":%lu"
        "}",
        timeBuf, dateBuf, dayFullBuf,
        uptimeBuf,
        ssidJson, localIP.c_str(),
        netRssi(),
        (float)dieTempC(),
        wsCount.load(),
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
        tempDisp,
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
        // Имена — по тому, что порог делает на часах: раньше поля звались
        // warn/crit по цвету плитки, и crit нёс порог гашения экрана, а не
        // BATTERY_CRITICAL_PCT — читать это было нельзя без исходников.
        (int)BATTERY_CRITICAL_PCT,
        (int)POWER_SCREEN_OFF_PCT,
        battery.low ? "true" : "false",
        powerModeName(),
        powerProfile(powerChosenMode()).name,
        powerIsHeld() ? "true" : "false",
        // Расписание и периоды опроса — для подсказок к уровням в дашборде:
        // по той же причине, что и пороги выше, копии там не держим.
        (int)POWER_NIGHT_ON_HOUR,
        (int)POWER_NIGHT_OFF_HOUR,
        (unsigned long)(powerProfile(POWER_NORMAL).sensorMs / 1000UL),
        (unsigned long)(powerProfile(POWER_ECO).sensorMs / 1000UL),
        resetReasonName(),
        resetWasAbnormal() ? "true" : "false",
        (unsigned long)requestCount.load()
    );
}

// ─── WebSocket: рассылка и heartbeat ──────────────────────
// Обе работы выполняет задача сервера (httpd_queue_work): только она пишет
// в сокеты, и кадры разных потоков не перемешаются в одном соединении.
static void wsBroadcastWork(void* arg) {
    char* json = static_cast<char*>(arg);
    httpd_ws_frame_t frame = {};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t*)json;
    frame.len     = strlen(json);
    for (const WsClient& c : wsClients)
        if (c.fd >= 0) wsSendOrDrop(c.fd, frame);
    free(json);
}

static void wsHeartbeatWork(void*) {
    const uint32_t now = millis();
    httpd_ws_frame_t ping = {};
    ping.type = HTTPD_WS_TYPE_PING;
    for (const WsClient& c : wsClients) {
        if (c.fd < 0) continue;
        if (now - c.lastSeenMs > WS_SILENCE_LIMIT_MS)
            httpd_sess_trigger_close(httpd, c.fd);
        else
            wsSendOrDrop(c.fd, ping);
    }
}

// Снимок собирается здесь, в вызывающем потоке под замком, а уходит копией
// в задачу сервера: собирать его там значило бы читать состояние loop()
// без замка.
void webApiBroadcast() {
    if (wsCount == 0 || !httpd) return;  // некому слать — не тратим CPU
    char json[JSON_BUF];
    buildJson(json, sizeof(json));
    char* copy = strdup(json);
    if (!copy) return;
    if (httpd_queue_work(httpd, wsBroadcastWork, copy) != ESP_OK) free(copy);
}

// ─── HTTP ─────────────────────────────────────────────────
// Страница не трогает состояние часов, поэтому замок ей не нужен: пока
// loop() занят, она всё равно отдаётся.
static esp_err_t handleRoot(httpd_req_t* req) {
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
    // отвечаем 304 и не гоняем страницу заново; не совпал — она поменялась, и
    // отдать надо новую.
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "ETag", INDEX_HTML_ETAG);

    char inm[40];
    if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK
        && strcmp(inm, INDEX_HTML_ETAG) == 0) {
        httpd_resp_set_status(req, "304 Not Modified");
        return httpd_resp_send(req, nullptr, 0);
    }

    // Страница лежит во флеше уже сжатой — распаковывает её браузер.
    // Комментарии из исходника вырезаются ещё при сборке (gen_web_ui.py),
    // поэтому уходит около 23 КБ вместо 130 КБ; точные цифры — в шапке
    // сгенерированного web_ui_gz.h. Здесь их больше не держим: прежние
    // «114 → 30» отстали от страницы на первой же крупной правке.
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

static esp_err_t handleApiStats(httpd_req_t* req) {
    requestCount++;
    char json[JSON_BUF];
    {
        AppGuard lock;
        buildJson(json, sizeof(json));
    }
    return sendJson(req, "200 OK", json);
}

static esp_err_t handleApiTime(httpd_req_t* req) {
    requestCount++;
    char json[256];
    {
        AppGuard lock;
        char uptimeBuf[32];
        formatUptime(millis() / 1000, uptimeBuf, sizeof(uptimeBuf));
        snprintf(json, sizeof(json),
            "{\"time\":\"%s\",\"date\":\"%s\",\"day\":\"%s\","
            "\"uptime\":\"%s\",\"timestamp\":%lu}",
            timeBuf, dateBuf, dayFullBuf,
            uptimeBuf, (unsigned long)time(nullptr));
    }
    return sendJson(req, "200 OK", json);
}

static esp_err_t handleApiWeather(httpd_req_t* req) {
    requestCount++;
    char json[320];
    {
        AppGuard lock;
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
    }
    return sendJson(req, "200 OK", json);
}

// ─── История для графиков ─────────────────────────────────
// Ответ здесь на порядок больше снимка: 5 рядов по TREND_HISTORY_SIZE точек —
// это ~12 КБ. Столько не собрать ни на стеке, ни в String, не разодрав кучу
// ровно в тот момент, когда через неё же идёт раздача страницы. Поэтому
// ответ уходит chunked: текст копится в буфере на полкилобайта и улетает
// кусками по мере заполнения.
struct ChunkWriter {
    httpd_req_t* req;
    char   buf[512] = {};
    size_t len = 0;
    bool   ok  = true;   // клиент отвалился — дальше не шлём

    explicit ChunkWriter(httpd_req_t* r) : req(r) {}

    void send(const char* s, size_t n) {
        if (ok && httpd_resp_send_chunk(req, s, (ssize_t)n) != ESP_OK) ok = false;
    }
    void put(const char* s) {
        size_t n = strlen(s);
        if (len + n > sizeof(buf)) flush();
        if (n > sizeof(buf)) { send(s, n); return; }  // не влезет и в пустой
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
        send(buf, len);
        len = 0;
    }
};

// Один ряд значений: [1.23,null,4.56]. NAN — это «датчик молчал»,
// и в JSON он обязан стать null: NaN литералом стандарт не знает,
// JSON.parse на такой ответ падает целиком.
static void writeSeries(ChunkWriter& out, const TrendHistory& h, const char* key,
                        const char* fmt, float TrendSample::*field) {
    out.put(",\"");
    out.put(key);
    out.put("\":[");
    for (uint16_t i = 0; i < h.size(); i++) {
        if (i) out.put(",");
        float v = h.at(i).*field;
        if (isnan(v)) out.put("null"); else out.printf(fmt, v);
    }
    out.put("]");
}

static esp_err_t handleApiHistory(httpd_req_t* req) {
    requestCount++;

    // Кольцо копируется под замком, а размечается и уходит без него. Отправка
    // ~12 КБ в экономе ждёт подтверждений браузера до полсекунды на кусок, и
    // держать замок всё это время значило бы остановить loop(). Копия — 7.2 КБ
    // в куче на время ответа.
    TrendHistory* snap = new (std::nothrow) TrendHistory;
    if (!snap) return httpd_resp_send_500(req);
    uint32_t now;
    {
        AppGuard lock;
        *snap = trendHistory;
        now   = millis();
    }
    const uint16_t n = snap->size();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");   // история живая, кешировать нечего

    ChunkWriter out(req);
    char head[24];
    // Возраст точек отдаём в секундах назад от «сейчас», а не временем: часы
    // устройства зависят от NTP, а разность millis() верна всегда, и сводить
    // их с часами браузера не приходится.
    //
    // Рядом с n стояло ещё step_s — ожидаемый шаг между точками, чтобы дашборд
    // знал, когда ждать следующую. Не пригодилось: график рисуется по самим
    // меткам возраста, а ждать точку ему незачем — она приезжает очередным
    // кадром. Поле уехало, чтобы не отдавать в каждом ответе число, которое
    // никто не читает, и не поддерживать его смысл при смене режима: в кольце
    // лежат точки, снятые и раз в минуту, и раз в две, а шаг был бы один.
    snprintf(head, sizeof(head), "{\"n\":%u", (unsigned)n);
    out.put(head);

    out.put(",\"age\":[");
    for (uint16_t i = 0; i < n; i++) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%s%lu", i ? "," : "",
                 (unsigned long)snap->ageS(i, now));
        out.put(tmp);
    }
    out.put("]");

    writeSeries(out, *snap, "temp",  "%.2f", &TrendSample::temp);
    writeSeries(out, *snap, "press", "%.2f", &TrendSample::press);
    writeSeries(out, *snap, "trend", "%.2f", &TrendSample::trend);
    // Милливольты, а не сотые: на хвосте кривой пункт шкалы стоит 5 мВ, и
    // округление до 0.01 В склеило бы соседние проценты в одно число.
    writeSeries(out, *snap, "bat",   "%.3f", &TrendSample::bat);

    out.put("}");
    out.flush();
    delete snap;
    out.send(nullptr, 0);   // пустой чанк закрывает ответ
    return out.ok ? ESP_OK : ESP_FAIL;
}

static esp_err_t handleApiBrightness(httpd_req_t* req) {
    requestCount++;
    ReqArgs args(req);
    if (!originAcceptedLocked(req)) return sendForeignOrigin(req);

    char val[8];
    const bool toAuto = args.get("auto", val, sizeof(val)) && argIsTrue(val);
    int pct = 0;
    if (!toAuto) {
        if (!args.get("value", val, sizeof(val)))
            return sendJson(req, "400 Bad Request",
                            "{\"error\":\"missing value or auto\"}");
        pct = atoi(val);
        if (pct < 0)   pct = 0;
        if (pct > 100) pct = 100;
    }

    {
        AppGuard lock;
        if (toAuto) {
            displaySetAuto();
            applyAutoBrightness();       // сразу применяем авто-уровень
        } else {
            displaySetManualPct(pct);
        }
        webApiBroadcast();
    }

    char resp[64];
    if (toAuto) snprintf(resp, sizeof(resp), "{\"ok\":true,\"mode\":\"auto\"}");
    else        snprintf(resp, sizeof(resp),
                         "{\"ok\":true,\"mode\":\"manual\",\"pct\":%d}", pct);
    return sendJson(req, "200 OK", resp);
}

static esp_err_t handleApiPower(httpd_req_t* req) {
    requestCount++;
    ReqArgs args(req);
    if (!originAcceptedLocked(req)) return sendForeignOrigin(req);
    char val[8];
    if (!args.get("on", val, sizeof(val)))
        return sendJson(req, "400 Bad Request", "{\"error\":\"missing on param\"}");

    char resp[96];
    {
        AppGuard lock;
        // не displaySetPower: ночью включаем с таймером
        const char* refused = screenSetPower(argIsTrue(val));
        // Отдаём фактическое состояние панели, а не запрошенное: включение
        // могут и не выполнить, и ответ обязан это показать. Вместе с причиной:
        // «заряд на исходе» и «яркость в нуле» лечатся по-разному, и дашборду
        // надо знать, что советовать. Пустая строка — отказа не было.
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"display_on\":%s,\"refused\":\"%s\"}",
                 displayIsOn() ? "true" : "false",
                 refused ? refused : "");
        webApiBroadcast();
    }
    return sendJson(req, "200 OK", resp);
}

// Уровень энергосбережения: mode=normal|eco.
static esp_err_t handleApiPowerMode(httpd_req_t* req) {
    requestCount++;
    ReqArgs args(req);
    if (!originAcceptedLocked(req)) return sendForeignOrigin(req);
    char name[16];
    if (!args.get("mode", name, sizeof(name)))
        return sendJson(req, "400 Bad Request", "{\"error\":\"missing mode\"}");
    PowerMode m;
    if (!powerModeFromName(name, &m))
        return sendJson(req, "400 Bad Request", "{\"error\":\"bad mode\"}");

    char resp[144];
    {
        AppGuard lock;
        powerSetMode(m);

        // mode — что работает сейчас, chosen — что выбрано. Расходятся они на
        // время замера: секундомер держит обычный уровень, а выбор ждёт сброса.
        // Дашборду надо показать принятую команду, а не поднятый уровень.
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"mode\":\"%s\",\"chosen\":\"%s\",\"pinned\":%s}",
                 powerModeName(),
                 powerProfile(powerChosenMode()).name,
                 powerIsHeld() ? "true" : "false");
        webApiBroadcast();
    }
    return sendJson(req, "200 OK", resp);
}

static esp_err_t handleReboot(httpd_req_t* req) {
    requestCount++;
    if (!originAcceptedLocked(req)) return sendForeignOrigin(req);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Rebooting...");
    delay(300);
    ESP.restart();
    return ESP_OK;
}

static esp_err_t handleNotFound(httpd_req_t* req, httpd_err_code_t) {
    requestCount++;
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "Not found");
}

// ─── WebSocket: приём ─────────────────────────────────────
// Команды — короткие строки: "ping:<до 12>", "br:auto", "sw:start". Кадр
// длиннее — не наш клиент, соединение рвём.
static const size_t WS_RX_MAX = 64;

// Рукопожатие: сервер уже ответил 101 и зовёт обработчик с GET. Проверка
// Origin поэтому здесь, а не до ответа: перехватчик рукопожатия
// (CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT) в сборке Arduino выключен.
// Чужой сокет закрывается сразу, прежде чем прочитан хоть один его кадр.
static esp_err_t wsOnHandshake(httpd_req_t* req, int fd) {
    requestCount++;
    char json[JSON_BUF];
    {
        AppGuard lock;
        if (!originAccepted(req)) return ESP_FAIL;
        WsClient* c = wsFind(-1);
        if (!c) return ESP_FAIL;
        c->fd         = fd;
        c->lastSeenMs = millis();
        wsCount++;
        buildJson(json, sizeof(json));
    }
    httpd_ws_frame_t frame = {};
    frame.type    = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t*)json;
    frame.len     = strlen(json);
    wsSendOrDrop(fd, frame);
    Serial.printf("WS client fd %d connected\n", fd);
    return ESP_OK;
}

static void wsOnText(int fd, const char* text, size_t length) {
    // Замер задержки: без логов и рассылки, иначе исказим RTT.
    if (length >= 5 && strncmp(text, "ping:", 5) == 0) {
        size_t tokLen = length - 5;
        if (tokLen > 12) tokLen = 12;
        char reply[48];
        {
            AppGuard lock;
            snprintf(reply, sizeof(reply), "pong:%.*s:%d:%lu",
                     (int)tokLen, text + 5,
                     (int)stopwatch.state,
                     (unsigned long)stopwatch.elapsed(millis()));
        }
        httpd_ws_frame_t frame = {};
        frame.type    = HTTPD_WS_TYPE_TEXT;
        frame.payload = (uint8_t*)reply;
        frame.len     = strlen(reply);
        wsSendOrDrop(fd, frame);
        return;
    }

    // Живая яркость: "br:<0..100>" или "br:auto". Ползунок шлёт значение
    // на каждый шаг жеста, поэтому этот путь обрабатывается до логов и до
    // рассылки — десяток строк в Serial и десяток килобайт JSON в секунду
    // стоили бы дороже самой регулировки. Отправитель значение и так знает,
    // остальные вкладки увидят его ближайшим снимком (раз в секунду).
    if (length > 3 && strncmp(text, "br:", 3) == 0) {
        char val[8] = {0};
        size_t n = length - 3;
        if (n > sizeof(val) - 1) n = sizeof(val) - 1;
        memcpy(val, text + 3, n);
        AppGuard lock;
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

    Serial.printf("WS fd %d TEXT: %.*s\n", fd, (int)length, text);
    AppGuard lock;
    // Команды секундомера: "sw:start" / "sw:pause" / "sw:reset"
    if      (length >= 8 && strncmp(text, "sw:start", 8) == 0) swStart();
    else if (length >= 8 && strncmp(text, "sw:pause", 8) == 0) swPause();
    else if (length >= 8 && strncmp(text, "sw:reset", 8) == 0) swReset();
    webApiBroadcast();          // мгновенно рассылаем новое состояние
}

// Управляющие кадры обработчик получает сам (handle_ws_control_frames):
// иначе сервер глотает pong молча, и heartbeat не отличил бы живую вкладку
// от пропавшей. Взамен ping и close приходится отвечать самим.
static esp_err_t handleWs(httpd_req_t* req) {
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) return wsOnHandshake(req, fd);

    uint8_t buf[WS_RX_MAX + 1];
    httpd_ws_frame_t frame = {};
    frame.payload = buf;
    // Длиннее WS_RX_MAX сервер кадр не примет и вернёт ошибку; вторая
    // проверка — чтобы нуль-терминатор не зависел от этого обещания.
    if (httpd_ws_recv_frame(req, &frame, WS_RX_MAX) != ESP_OK) return ESP_FAIL;
    if (frame.len > WS_RX_MAX) return ESP_FAIL;
    buf[frame.len] = '\0';

    WsClient* c = wsFind(fd);
    if (c) c->lastSeenMs = millis();   // любой кадр — знак жизни, не только pong

    switch (frame.type) {
    case HTTPD_WS_TYPE_TEXT:
        wsOnText(fd, (const char*)buf, frame.len);
        return ESP_OK;
    case HTTPD_WS_TYPE_PING:
        frame.type = HTTPD_WS_TYPE_PONG;           // pong несёт тот же payload
        return httpd_ws_send_frame_async(httpd, fd, &frame);
    case HTTPD_WS_TYPE_CLOSE:
        // Ответный close и закрытие: ESP_FAIL из обработчика сервер понимает
        // как «сессию закрыть».
        frame.len = 0;
        httpd_ws_send_frame_async(httpd, fd, &frame);
        return ESP_FAIL;
    default:                                        // pong, binary — только отметка выше
        return ESP_OK;
    }
}

// ─── Публичный API ────────────────────────────────────────
static void route(const char* uri, httpd_method_t method,
                  esp_err_t (*handler)(httpd_req_t*), bool websocket = false) {
    httpd_uri_t u = {};
    u.uri     = uri;
    u.method  = method;
    u.handler = handler;
    u.is_websocket             = websocket;
    u.handle_ws_control_frames = websocket;
    httpd_register_uri_handler(httpd, &u);
}

void webApiBegin() {
    jsonEscape(WIFI_SSID, ssidJson, sizeof(ssidJson));
    for (WsClient& c : wsClients) c.fd = -1;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = HTTPD_MAX_SOCKETS;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 12;      // 10 маршрутов + запас
    // 4 КБ по умолчанию мало: снимок (JSON_BUF) собирается на стеке
    // обработчика, а snprintf с плавающей точкой сам просит ещё около
    // килобайта.
    cfg.stack_size       = 8192;
    cfg.close_fn         = onSessionClose;

    if (httpd_start(&httpd, &cfg) != ESP_OK) {
        httpd = nullptr;
        Serial.println("HTTP: сервер не запустился");
        return;
    }

    route("/",               HTTP_GET,  handleRoot);
    route("/api/stats",      HTTP_GET,  handleApiStats);
    route("/api/time",       HTTP_GET,  handleApiTime);
    route("/api/weather",    HTTP_GET,  handleApiWeather);
    route("/api/history",    HTTP_GET,  handleApiHistory);
    route("/api/brightness", HTTP_POST, handleApiBrightness);
    route("/api/power",      HTTP_POST, handleApiPower);
    route("/api/powermode",  HTTP_POST, handleApiPowerMode);
    route("/api/reboot",     HTTP_POST, handleReboot);
    route("/ws",             HTTP_GET,  handleWs, true);
    httpd_register_err_handler(httpd, HTTPD_404_NOT_FOUND, handleNotFound);

    Serial.println("HTTP :80  WS :80/ws");
}

// Heartbeat: раз в WS_PING_INTERVAL_MS — ping живым и закрытие молчащих.
// Само обслуживание клиентов идёт в задаче сервера, loop() его больше
// не крутит.
void webApiLoop() {
    static uint32_t lastPing = 0;
    const uint32_t now = millis();
    if (wsCount == 0 || !httpd || now - lastPing < WS_PING_INTERVAL_MS) return;
    lastPing = now;
    httpd_queue_work(httpd, wsHeartbeatWork, nullptr);
}

uint8_t webApiClientCount() { return (uint8_t)wsCount.load(); }
