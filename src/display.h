#pragma once
#include <stdint.h>

// ============================================================
//  display.h — OLED SSD1322: раскладка кадра, яркость, питание.
//
//  Данные для отрисовки модуль берёт из app.h, наружу торчат
//  только команды. При HAS_DISPLAY == 0 все функции — заглушки.
// ============================================================

void displayBegin();                  // SPI + инициализация панели
void displaySplash(const char* msg);  // одна строка по центру («Connecting WiFi...»)

void displayDraw();                   // полный кадр: часы или секундомер
void displayStopwatchFrame();         // между секундами — только поле ".mmm"
void displayInvalidateStopwatch();    // следующий кадр секундомера сделать полным

// ─── Питание ──────────────────────────────────────────────
void displaySetPower(bool on);
bool displayIsOn();

// ─── Яркость ──────────────────────────────────────────────
void displaySetManualPct(int pct);    // ручной режим, 0..100 %
void displaySetAuto();                // вернуться к авто-режиму
bool displayIsManual();
void displayAutoForHour(int hour);    // применить уровень часа (в ручном — ничего)
void displaySetAutoScale(uint8_t pct); // масштаб авто-яркости, % (эконом-режимы)

uint8_t     displayContrast();        // текущий контраст 0..255
const char* displayBrightnessLabel(); // "Night" / "Day" / "Manual" …
