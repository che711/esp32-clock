#include "net.h"
#include "config.h"
#include "app.h"
#include "power.h"
#include "schedule_calc.h"
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// ============================================================
//  net.cpp — Wi-Fi + NTP.
//
//  Подключение — автомат, а не цикл с delay() внутри. Ассоциация
//  занимает секунды, а из loop() она вызывается на возврате из
//  выживания: пока connectWifi() крутил свои 30 × delay(500),
//  вставало всё — часы, секундомер, веб и опрос датчика, до
//  пятнадцати секунд разом.
//
//  Автомат двигают два вызова: wifiBeginConnect() начинает,
//  wifiConnectStep() делает шаг и говорит, закончилось ли. Вся
//  работа «после связи» собрана в wifiOnConnected(), чтобы не
//  разъезжалась между путями.
// ============================================================

static bool     wifiConnecting   = false;
static uint32_t wifiConnectStart = 0;
static uint32_t wifiDotMs        = 0;   // ритм точек в логе, раз в 500 мс
// Отработала ли wifiOnConnected() для текущей ассоциации. Связь поднимается не
// только через автомат: setAutoReconnect(true) и WiFi.reconnect() возвращают её
// сами, мимо него. Без этого признака после такого возврата не было бы ни
// нового localIP, ни настроек радио под профиль.
static bool     wifiReady        = false;

// Момент последнего запуска SNTP. Общий для netBegin() и netLoop(), чтобы
// ретрай отсчитывался от старта, а не от первого захода в цикл.
static uint32_t lastNtpMs = 0;

// Поднимает SNTP-демона и задаёт TZ-правило (DST считается автоматически).
// Вызывается и без связи: TZ должен быть установлен в любом случае, а демон
// сам ретраит запрос, когда сеть появится.
static void startNTP() {
    configTzTime(TZ_INFO, NTP_SERVER);
    lastNtpMs = millis();
}

static void wifiOnConnected() {
    wifiReady = true;
    localIP = WiFi.localIP().toString();
    Serial.printf("\nIP: %s\n", localIP.c_str());
    // Мощность и глубина сна — под режим и секундомер. Старт STA в ядре
    // Arduino сбрасывает сон на MIN_MODEM, так что без этого вызова радио
    // после переподключения работало бы не в своей глубине.
    powerApplyRadio();
    // mDNS (clock.local) здесь больше не поднимается: в сети он так и не
    // заработал, а раз часы открывают только по IP, то и адрес ответчика
    // незачем. IP виден в нижней строке экрана.
    // Время — сразу, как появилась связь. SNTP запущен ещё в netBegin(), но
    // без сети его запрос ушёл в никуда, а повтора ждать долго: наш в
    // netLoop() случится только через NTP_RETRY_MS, и всё это время на
    // экране прочерки. На реконнекте запрос лишний — один пакет.
    startNTP();
}

static void wifiBeginConnect() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // begin() без подключения: он переписывает конфиг STA целиком, и
    // listen_interval надо вписать после него, но до ассоциации —
    // позже точка доступа его не узнает (см. powerPrepareAssociation).
    // Переподключения, свои и авто, берут конфиг уже с ним.
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 0, nullptr, false);
    powerPrepareAssociation();
    WiFi.reconnect();              // без связи это просто esp_wifi_connect()
    wifiConnecting   = true;
    wifiReady        = false;
    wifiConnectStart = millis();
    wifiDotMs        = wifiConnectStart;
}

// true — подключение больше не в процессе: либо связь есть, либо вышло время.
// Промах не страшен: дальше связь поднимает авто-реконнект ядра, а если и он
// не справится — запасной WiFi.reconnect() из netLoop().
static bool wifiConnectStep(uint32_t now) {
    if (!wifiConnecting) return true;

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnecting = false;
        wifiOnConnected();
        return true;
    }
    if (now - wifiDotMs >= 500) { wifiDotMs = now; Serial.print("."); }
    if (now - wifiConnectStart >= WIFI_CONNECT_TIMEOUT_MS) {
        wifiConnecting = false;
        Serial.println("\nWiFi FAILED");
        return true;
    }
    return false;
}

// Связь и время в setup() не ждём — их доводит netLoop(): подключение крутит
// автомат, время запрашивает wifiOnConnected().
//
// Раньше связь в setup() прокачивалась на месте, а следом ещё до десяти секунд
// ждали NTP: «показывать всё равно нечего, на экране заставка». Показывать
// было нечего, но и отвечать тоже: веб-сервер поднимался только после обоих
// ожиданий, и в худшем случае часы выходили на связь через 15 с WiFi плюс
// 10 с NTP. Сильнее всего это било по кнопке RST: она стирает время в RTC,
// и ожидание NTP после неё шло всегда, а после паники проскакивало.
//
// wifiBeginConnect() первым: WiFi.mode() внутри поднимает сетевой стек, на
// котором стоят и SNTP, и веб-сервер. startNTP() — сразу за ним, не дожидаясь
// связи: TZ нужен сейчас, время могло пережить сброс в RTC.
void netBegin() {
    wifiBeginConnect();
    startNTP();
}

// Поддержание сети: реконнект + ретрай NTP, пока часы не встали.
void netLoop() {
    static uint32_t  lastCheck = 0;
    // Сколько длится текущий обрыв — или сколько прошло с нашего последнего
    // reconnect(). Свой reconnect — только запасной путь. На обрыв первым
    // отвечает авто-реконнект ядра, и раньше мы дёргали reconnect() поверх
    // него каждые 10 с: esp_wifi_connect() посреди его ассоциации начинал её
    // заново, и при слабом сигнале связь могла не встать никогда. Но ядро
    // повторяет не всё — например, AUTH_FAIL после первого подключения оно не
    // ретраит, — поэтому совсем без нас нельзя. Ждём столько же, сколько
    // отводим на одну ассоциацию.
    static HoldTimer lost;
    uint32_t now = millis();

    // Пока идёт ассоциация — только двигаем автомат. Проверять связь и
    // ре-синкать NTP посреди подключения нечего.
    if (wifiConnecting) { wifiConnectStep(now); return; }

    if (now - lastCheck >= 10000) {          // проверка связи раз в 10 с
        lastCheck = now;
        bool down = WiFi.status() != WL_CONNECTED;
        switch (lost.step(down, now, WIFI_CONNECT_TIMEOUT_MS)) {
        case HoldTimer::STARTED:
            Serial.println("WiFi lost, auto-reconnect is on it");
            break;
        case HoldTimer::ELAPSED:
            Serial.println("WiFi still down -> reconnect");
            WiFi.reconnect();
            break;
        case HoldTimer::NONE:
            break;
        }
        if (down) {
            wifiReady = false;
        } else if (!wifiReady) {
            // Связь вернулась мимо автомата — авто-реконнектом стека или
            // предыдущим WiFi.reconnect(). Доводим её до конца тем же путём.
            wifiOnConnected();
        }
        String ip = WiFi.localIP().toString();
        if (ip != localIP) localIP = ip;
    }
    // Только ретрай, пока часы не встали. Плановый ресинк раньше стоял здесь
    // же, раз в 6 ч, но был пустым: демон SNTP и сам переспрашивает сервер
    // каждые CONFIG_LWIP_SNTP_UPDATE_DELAY (3 ч в сборке ядра), а наш вызов
    // лишь перезапускал его.
    if (!timeSynced && WiFi.status() == WL_CONNECTED
        && now - lastNtpMs >= NTP_RETRY_MS) {
        Serial.println("NTP retry");
        startNTP();
    }
}

void netShutdown() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
int  netRssi()      { return WiFi.RSSI(); }
