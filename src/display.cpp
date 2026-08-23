#include "display.h"
#include "config.h"
#include "app.h"
#include "clock_utils.h"
#include "display_calc.h"
#include <U8g2lib.h>
#include <SPI.h>

// ============================================================
//  display.cpp — всё, что знает про геометрию экрана.
// ============================================================

static U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI
    u8g2(U8G2_R0, OLED_CS_PIN, OLED_DC_PIN, OLED_RST_PIN);

static bool        displayOn        = true;   // питание: /api/power и расписание
// Уровень шкалы интерфейса, 0..255. В регистры тока панели он переводится
// в refreshPanel() — там же объяснено, почему это не одно и то же.
//
// Стартуем не с максимума: до первого applyAutoBrightness() успевают пройти
// заставка и вся инициализация, а на батарее это самый дорогой режим панели.
// Уровень тот же, что автоматика берёт при неизвестном часе (brightnessNoTime).
static uint8_t     currentLevel     = CONTRAST_EVENING;
static bool        manualBrightness = false;
static const char* brightnessLabel  = "No time";

// Питание панели и яркость связаны (см. refreshPanel), поэтому обе функции
// нужны раньше, чем идут их разделы.
static void refreshPanel();
static void applyLevel(uint8_t val, const char* label);

// Раскладка часов: поля слева/справа и место под знак градуса.
static const int CLOCK_MARGIN  = 4;
static const int DEGREE_W      = 9;    // кружок градуса после цифр
static const int SEP_GAP       = 8;    // воздух вокруг разделителя
static const int TEMP_BASELINE = 42;   // выше часов: шрифт мельче, иначе висит низко

// Геометрия поля ".mmm" последнего полного кадра; < 0 — нужен полный кадр.
static int  swMsX = 0, swMsBoxX = -1, swMsBoxW = 0;
// Текст последнего полного кадра целиком. До часа по нему сверяются первые
// пять символов "MM:SS", от часа — вся строка "HH:MM:SS": там миллисекунд нет,
// и полный кадр нужен раз в секунду, а не на каждый вызов.
static char swLastFull[16] = "";

// ─── Инициализация ────────────────────────────────────────
void displayBegin() {
#if HAS_DISPLAY
    SPI.begin(OLED_CLK_PIN, -1, OLED_DIN_PIN, OLED_CS_PIN);
    u8g2.begin();
    refreshPanel();
#endif
}

void displaySplash(const char* msg) {
#if HAS_DISPLAY
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(50, 35, msg);
    u8g2.sendBuffer();
#else
    (void)msg;
#endif
}

// ─── Питание ──────────────────────────────────────────────
// Панель светится, только когда её не выключили И контраст ненулевой.
// Регистр контраста SSD1322 (0xC1) задаёт ток сегментов, а не яркость до
// нуля: на нулевом значении панель не гаснет, а продолжает заметно светить.
// Поэтому «0 %» доводится до конца через power save — иначе ползунок в
// нижнем положении не выключал бы экран, а лишь слегка притушивал.
static bool panelLit() { return displayOn && currentLevel > 0; }

static void refreshPanel() {
#if HAS_DISPLAY
    if (!panelLit()) { u8g2.setPowerSave(1); return; }

    // Ток задают два регистра, а не один (см. display_calc.h). setContrast()
    // из u8g2 пишет только 0xC1, master-регистр шлём сами — иначе нижняя
    // половина шкалы упирается в 1/256 полного тока и экран на ней светит
    // почти так же, как на максимуме.
    PanelDrive d = panelDriveForLevel(currentLevel);
    u8g2.sendF("ca", 0x0C7, d.master);
    u8g2.setContrast(d.contrast);
    u8g2.setPowerSave(0);      // ток выставлен заранее: панель не моргнёт
#endif
}

void displaySetPower(bool on) {
    displayOn = on;
    // Включение при нулевой яркости иначе не дало бы ничего видимого —
    // панель осталась бы в power save. Поднимаем до нижней ЧИТАЕМОЙ ступени:
    // команда «включить экран» должна давать результат, который видно.
    //
    // Единица тут не годится: по гамме это 1/4096 полного тока, панель на ней
    // неотличима от выключенной. А в ручном режиме поправить уровень некому —
    // displayAutoForHour() выходит сразу, — и экран так и оставался «включённым»
    // и чёрным одновременно.
    if (on && currentLevel == 0) applyLevel(CONTRAST_MIN_VISIBLE, "Manual");
    else                         refreshPanel();
    Serial.printf("Display -> %s\n", panelLit() ? "ON" : "OFF");
}

// Наружу отдаём фактическое состояние панели, а не флаг питания: расписание
// в main.cpp и дашборд спрашивают «экран сейчас виден?», и при нулевой
// яркости честный ответ — нет.
bool displayIsOn() { return panelLit(); }

// ─── Яркость ──────────────────────────────────────────────
static void applyLevel(uint8_t val, const char* label) {
    currentLevel = val;
    brightnessLabel = label;
    refreshPanel();      // ноль гасит панель, ненулевое — пересчитывает ток
}

void displaySetManualPct(int pct) {
    manualBrightness = true;
    // Ползунок в нуле — это «выключить», а не «еле светит»: гашение делает
    // refreshPanel(), здесь остаётся назвать уровень своим именем.
    applyLevel(pctToContrast(pct), pct <= 0 ? "Off" : "Manual");
}

void displaySetAuto() { manualBrightness = false; }

bool displayIsManual() { return manualBrightness; }

// Масштаб авто-яркости: эконом-режимы прижимают всю шкалу разом, а не
// правят таблицу часов — деление на «утро/день/вечер/ночь» остаётся.
static uint8_t autoScalePct = 100;

void displaySetAutoScale(uint8_t pct) {
    if (pct > 100) pct = 100;
    autoScalePct = pct;
}

static void applyAutoLevel(BrightnessLevel b) {
    if (manualBrightness) return;
    // Ниже единицы не опускаемся: ноль гасит панель совсем (см. refreshPanel),
    // а автоматика яркости экран выключать не должна — за это отвечают ночное
    // расписание и порог заряда.
    uint8_t scaled = (uint8_t)((uint32_t)b.contrast * autoScalePct / 100);
    if (scaled == 0) scaled = 1;
    if (scaled != currentLevel) {
        applyLevel(scaled, b.label);
        Serial.printf("Auto brightness -> %s (%d)\n", brightnessLabel, currentLevel);
    }
}

void displayAutoForHour(int hour) { applyAutoLevel(brightnessForHour(hour)); }

// Часы не встали: расписание применить не к чему, но яркость выставить надо —
// иначе панель осталась бы на стартовом уровне до самого первого синка.
void displayAutoNoTime() { applyAutoLevel(brightnessNoTime()); }

uint8_t     displayLevel()           { return currentLevel; }
const char* displayBrightnessLabel() { return brightnessLabel; }

void displayInvalidateStopwatch() { swMsBoxX = -1; swLastFull[0] = '\0'; }

// ─── Нижняя строка: погода + батарея ─────────────────────
#if HAS_DISPLAY
// Маленькая иконка батареи: рамка + «носик» + заливка по проценту.
// Рисует левым краем в x, верх иконки yTop (высота 7). Возвращает полную ширину.
static int drawBattIcon(int x, int yTop, uint8_t pct) {
    const int w = 14, h = 7;
    u8g2.drawFrame(x, yTop, w, h);              // контур тела
    u8g2.drawBox(x + w, yTop + 2, 2, 3);        // носик
    int fillW = (int)pct * (w - 2) / 100;       // заливка пропорционально
    if (fillW > 0) u8g2.drawBox(x + 1, yTop + 1, fillW, h - 2);
    return w + 2;                               // тело + носик
}

// Крупная перечёркнутая батарея на месте температуры. Когда заряда почти
// не осталось, это важнее погоды: цифру в углу можно и не заметить, а знак
// во всё левое поле — нет.
//
// Рамка и перечёркивание в две линии: одиночная на 256×64 при низком
// контрасте теряется, особенно под наклоном.
static void drawBattWarn(int xLeft, int avail) {
    const int w = 44, h = 24, nub = 3;
    int x = xLeft + (avail - (w + nub)) / 2;
    int y = TEMP_BASELINE - h;

    u8g2.drawFrame(x, y, w, h);
    u8g2.drawFrame(x + 1, y + 1, w - 2, h - 2);
    u8g2.drawBox(x + w, y + h / 2 - 4, nub, 8);          // носик

    u8g2.drawLine(x + 4, y + h - 4, x + w - 5, y + 3);   // перечёркивание
    u8g2.drawLine(x + 5, y + h - 4, x + w - 4, y + 3);
}

// Нижняя строка: сегменты через точку слева, батарея справа.
// mark — маркер перед сегментами ("II " на паузе) или nullptr.
//
// Сегменты упорядочены по убыванию важности: если строка не помещается
// до батареи, хвост просто не рисуется. Раньше ширина не проверялась вовсе
// и всё держалось на том, что сегментов заведомо мало.
static void drawBottomStatus(const char* mark, bool withDate, bool withTemp) {
    u8g2.setFont(u8g2_font_5x7_tr);

    char tstr[12], pstr[12];
    const char* segs[5];
    int n = 0;

    if (withDate && timeSynced) {
        segs[n++] = dayShortBuf;
        segs[n++] = dateBuf;
    }
    if (!weather.valid) {
        segs[n++] = "no sensor";
    } else {
        if (withTemp) {
            snprintf(tstr, sizeof(tstr), "%.1fC", weather.temperature);
            segs[n++] = tstr;
        }
        snprintf(pstr, sizeof(pstr), "%.0fhPa", weather.pressure);
        segs[n++] = pstr;
    }
    // Адрес — последним: нужен редко (когда роутер выдал новый и clock.local
    // не отзывается), поэтому при нехватке места жертвуем именно им.
    // На секундомере не показываем: там режим сосредоточенный.
    // 0.0.0.0 — сети нет, показывать нечего.
    if (withDate && localIP.length() && localIP != "0.0.0.0") segs[n++] = localIP.c_str();

    // Батарею считаем первой, чтобы знать, где заканчивается место под текст.
    // Показывается, только если обнаружена (иначе питание от USB).
    int textLimit = 254;
    if (battery.valid) {
        char pctStr[6];
        snprintf(pctStr, sizeof(pctStr), "%u%%", battery.percent);
        int pctW   = u8g2.getStrWidth(pctStr);
        int iconW  = 16;                         // 14 тело + 2 носик
        int totalW = iconW + 3 + pctW;
        // Своё имя, а не x: батарея прижата к правому краю независимо
        // от курсора сегментов выше, и путать их не стоит.
        int xBat   = 256 - totalW - 2;
        drawBattIcon(xBat, 56, battery.percent);  // верх 56 → низ 62, вровень с текстом
        u8g2.drawStr(xBat + iconW + 3, 63, pctStr);
        textLimit = xBat - 4;                     // воздух между текстом и иконкой
    }

    int x = 2;
    if (mark && mark[0]) {
        u8g2.drawStr(x, 63, mark);
        x += u8g2.getStrWidth(mark);
    }
    for (int i = 0; i < n; i++) {
        int gap = i ? 8 : 0;
        if (x + gap + u8g2.getStrWidth(segs[i]) > textLimit) break;
        if (gap) {
            u8g2.drawBox(x + 3, 59, 2, 2);   // разделитель-точка по центру строки
            x += gap;
        }
        u8g2.drawStr(x, 63, segs[i]);
        x += u8g2.getStrWidth(segs[i]);
    }
}
#endif

// ─── Кадр ─────────────────────────────────────────────────
void displayDraw() {
#if !HAS_DISPLAY
    return;
#else
    if (!panelLit()) return;
    u8g2.clearBuffer();

    if (!stopwatch.idle()) {
        // ── Режим секундомера ──────────────────────────
        char full[16];
        formatStopwatch(stopwatch.elapsed(millis()), full, sizeof(full));  // "MM:SS.mmm"
        // делим на "MM:SS" (крупно) и хвост (мельче): ".mmm" у секундомера
        // до часа, ":SS" после. Точность у %s — чтобы гарантия влезания была
        // видна компилятору, а не держалась на длине формата в formatStopwatch.
        char mmss[6];  char msStr[8];
        memcpy(mmss, full, 5);   mmss[5] = '\0';
        snprintf(msStr, sizeof(msStr), "%.*s", (int)sizeof(msStr) - 1, full + 5);

        u8g2.setFont(u8g2_font_logisoso46_tr);
        int mainW = u8g2.getStrWidth(mmss);
        u8g2.setFont(u8g2_font_logisoso24_tr);
        int msW = u8g2.getStrWidth(msStr);

        int totalW  = mainW + msW + 4;
        int xMain   = (256 - totalW) / 2;
        int xMs     = xMain + mainW + 4;

        u8g2.setFont(u8g2_font_logisoso46_tr);
        u8g2.drawStr(xMain, 50, mmss);
        u8g2.setFont(u8g2_font_logisoso24_tr);
        u8g2.drawStr(xMs, 50, msStr);

        // Геометрия для частичной перерисовки в displayStopwatchFrame()
        swMsX    = xMs;
        swMsBoxX = (xMs / 8) * 8;
        int boxRight = ((xMs + msW + 7) / 8) * 8 + 8;   // +тайл запаса справа
        if (boxRight > 256) boxRight = 256;
        swMsBoxW = boxRight - swMsBoxX;
        snprintf(swLastFull, sizeof(swLastFull), "%s", full);

        u8g2.drawHLine(0, 53, 256);

        // Низ: погода + батарея, с маркером состояния секундомера
        drawBottomStatus(stopwatch.running() ? nullptr : "II ", false, true);

    } else {
        // ── Часы: слева температура, справа HH:MM ──────
        char hh[3] = { timeBuf[0], timeBuf[1], 0 };
        char mm[3] = { timeBuf[3], timeBuf[4], 0 };

        u8g2.setFont(u8g2_font_logisoso46_tr);
        int dw  = u8g2.getStrWidth("00");
        int cw  = u8g2.getStrWidth(":");
        const int gap = 4;
        int clockW = dw * 2 + cw + gap * 2;

        // Разделитель привязан к часам, а не к температуре: иначе дёргался бы
        // при смене её ширины.
        int xClock = 256 - CLOCK_MARGIN - clockW;
        int xSep   = xClock - SEP_GAP;

        int x = xClock;
        u8g2.drawStr(x, 50, hh);  x += dw + gap;
        u8g2.drawStr(x, 50, ":"); x += cw + gap;
        u8g2.drawStr(x, 50, mm);

        u8g2.drawVLine(xSep, 4, 46);     // разделитель на всю высоту цифр

        int leftW = xSep - SEP_GAP - CLOCK_MARGIN;

        if (battery.valid && battery.percent <= BATTERY_CRITICAL_PCT) {
            // Заряд на исходе — вместо погоды предупреждение
            drawBattWarn(CLOCK_MARGIN, leftW);
        } else {
            // Знак градуса — кружком: в наборе _tr символа ° нет.
            char tstr[8];
            if (weather.valid) snprintf(tstr, sizeof(tstr), "%.1f", weather.temperature);
            else               snprintf(tstr, sizeof(tstr), "--");

            int avail = leftW - DEGREE_W;
            int th = 32;
            u8g2.setFont(u8g2_font_logisoso32_tr);
            int tw = u8g2.getStrWidth(tstr);
            if (tw > avail && weather.valid) {         // "-12.3" шире плюсовой:
                snprintf(tstr, sizeof(tstr), "%.0f", weather.temperature);
                tw = u8g2.getStrWidth(tstr);           // сперва жертвуем десятыми
            }
            if (tw > avail) {                          // и только потом размером
                th = 26;
                u8g2.setFont(u8g2_font_logisoso26_tr);
                tw = u8g2.getStrWidth(tstr);
            }

            int xT = CLOCK_MARGIN + (avail - tw) / 2;
            u8g2.drawStr(xT, TEMP_BASELINE, tstr);
            u8g2.drawCircle(xT + tw + 4, TEMP_BASELINE - th + 4, 3);   // ° у верха цифр
        }

        u8g2.drawHLine(0, 53, 256);

        drawBottomStatus(nullptr, true, false);
        swMsBoxX = -1;                 // на часах поля ".mmm" нет
    }

    u8g2.sendBuffer();
#endif
}

// Полный кадр стоит ~24 мс и 25 fps не выдержит, поэтому между секундами
// шлём только полоску ".mmm" (~2 мс). Полный — когда меняется "MM:SS".
void displayStopwatchFrame() {
#if !HAS_DISPLAY
    return;
#else
    if (!panelLit()) return;

    uint32_t el = stopwatch.elapsed(millis());
    char full[16];
    formatStopwatch(el, full, sizeof(full));

    // >= 1 часа формат "HH:MM:SS" — миллисекунд нет, дробить нечего.
    // От часа формат "HH:MM:SS": миллисекунд в нём нет, дробить нечего — но и
    // гнать полный кадр на каждый вызов незачем. Раньше условие el >= 3600000
    // делало ровно это: значение менялось раз в секунду, а кадр уходил в панель
    // 25 раз, по ~24 мс на SPI каждый.
    if (el >= 3600000UL) {
        if (strcmp(full, swLastFull) != 0) displayDraw();
        return;
    }

    if (swMsBoxX < 0 || memcmp(full, swLastFull, 5) != 0) {
        displayDraw();
        return;
    }

    u8g2.setDrawColor(0);
    u8g2.drawBox(swMsBoxX, 24, swMsBoxW, 27);   // y 24..50, линию на y=53 не трогаем
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_logisoso24_tr);
    u8g2.drawStr(swMsX, 50, full + 5);
    u8g2.updateDisplayArea(swMsBoxX / 8, 3, swMsBoxW / 8, 4);
#endif
}
