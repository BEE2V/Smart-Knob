#pragma once

#include <Arduino.h>

constexpr int TFT_CS = 10;
constexpr int TFT_DC = 8;
constexpr int TFT_RST = 9;

constexpr int TFT_MOSI = 11;
constexpr int TFT_SCLK = 12;

constexpr int ENC_CLK = 20;
constexpr int ENC_DT = 21;
constexpr int ENC_SW = 47;

constexpr int BTN1 = 48;
constexpr int BTN2 = 45;
constexpr int BTN3 = 0;
constexpr int BTN4 = 35;

constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;
constexpr uint16_t UI_DARK_GREY = 0x7BEF;
