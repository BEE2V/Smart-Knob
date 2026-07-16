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

// Fill these later when testing real Home Assistant commands.
constexpr const char *WIFI_SSID = "Dialog 4G";
constexpr const char *WIFI_PASSWORD = "Q06YLA0QED3";
constexpr const char *HA_BASE_URL = "http://192.168.8.104:8123"; // Example: "http://192.168.8.120:8123"
constexpr const char *HA_TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIxYTdhZTNmNmE5ZGI0YzAyYmIzOGUwNzM2NWJkZjg1OCIsImlhdCI6MTc4NDE4OTIxOSwiZXhwIjoyMDk5NTQ5MjE5fQ.xfRVkPvpB2K8ZjXpcxWBJhgVptZ92I569UBsdrr84A8";
