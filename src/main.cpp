#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ================= TFT =================
#define TFT_CS 10
#define TFT_DC 8
#define TFT_RST 9

#define TFT_MOSI 11
#define TFT_SCLK 12

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// ================= Inputs =================
#define ENC_CLK 20
#define ENC_DT 21
#define ENC_SW 47

#define BTN1 48
#define BTN2 45
#define BTN3 0
#define BTN4 35

// ================= Encoder =================
int encoderCount = 0;
int encoderSteps = 0;

int lastEncoded = 0;
unsigned long lastEncoderTime = 0;

// ================= Circle Positions =================

#define R 20

struct ButtonState
{
  int x;
  int y;
  bool last;
};

ButtonState buttons[5] =
    {
        {60, 150, false},  // BTN1
        {180, 150, false}, // BTN2
        {60, 270, false},  // BTN3
        {180, 270, false}, // BTN4
        {120, 210, false}  // SW
};

// ================= Drawing =================

void drawCircle(ButtonState &b, bool pressed)
{
  if (pressed)
  {
    tft.fillCircle(b.x, b.y, R, ST77XX_GREEN);
  }
  else
  {
    tft.fillCircle(b.x, b.y, R, ST77XX_BLACK);
    tft.drawCircle(b.x, b.y, R, ST77XX_WHITE);
  }
}

void updateEncoderDisplay()
{
  tft.fillRect(15, 15, 210, 50, ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(25, 25);

  tft.print("Count:");
  tft.print(encoderCount);
}

// ================= Setup =================

void setup()
{
  Serial.begin(115200);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  // Initial encoder state
  lastEncoded =
      (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);

  tft.drawRect(10, 10, 220, 60, ST77XX_WHITE);

  for (int i = 0; i < 5; i++)
    drawCircle(buttons[i], false);

  updateEncoderDisplay();
}

// ================= Loop =================

void loop()
{

  // -------- Encoder --------

  int MSB = digitalRead(ENC_CLK);
  int LSB = digitalRead(ENC_DT);

  int encoded = (MSB << 1) | LSB;

  int sum = (lastEncoded << 2) | encoded;

  int movement = 0;

  // clockwise
  if (sum == 0b1101 ||
      sum == 0b0100 ||
      sum == 0b0010 ||
      sum == 0b1011)
  {
    movement = 1;
  }

  // counter clockwise
  if (sum == 0b1110 ||
      sum == 0b0111 ||
      sum == 0b0001 ||
      sum == 0b1000)
  {
    movement = -1;
  }

  if (movement != 0)
  {
    encoderSteps += movement;

    // One physical click = one count
    if (abs(encoderSteps) >= 2)
    {
      if (encoderSteps > 0)
        encoderCount++;
      else
        encoderCount--;

      encoderSteps = 0;

      updateEncoderDisplay();
    }
  }

  lastEncoded = encoded;

  // -------- Buttons --------

  bool states[5];

  states[0] = !digitalRead(BTN1);
  states[1] = !digitalRead(BTN2);
  states[2] = !digitalRead(BTN3);
  states[3] = !digitalRead(BTN4);
  states[4] = !digitalRead(ENC_SW);

  for (int i = 0; i < 5; i++)
  {
    if (states[i] != buttons[i].last)
    {
      drawCircle(buttons[i], states[i]);
      buttons[i].last = states[i];
    }
  }
}