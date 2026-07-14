#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// TFT Pins
#define TFT_CS 10
#define TFT_DC 8
#define TFT_RST 9

#define TFT_MOSI 11
#define TFT_SCLK 12

Adafruit_ST7789 tft = Adafruit_ST7789(
    TFT_CS,
    TFT_DC,
    TFT_RST);

void setup()
{
  Serial.begin(115200);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);
  tft.setRotation(0);

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);

  tft.setCursor(20, 20);
  tft.println("ESP32-S3");

  tft.setCursor(20, 50);
  tft.println("GMT024-08");

  tft.drawRect(10, 100, 100, 60, ST77XX_RED);
  tft.fillCircle(180, 130, 30, ST77XX_BLUE);

  Serial.println("Display Test OK");
}

void loop()
{
}