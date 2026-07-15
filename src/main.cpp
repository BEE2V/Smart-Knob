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

// ================= INPUTS =================

#define ENC_CLK 20
#define ENC_DT 21
#define ENC_SW 47

#define BTN1 48
#define BTN2 45
#define BTN3 0
#define BTN4 35

// =================================================
// DEVICE SYSTEM
// =================================================

enum DeviceType
{
  LIGHT,
  FAN,
  SENSOR,
  MEDIA
};

struct Device
{
  const char *name;

  DeviceType type;

  bool controllable;

  bool state;

  int value;

  int maxValue;
};

Device devices[] =
    {

        {"Living Room Light",
         LIGHT,
         true,
         true,
         80,
         100},

        {"Bedroom Fan",
         FAN,
         true,
         true,
         3,
         5},

        {"Temperature",
         SENSOR,
         false,
         true,
         25,
         50},

        {"Music Player",
         MEDIA,
         true,
         false,
         60,
         100}

};

const int deviceCount =
    sizeof(devices) / sizeof(Device);

// =================================================
// UI STATE
// =================================================

enum Screen
{
  DEVICE_LIST,
  DEVICE_PAGE
};

Screen currentScreen = DEVICE_LIST;

int selected = 0;

int oldSelected = 0;

// =================================================
// ENCODER
// =================================================

int lastEncoded = 0;

int encoderSteps = 0;

int readEncoder()
{

  int MSB = digitalRead(ENC_CLK);
  int LSB = digitalRead(ENC_DT);

  int encoded = (MSB << 1) | LSB;

  int sum = (lastEncoded << 2) | encoded;

  int move = 0;

  if (sum == 0b1101 ||
      sum == 0b0100 ||
      sum == 0b0010 ||
      sum == 0b1011)
  {
    move = 1;
  }

  if (sum == 0b1110 ||
      sum == 0b0111 ||
      sum == 0b0001 ||
      sum == 0b1000)
  {
    move = -1;
  }

  lastEncoded = encoded;

  if (move)
  {

    encoderSteps += move;

    if (abs(encoderSteps) >= 2)
    {
      encoderSteps = 0;
      return move;
    }
  }

  return 0;
}

// =================================================
// BUTTON DEBOUNCE
// =================================================

struct Button
{
  int pin;

  bool last;

  unsigned long timer;
};

Button buttons[5] =
    {
        {BTN1, false, 0},
        {BTN2, false, 0},
        {BTN3, false, 0},
        {BTN4, false, 0},
        {ENC_SW, false, 0}};

bool buttonPressed(int i)
{

  bool now = !digitalRead(buttons[i].pin);

  if (now != buttons[i].last)
  {

    if (millis() - buttons[i].timer > 60)
    {

      buttons[i].timer = millis();

      buttons[i].last = now;

      if (now)
        return true;
    }
  }

  return false;
}

// =================================================
// DRAW FUNCTIONS
// =================================================

void drawDeviceList()
{

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(ST77XX_YELLOW);

  tft.setCursor(10, 10);

  tft.println("DEVICES");

  for (int i = 0; i < deviceCount; i++)
  {

    tft.setCursor(10, 50 + i * 35);

    if (i == selected)
    {
      tft.setTextColor(ST77XX_GREEN);
      tft.print("> ");
    }
    else
    {
      tft.setTextColor(ST77XX_WHITE);
      tft.print("  ");
    }

    tft.println(devices[i].name);
  }
}

void drawHeader()
{

  tft.fillRect(0, 0, 240, 45, ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(ST77XX_YELLOW);

  tft.setCursor(10, 15);

  tft.println(devices[selected].name);
}

void drawLight()
{

  Device &d = devices[selected];

  tft.fillRect(0, 50, 240, 220, ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(50, 70);

  tft.println("Brightness");

  tft.drawRect(30, 120, 180, 20, ST77XX_WHITE);

  int w = map(d.value, 0, 100, 0, 176);

  tft.fillRect(32, 122, w, 16, ST77XX_GREEN);

  tft.setCursor(90, 170);

  tft.print(d.value);

  tft.print("%");
}

void drawFan()
{

  Device &d = devices[selected];

  tft.fillRect(0, 50, 240, 220, ST77XX_BLACK);

  int cx = 120;
  int cy = 140;

  int r = 60;

  tft.drawCircle(cx, cy, r, ST77XX_WHITE);

  float angle =
      map(d.value, 0, d.maxValue, -130, 130);

  float rad = angle * PI / 180;

  int x = cx + cos(rad) * r;
  int y = cy + sin(rad) * r;

  tft.drawLine(cx, cy, x, y, ST77XX_GREEN);

  tft.setTextSize(2);

  tft.setCursor(85, 220);

  tft.print("Speed ");

  tft.print(d.value);
}

void drawSensor()
{

  Device &d = devices[selected];

  tft.fillRect(0, 50, 240, 220, ST77XX_BLACK);

  tft.setTextSize(3);

  tft.setCursor(70, 120);

  tft.setTextColor(ST77XX_CYAN);

  tft.print(d.value);

  tft.print(" C");
}

void drawMedia()
{

  Device &d = devices[selected];

  tft.fillRect(0, 50, 240, 220, ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(70, 80);

  tft.println("Volume");

  tft.drawRect(30, 130, 180, 20, ST77XX_WHITE);

  int w = map(d.value, 0, 100, 0, 176);

  tft.fillRect(32, 132, w, 16, ST77XX_BLUE);

  tft.setCursor(100, 180);

  tft.print(d.value);

  tft.print("%");
}

void drawDevicePage()
{

  tft.fillScreen(ST77XX_BLACK);

  drawHeader();

  switch (devices[selected].type)
  {

  case LIGHT:
    drawLight();
    break;

  case FAN:
    drawFan();
    break;

  case SENSOR:
    drawSensor();
    break;

  case MEDIA:
    drawMedia();
    break;
  }
}

// =================================================
// ACTIONS
// =================================================

void enterPressed()
{

  if (currentScreen == DEVICE_LIST)
  {

    currentScreen = DEVICE_PAGE;

    drawDevicePage();
  }
  else
  {

    Device &d = devices[selected];

    if (d.controllable)
    {
      d.state = !d.state;
      Serial.println("State changed");
    }
  }
}

void backPressed()
{

  if (currentScreen == DEVICE_PAGE)
  {

    currentScreen = DEVICE_LIST;

    drawDeviceList();
  }
}

// =================================================
// SETUP
// =================================================

void setup()
{

  Serial.begin(115200);

  SPI.begin(
      TFT_SCLK,
      -1,
      TFT_MOSI,
      TFT_CS);

  tft.init(240, 320);

  tft.setRotation(0);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  lastEncoded =
      (digitalRead(ENC_CLK) << 1) |
      digitalRead(ENC_DT);

  drawDeviceList();
}

// =================================================
// LOOP
// =================================================

void loop()
{

  int move = readEncoder();

  if (move)
  {

    if (currentScreen == DEVICE_LIST)
    {

      selected += move;

      if (selected < 0)
        selected = deviceCount - 1;

      if (selected >= deviceCount)
        selected = 0;

      drawDeviceList();
    }
    else
    {

      Device &d = devices[selected];

      if (d.type == LIGHT || d.type == MEDIA)
      {
        d.value += move * 5;
      }

      if (d.type == FAN)
      {
        d.value += move;
      }

      if (d.value < 0)
        d.value = 0;

      if (d.value > d.maxValue)
        d.value = d.maxValue;

      switch (d.type)
      {

      case LIGHT:
        drawLight();
        break;

      case FAN:
        drawFan();
        break;

      case MEDIA:
        drawMedia();
        break;

      default:
        break;
      }
    }
  }

  // Buttons

  if (buttonPressed(0))
  {
    selected--;

    if (selected < 0)
      selected = deviceCount - 1;

    drawDeviceList();
  }

  if (buttonPressed(2))
  {
    selected++;

    if (selected >= deviceCount)
      selected = 0;

    drawDeviceList();
  }

  if (buttonPressed(3))
  {
    backPressed();
  }

  if (buttonPressed(4))
  {
    enterPressed();
  }
}