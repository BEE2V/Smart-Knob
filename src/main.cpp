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

constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;
constexpr uint16_t UI_DARK_GREY = 0x7BEF;

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

enum class DeviceType
{
  Light,
  Fan,
  Sensor,
  Media
};

struct Device
{
  const char *name;
  const char *area;
  DeviceType type;
  bool controllable;
  bool state;
  int value;
  int maxValue;
};

Device devices[] = {
    {"Living Room Light", "Living Room", DeviceType::Light, true, true, 80, 100},
    {"Bedroom Fan", "Bedroom", DeviceType::Fan, true, true, 3, 5},
    {"Temperature", "Bedroom", DeviceType::Sensor, false, true, 25, 50},
    {"Music Player", "Living Room", DeviceType::Media, true, false, 60, 100}};

const int deviceCount = sizeof(devices) / sizeof(Device);

// =================================================
// UI STATE
// =================================================

enum class UIState
{
  DevicesMenu,
  LightControl,
  FanControl,
  SensorDetails,
  MusicControl
};

struct UIContext
{
  UIState state = UIState::DevicesMenu;
  UIState parentState = UIState::DevicesMenu;
  int selectedIndex = 0;
  int previousSelectedIndex = 0;
  int activeDeviceIndex = 0;
  int originalValue = 0;
  bool requiresFullRedraw = true;
};

UIContext ui;

struct MusicState
{
  const char *track = "Evening Lights";
  const char *artist = "Local Mock";
  bool playing = false;
};

MusicState music;

// =================================================
// ENCODER
// =================================================

int lastEncoded = 0;
int encoderSteps = 0;

int readEncoder()
{
  int msb = digitalRead(ENC_CLK);
  int lsb = digitalRead(ENC_DT);
  int encoded = (msb << 1) | lsb;
  int sum = (lastEncoded << 2) | encoded;
  int move = 0;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
  {
    move = 1;
  }

  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
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

Button buttons[5] = {
    {BTN1, false, 0},
    {BTN2, false, 0},
    {BTN3, false, 0},
    {BTN4, false, 0},
    {ENC_SW, false, 0}};

bool buttonPressed(int i)
{
  bool now = !digitalRead(buttons[i].pin);

  if (now != buttons[i].last && millis() - buttons[i].timer > 60)
  {
    buttons[i].timer = millis();
    buttons[i].last = now;

    if (now)
    {
      return true;
    }
  }

  return false;
}

// =================================================
// DRAW HELPERS
// =================================================

uint16_t colorForDevice(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Light:
    return ST77XX_YELLOW;
  case DeviceType::Fan:
    return ST77XX_GREEN;
  case DeviceType::Sensor:
    return ST77XX_CYAN;
  case DeviceType::Media:
    return ST77XX_BLUE;
  }

  return ST77XX_WHITE;
}

int rowY(int index)
{
  return 54 + index * 42;
}

Device &activeDevice()
{
  return devices[ui.activeDeviceIndex];
}

void drawHeader(const char *title)
{
  tft.fillRect(0, 0, SCREEN_W, 44, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(10, 14);
  tft.println(title);
  tft.drawFastHLine(0, 44, SCREEN_W, ST77XX_BLUE);
}

void drawDeviceRow(int index)
{
  const int y = rowY(index);
  const bool selected = index == ui.selectedIndex;
  Device &d = devices[index];

  tft.fillRect(0, y - 4, SCREEN_W, 38, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
  tft.setCursor(10, y);
  tft.print(selected ? "> " : "  ");
  tft.println(d.name);

  tft.setTextSize(1);
  tft.setTextColor(colorForDevice(d.type));
  tft.setCursor(34, y + 22);
  tft.print(d.area);
}

void drawDeviceList(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader("DEVICES");

    for (int i = 0; i < deviceCount; i++)
    {
      drawDeviceRow(i);
    }

    return;
  }

  if (ui.previousSelectedIndex != ui.selectedIndex)
  {
    drawDeviceRow(ui.previousSelectedIndex);
    drawDeviceRow(ui.selectedIndex);
  }
}

void drawSliderControl(const char *label, uint16_t fillColor, bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(46, 78);
    tft.println(label);
    tft.drawRect(30, 130, 180, 20, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(42, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Press knob to confirm");
  }

  tft.fillRect(32, 132, 176, 16, ST77XX_BLACK);
  int w = map(d.value, 0, d.maxValue, 0, 176);
  tft.fillRect(32, 132, w, 16, fillColor);

  tft.fillRect(70, 176, 100, 34, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(fillColor);
  tft.setCursor(82, 178);
  tft.print(d.value);
  tft.print("%");
}

void drawFanControl(bool fullRedraw)
{
  Device &d = activeDevice();
  const int cx = 120;
  const int cy = 150;
  const int r = 62;

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name);
    tft.drawCircle(cx, cy, r, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(42, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Press knob to confirm");
  }

  tft.fillCircle(cx, cy, r - 2, ST77XX_BLACK);
  tft.drawCircle(cx, cy, r, ST77XX_WHITE);

  float angle = map(d.value, 0, d.maxValue, -130, 130);
  float rad = angle * PI / 180;
  int x = cx + cos(rad) * (r - 8);
  int y = cy + sin(rad) * (r - 8);

  tft.drawLine(cx, cy, x, y, ST77XX_GREEN);
  tft.fillCircle(cx, cy, 5, ST77XX_GREEN);

  tft.fillRect(60, 224, 130, 30, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(72, 228);
  tft.print("Speed ");
  tft.print(d.value);
}

void drawSensorDetails(bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name);
    tft.setTextSize(1);
    tft.setCursor(74, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Button 4: back");
  }

  tft.fillRect(45, 110, 150, 70, ST77XX_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(68, 122);
  tft.print(d.value);
  tft.print(" C");
}

void drawMusicControl(bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 78);
    tft.println(music.track);

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(22, 106);
    tft.println(music.artist);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(42, 224);
    tft.print("<   ");
    tft.print(music.playing ? "Pause" : "Play");
    tft.print("   >");

    tft.setTextSize(1);
    tft.setCursor(35, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Rotate: volume  Press: play");
  }

  tft.fillRect(35, 135, 170, 54, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(music.playing ? ST77XX_GREEN : ST77XX_RED);
  tft.setCursor(68, 136);
  tft.print(music.playing ? "Playing" : "Paused");

  tft.drawRect(30, 170, 180, 16, ST77XX_WHITE);
  tft.fillRect(32, 172, 176, 12, ST77XX_BLACK);
  int w = map(d.value, 0, d.maxValue, 0, 176);
  tft.fillRect(32, 172, w, 12, ST77XX_BLUE);
}

void renderCurrentScreen(bool fullRedraw)
{
  switch (ui.state)
  {
  case UIState::DevicesMenu:
    drawDeviceList(fullRedraw);
    break;
  case UIState::LightControl:
    drawSliderControl("Brightness", ST77XX_YELLOW, fullRedraw);
    break;
  case UIState::FanControl:
    drawFanControl(fullRedraw);
    break;
  case UIState::SensorDetails:
    drawSensorDetails(fullRedraw);
    break;
  case UIState::MusicControl:
    drawMusicControl(fullRedraw);
    break;
  }
}

void changeState(UIState newState, UIState parentState)
{
  ui.parentState = parentState;
  ui.state = newState;
  ui.requiresFullRedraw = true;
}

void openSelectedDevice()
{
  ui.activeDeviceIndex = ui.selectedIndex;
  ui.originalValue = activeDevice().value;

  switch (activeDevice().type)
  {
  case DeviceType::Light:
    changeState(UIState::LightControl, UIState::DevicesMenu);
    break;
  case DeviceType::Fan:
    changeState(UIState::FanControl, UIState::DevicesMenu);
    break;
  case DeviceType::Sensor:
    changeState(UIState::SensorDetails, UIState::DevicesMenu);
    break;
  case DeviceType::Media:
    changeState(UIState::MusicControl, UIState::DevicesMenu);
    break;
  }
}

void returnToDevices()
{
  ui.selectedIndex = ui.activeDeviceIndex;
  ui.previousSelectedIndex = ui.selectedIndex;
  changeState(UIState::DevicesMenu, UIState::DevicesMenu);
}

// =================================================
// INPUT HANDLING
// =================================================

void moveSelection(int direction)
{
  ui.previousSelectedIndex = ui.selectedIndex;
  ui.selectedIndex += direction;

  if (ui.selectedIndex < 0)
  {
    ui.selectedIndex = deviceCount - 1;
  }

  if (ui.selectedIndex >= deviceCount)
  {
    ui.selectedIndex = 0;
  }
}

void adjustActiveValue(int move)
{
  Device &d = activeDevice();
  int step = 1;

  if (d.type == DeviceType::Light || d.type == DeviceType::Media)
  {
    step = 5;
  }

  d.value += move * step;

  if (d.value < 0)
  {
    d.value = 0;
  }

  if (d.value > d.maxValue)
  {
    d.value = d.maxValue;
  }
}

void handleMenuInput(int encoderMove, bool btnUp, bool btnDown, bool btnBack, bool btnEnter)
{
  if (encoderMove)
  {
    moveSelection(encoderMove);
    renderCurrentScreen(false);
  }

  if (btnUp)
  {
    moveSelection(-1);
    renderCurrentScreen(false);
  }

  if (btnDown)
  {
    moveSelection(1);
    renderCurrentScreen(false);
  }

  if (btnBack)
  {
    ui.selectedIndex = 0;
    ui.previousSelectedIndex = 0;
    ui.requiresFullRedraw = true;
  }

  if (btnEnter)
  {
    openSelectedDevice();
  }
}

void handleControlInput(int encoderMove, bool btnBack, bool btnEnter)
{
  if (encoderMove && activeDevice().controllable)
  {
    adjustActiveValue(encoderMove);
    renderCurrentScreen(false);
  }

  if (btnBack)
  {
    activeDevice().value = ui.originalValue;
    returnToDevices();
  }

  if (btnEnter)
  {
    Serial.print("Confirmed ");
    Serial.print(activeDevice().name);
    Serial.print(": ");
    Serial.println(activeDevice().value);
    returnToDevices();
  }
}

void handleSensorInput(bool btnBack, bool btnEnter)
{
  if (btnBack || btnEnter)
  {
    returnToDevices();
  }
}

void handleMusicInput(int encoderMove, bool btnBack, bool btnEnter)
{
  if (encoderMove)
  {
    adjustActiveValue(encoderMove);
    renderCurrentScreen(false);
  }

  if (btnEnter)
  {
    music.playing = !music.playing;
    activeDevice().state = music.playing;
    renderCurrentScreen(true);
  }

  if (btnBack)
  {
    returnToDevices();
  }
}

void handleInput()
{
  int encoderMove = readEncoder();
  bool btnUp = buttonPressed(0);
  bool btnDown = buttonPressed(2);
  bool btnBack = buttonPressed(3);
  bool btnEnter = buttonPressed(4);

  switch (ui.state)
  {
  case UIState::DevicesMenu:
    handleMenuInput(encoderMove, btnUp, btnDown, btnBack, btnEnter);
    break;
  case UIState::LightControl:
  case UIState::FanControl:
    handleControlInput(encoderMove, btnBack, btnEnter);
    break;
  case UIState::SensorDetails:
    handleSensorInput(btnBack, btnEnter);
    break;
  case UIState::MusicControl:
    handleMusicInput(encoderMove, btnBack, btnEnter);
    break;
  }
}

// =================================================
// SETUP
// =================================================

void setup()
{
  Serial.begin(115200);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);
  tft.setRotation(0);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  lastEncoded = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
}

// =================================================
// LOOP
// =================================================

void loop()
{
  handleInput();

  if (ui.requiresFullRedraw)
  {
    renderCurrentScreen(true);
    ui.requiresFullRedraw = false;
  }
}
