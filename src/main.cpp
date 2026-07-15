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

// =================================================
//                 DEVICE SYSTEM
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
//                 UI STATE
// =================================================

enum Screen
{
  DEVICE_LIST,
  DEVICE_PAGE
};

Screen currentScreen = DEVICE_LIST;

int selected = 0;

bool screenChanged = true;

// =================================================
//                 ENCODER
// =================================================

int encoderSteps = 0;

int lastEncoded = 0;

int encoderMovement()
{

  int MSB = digitalRead(ENC_CLK);
  int LSB = digitalRead(ENC_DT);

  int encoded = (MSB << 1) | LSB;

  int sum = (lastEncoded << 2) | encoded;

  int movement = 0;

  if (sum == 0b1101 ||
      sum == 0b0100 ||
      sum == 0b0010 ||
      sum == 0b1011)
    movement = 1;

  if (sum == 0b1110 ||
      sum == 0b0111 ||
      sum == 0b0001 ||
      sum == 0b1000)
    movement = -1;

  lastEncoded = encoded;

  if (movement)
  {
    encoderSteps += movement;

    if (abs(encoderSteps) >= 2)
    {
      encoderSteps = 0;
      return movement;
    }
  }

  return 0;
}

// =================================================
//                 DRAW LIST
// =================================================

void drawDeviceList()
{

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setCursor(10, 10);

  tft.setTextColor(ST77XX_YELLOW);

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

// =================================================
//                 DRAW DEVICE PAGE
// =================================================

void drawDevicePage()
{

  Device &d = devices[selected];

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setTextColor(ST77XX_YELLOW);

  tft.setCursor(10, 10);

  tft.println(d.name);

  // -------- Light --------

  if (d.type == LIGHT)
  {

    tft.setCursor(40, 70);

    tft.setTextColor(ST77XX_WHITE);

    tft.println("Brightness");

    tft.drawRect(30, 120, 180, 20, ST77XX_WHITE);

    int width =
        map(d.value, 0, 100, 0, 176);

    tft.fillRect(32, 122, width, 16, ST77XX_GREEN);

    tft.setCursor(90, 170);

    tft.print(d.value);

    tft.println("%");
  }

  // -------- Fan --------

  if (d.type == FAN)
  {

    tft.setCursor(60, 90);

    tft.setTextSize(3);

    tft.setTextColor(ST77XX_CYAN);

    tft.print("SPEED ");

    tft.println(d.value);
  }

  // -------- Sensor --------

  if (d.type == SENSOR)
  {

    tft.setCursor(70, 100);

    tft.setTextSize(3);

    tft.setTextColor(ST77XX_WHITE);

    tft.print(d.value);

    tft.println(" C");
  }

  // -------- Media --------

  if (d.type == MEDIA)
  {

    tft.setCursor(50, 90);

    tft.setTextSize(2);

    tft.println("Volume");

    tft.drawRect(30, 130, 180, 20, ST77XX_WHITE);

    int width =
        map(d.value, 0, 100, 0, 176);

    tft.fillRect(32, 132, width, 16, ST77XX_BLUE);
  }
}

// =================================================
//                 BUTTON ACTIONS
// =================================================

void selectPressed()
{

  if (currentScreen == DEVICE_LIST)
  {

    currentScreen = DEVICE_PAGE;
  }
  else
  {

    Device &d = devices[selected];

    if (d.controllable)
    {
      d.state = !d.state;
    }
  }

  screenChanged = true;
}

void backPressed()
{

  if (currentScreen == DEVICE_PAGE)
  {

    currentScreen = DEVICE_LIST;

    screenChanged = true;
  }
}

// =================================================
//                 BUTTON HANDLER
// =================================================

bool lastButtons[5] = {0, 0, 0, 0, 0};

void buttonsTask()
{

  bool b[5];

  b[0] = !digitalRead(BTN1);
  b[1] = !digitalRead(BTN2);
  b[2] = !digitalRead(BTN3);
  b[3] = !digitalRead(BTN4);
  b[4] = !digitalRead(ENC_SW);

  for (int i = 0; i < 5; i++)
  {

    if (b[i] && !lastButtons[i])
    {

      if (i == 3)
        backPressed();

      if (i == 4)
        selectPressed();

      if (currentScreen == DEVICE_LIST)
      {

        if (i == 0)
        {
          selected--;

          if (selected < 0)
            selected = deviceCount - 1;

          screenChanged = true;
        }

        if (i == 2)
        {
          selected++;

          if (selected >= deviceCount)
            selected = 0;

          screenChanged = true;
        }
      }
    }

    lastButtons[i] = b[i];
  }
}

// =================================================
//                     SETUP
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
//                      LOOP
// =================================================

void loop()
{

  int move = encoderMovement();

  if (move)
  {

    if (currentScreen == DEVICE_LIST)
    {

      selected += move;

      if (selected < 0)
        selected = deviceCount - 1;

      if (selected >= deviceCount)
        selected = 0;
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
    }

    screenChanged = true;
  }

  buttonsTask();

  if (screenChanged)
  {

    if (currentScreen == DEVICE_LIST)
      drawDeviceList();

    else
      drawDevicePage();

    screenChanged = false;
  }
}