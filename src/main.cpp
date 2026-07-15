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

  bool state;

  int value;
};

Device devices[] =
    {

        {"Living Room Light",
         LIGHT,
         true,
         80},

        {"Bedroom Fan",
         FAN,
         true,
         3},

        {"Temperature",
         SENSOR,
         true,
         25},

        {"Music Player",
         MEDIA,
         true,
         60}

};

const int deviceCount =
    sizeof(devices) / sizeof(Device);

int selected = 0;

bool screenChanged = true;

// =================================================
//                 ENCODER
// =================================================

int encoderSteps = 0;

int lastEncoded = 0;

void encoderTask()
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
  {
    movement = 1;
  }

  if (sum == 0b1110 ||
      sum == 0b0111 ||
      sum == 0b0001 ||
      sum == 0b1000)
  {
    movement = -1;
  }

  if (movement)
  {

    encoderSteps += movement;

    if (abs(encoderSteps) >= 2)
    {

      if (encoderSteps > 0)
        selected++;
      else
        selected--;

      encoderSteps = 0;

      if (selected < 0)
        selected = deviceCount - 1;

      if (selected >= deviceCount)
        selected = 0;

      screenChanged = true;
    }
  }

  lastEncoded = encoded;
}

// =================================================
//                 DEVICE ACTIONS
// =================================================

void showDevice()
{

  Device &d = devices[selected];

  Serial.print("Selected: ");
  Serial.println(d.name);

  switch (d.type)
  {

  case LIGHT:

    Serial.print("Brightness: ");
    Serial.println(d.value);

    break;

  case FAN:

    Serial.print("Speed: ");
    Serial.println(d.value);

    break;

  case SENSOR:

    Serial.print("Temperature: ");
    Serial.println(d.value);

    break;

  case MEDIA:

    Serial.print("Volume: ");
    Serial.println(d.value);

    break;
  }
}

void toggleDevice()
{

  Device &d = devices[selected];

  d.state = !d.state;

  Serial.print(d.name);

  Serial.println(
      d.state ? " ON" : " OFF");

  screenChanged = true;
}

// =================================================
//                 DRAW UI
// =================================================

void drawDevices()
{

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setCursor(10, 10);

  tft.setTextColor(ST77XX_YELLOW);

  tft.println("DEVICES");

  for (int i = 0; i < deviceCount; i++)
  {

    tft.setCursor(15, 50 + i * 35);

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
//                 BUTTONS
// =================================================

bool lastButtons[5] = {false, false, false, false, false};

void buttonsTask()
{

  bool state[5];

  state[0] = !digitalRead(BTN1);
  state[1] = !digitalRead(BTN2);
  state[2] = !digitalRead(BTN3);
  state[3] = !digitalRead(BTN4);
  state[4] = !digitalRead(ENC_SW);

  for (int i = 0; i < 5; i++)
  {

    if (state[i] && !lastButtons[i])
    {

      switch (i)
      {

      case 0:
        toggleDevice();
        break;

      case 1:

        selected--;

        if (selected < 0)
          selected = deviceCount - 1;

        screenChanged = true;

        break;

      case 2:

        selected++;

        if (selected >= deviceCount)
          selected = 0;

        screenChanged = true;

        break;

      case 4:

        showDevice();

        break;

      case 3:

        Serial.println("BTN4");

        break;
      }
    }

    lastButtons[i] = state[i];
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

  tft.fillScreen(ST77XX_BLACK);

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

  drawDevices();
}

// =================================================
//                     LOOP
// =================================================

void loop()
{

  encoderTask();

  buttonsTask();

  if (screenChanged)
  {

    drawDevices();

    screenChanged = false;
  }
}