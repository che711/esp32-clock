#include "display.h"
#include "config.h"
#include "app.h"
#include "clock_utils.h"
#include <U8g2lib.h>
#include <SPI.h>

// ============================================================
//  display.cpp — всё, что знает про геометрию экрана.
// ============================================================

static U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI
    u8g2(U8G2_R0, OLED_CS_PIN, OLED_DC_PIN, OLED_RST_PIN);

static bool        displayOn        = true;
static uint8_t     currentContrast  = CONTRAST_MAX;
static bool        manualBrightness = false;
static const char* brightnessLabel  = "Day";

// Раскладка часов: поля слева/справа и место под знак градуса.
static const int CLOCK_MARGIN  = 4;
static const int DEGREE_W      = 9;    // кружок градуса после цифр
static const int SEP_GAP       = 8;    // воздух вокруг разделителя
static const int TEMP_BASELINE = 42;   // выше часов: шрифт мельче, иначе висит низко

// Геометрия поля ".mmm" последнего полного кадра; < 0 — нужен полный кадр.
static int  swMsX = 0, swMsBoxX = -1, swMsBoxW = 0;
static char swMmSs[6] = "";

// ─── Инициализация ────────────────────────────────────────
void displayBegin() {
#if HAS_DISPLAY
    SPI.begin(OLED_CLK_PIN, -1, OLED_DIN_PIN, OLED_CS_PIN);
    u8g2.begin();
    u8g2.setContrast(currentContrast);
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
void displaySetPower(bool on) {
    displayOn = on;
#if HAS_DISPLAY
    if (on) {
        u8g2.setPowerSave(0);
        u8g2.setContrast(currentContrast);
    } else {
        u8g2.setPowerSave(1);
    }
#endif
    Serial.printf("Display -> %s\n", on ? "ON" : "OFF");
}

bool displayIsOn() { return displayOn; }

// ─── Яркость ──────────────────────────────────────────────
static void applyContrast(uint8_t val, const char* label) {
    currentContrast = val;
    brightnessLabel = label;
#if HAS_DISPLAY
    if (displayOn) u8g2.setContrast(currentContrast);
#endif
}

void displaySetManualPct(int pct) {
    manualBrightness = true;
    applyContrast(pctToContrast(pct), "Manual");
}

void displaySetAuto() { manualBrightness = false; }

bool displayIsManual() { return manualBrightness; }

void displayAutoForHour(int hour) {
    if (manualBrightness) return;
    BrightnessLevel b = brightnessForHour(hour);
    if (b.contrast != currentContrast) {
        applyContrast(b.contrast, b.label);
        Serial.printf("Auto brightness -> %s (%d)\n", brightnessLabel, currentContrast);
    }
}

uint8_t     displayContrast()        { return currentContrast; }
const char* displayBrightnessLabel() { return brightnessLabel; }

void displayInvalidateStopwatch() { swMsBoxX = -1; }

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
    if (!displayOn) return;
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
        memcpy(swMmSs, full, 5);  swMmSs[5] = '\0';

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

        // Знак градуса — кружком: в наборе _tr символа ° нет.
        char tstr[8];
        if (weather.valid) snprintf(tstr, sizeof(tstr), "%.1f", weather.temperature);
        else               snprintf(tstr, sizeof(tstr), "--");

        int avail = xSep - SEP_GAP - CLOCK_MARGIN - DEGREE_W;
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
    if (!displayOn) return;

    uint32_t el = stopwatch.elapsed(millis());
    char full[16];
    formatStopwatch(el, full, sizeof(full));

    // >= 1 часа формат "HH:MM:SS" — миллисекунд нет, дробить нечего.
    if (el >= 3600000UL || swMsBoxX < 0 || memcmp(full, swMmSs, 5) != 0) {
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
