#include "ui.h"

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "config.h"
#include "device_control.h"
#include "devices.h"
#include "homeassistant.h"

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

constexpr int VISIBLE_DEVICE_ROWS = 6;
constexpr unsigned long ACTIVE_SENSOR_REFRESH_MS = 2000;
constexpr unsigned long SHORTCUT_POPUP_MS = 900;
constexpr uint16_t UI_SELECTION_FILL = 0x0841;

enum class UIState
{
  DevicesMenu,
  LightControl,
  FanControl,
  SensorDetails,
  BinarySensorDetails,
  MusicControl
};

struct UIContext
{
  UIState state = UIState::DevicesMenu;
  UIState parentState = UIState::DevicesMenu;
  int selectedIndex = 0;
  int previousSelectedIndex = 0;
  int firstVisibleIndex = 0;
  int activeDeviceIndex = 0;
  int originalValue = 0;
  unsigned long lastDeviceRevision = 0;
  unsigned long lastActiveRefresh = 0;
  unsigned long popupUntil = 0;
  bool requiresFullRedraw = true;
  bool popupActive = false;
};

struct MusicState
{
  const char *track = "Evening Lights";
  const char *artist = "Local Mock";
  bool playing = false;
};

UIContext ui;
MusicState music;

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
  case DeviceType::BinarySensor:
    return ST77XX_MAGENTA;
  case DeviceType::Media:
    return ST77XX_BLUE;
  }

  return ST77XX_WHITE;
}

int rowY(int index)
{
  return 54 + (index - ui.firstVisibleIndex) * 42;
}

Device &activeDevice()
{
  return getDevice(ui.activeDeviceIndex);
}

String clippedText(const String &text, int maxChars)
{
  if (text.length() <= maxChars)
  {
    return text;
  }

  return text.substring(0, maxChars - 1) + "~";
}

String sensorValueText(const Device &device)
{
  String value;

  if (device.unit == "V")
  {
    value = String(device.value, 2);
  }
  else if (fabs(device.value - round(device.value)) < 0.05f)
  {
    value = String((int)round(device.value));
  }
  else
  {
    value = String(device.value, 1);
  }

  if (device.unit.length() > 0)
  {
    value += " ";
    value += device.unit;
  }

  return value;
}

void drawCenteredText(const String &text, int16_t y, uint8_t size, uint16_t color)
{
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;

  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  tft.setCursor((SCREEN_W - w) / 2, y);
  tft.print(text);
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

void drawShortcutPopup(int shortcutNumber, bool longPress, bool success)
{
  const int16_t x = 24;
  const int16_t y = 100;
  const int16_t w = 192;
  const int16_t h = 96;

  tft.fillRoundRect(x, y, w, h, 8, ST77XX_BLACK);
  tft.drawRoundRect(x, y, w, h, 8, success ? ST77XX_GREEN : ST77XX_RED);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(x + 34, y + 18);
  tft.print("Shortcut ");
  tft.print(shortcutNumber);

  tft.setTextSize(1);
  tft.setTextColor(UI_DARK_GREY);
  tft.setCursor(x + 58, y + 48);
  tft.print(longPress ? "long press" : "short press");

  tft.setTextSize(2);
  tft.setTextColor(success ? ST77XX_GREEN : ST77XX_RED);
  tft.setCursor(x + (success ? 76 : 64), y + 66);
  tft.print(success ? "SENT" : "FAILED");

  ui.popupActive = true;
  ui.popupUntil = millis() + SHORTCUT_POPUP_MS;
}

void drawDeviceRow(int index)
{
  if (index < ui.firstVisibleIndex || index >= ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS)
  {
    return;
  }

  const int y = rowY(index);
  const bool selected = index == ui.selectedIndex;
  Device &d = getDevice(index);

  tft.fillRect(0, y - 8, SCREEN_W, 42, ST77XX_BLACK);
  if (selected)
  {
    tft.fillRoundRect(4, y - 7, SCREEN_W - 8, 40, 6, UI_SELECTION_FILL);
    tft.drawRoundRect(4, y - 7, SCREEN_W - 8, 40, 6, ST77XX_GREEN);
  }

  tft.setTextSize(2);
  tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
  tft.setCursor(14, y);
  tft.print(clippedText(d.name, 19));

  tft.setTextSize(1);
  tft.fillCircle(18, y + 25, 3, d.available ? ST77XX_GREEN : ST77XX_YELLOW);
  tft.setTextColor(d.available ? UI_DARK_GREY : ST77XX_YELLOW);
  tft.setCursor(28, y + 21);
  tft.print(d.available ? "online" : "offline");
}

void drawDeviceList(bool fullRedraw)
{
  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader("DEVICES");

    int lastVisible = min(deviceCount, ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS);

    for (int i = ui.firstVisibleIndex; i < lastVisible; i++)
    {
      drawDeviceRow(i);
    }

    if (ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS < deviceCount)
    {
      tft.setTextSize(1);
      tft.setTextColor(UI_DARK_GREY);
      tft.setCursor(198, 302);
      tft.print("more");
    }

    return;
  }

  if (ui.previousSelectedIndex != ui.selectedIndex)
  {
    bool oldVisible = ui.previousSelectedIndex >= ui.firstVisibleIndex &&
                      ui.previousSelectedIndex < ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS;
    bool newVisible = ui.selectedIndex >= ui.firstVisibleIndex &&
                      ui.selectedIndex < ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS;

    if (oldVisible && newVisible)
    {
      drawDeviceRow(ui.previousSelectedIndex);
      drawDeviceRow(ui.selectedIndex);
    }
    else
    {
      drawDeviceList(true);
    }
  }
}

void drawSliderControl(const char *label, uint16_t fillColor, bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name.c_str());

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
  int w = map((int)round(d.value), 0, (int)round(d.maxValue), 0, 176);
  tft.fillRect(32, 132, w, 16, fillColor);

  tft.fillRect(70, 176, 100, 34, ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(fillColor);
  tft.setCursor(82, 178);
  tft.print((int)round(d.value));
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
    drawHeader(d.name.c_str());
    tft.drawCircle(cx, cy, r, ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(42, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Press knob to confirm");
  }

  tft.fillCircle(cx, cy, r - 2, ST77XX_BLACK);
  tft.drawCircle(cx, cy, r, ST77XX_WHITE);

  float angle = map((int)round(d.value), 0, (int)round(d.maxValue), -130, 130);
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
  tft.print((int)round(d.value));
}

void drawSensorDetails(bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name.c_str());
    tft.setTextSize(1);
    tft.setCursor(74, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Button 4: back");
  }

  String value = sensorValueText(d);

  tft.fillRect(0, 104, SCREEN_W, 86, ST77XX_BLACK);
  drawCenteredText(value, 124, value.length() > 6 ? 3 : 4, ST77XX_CYAN);
}

void drawBinarySensorDetails(bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name.c_str());
    tft.setTextSize(1);
    tft.setCursor(74, 276);
    tft.setTextColor(UI_DARK_GREY);
    tft.print("Button 4: back");
  }

  tft.fillRect(0, 104, SCREEN_W, 86, ST77XX_BLACK);
  drawCenteredText(d.state ? "Detected" : "Clear", 126, 3, d.state ? ST77XX_MAGENTA : ST77XX_GREEN);
}

void drawMusicControl(bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name.c_str());

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
  int w = map((int)round(d.value), 0, (int)round(d.maxValue), 0, 176);
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
  case UIState::BinarySensorDetails:
    drawBinarySensorDetails(fullRedraw);
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
  ui.lastActiveRefresh = 0;
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
  case DeviceType::BinarySensor:
    changeState(UIState::BinarySensorDetails, UIState::DevicesMenu);
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
  ui.firstVisibleIndex = max(0, min(ui.firstVisibleIndex, max(0, deviceCount - VISIBLE_DEVICE_ROWS)));
  changeState(UIState::DevicesMenu, UIState::DevicesMenu);
}

void moveSelection(int direction)
{
  ui.previousSelectedIndex = ui.selectedIndex;
  int oldFirstVisibleIndex = ui.firstVisibleIndex;
  ui.selectedIndex += direction;

  if (ui.selectedIndex < 0)
  {
    ui.selectedIndex = max(0, deviceCount - 1);
  }

  if (ui.selectedIndex >= deviceCount)
  {
    ui.selectedIndex = 0;
  }

  if (ui.selectedIndex < ui.firstVisibleIndex)
  {
    ui.firstVisibleIndex = ui.selectedIndex;
  }

  if (ui.selectedIndex >= ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS)
  {
    ui.firstVisibleIndex = ui.selectedIndex - VISIBLE_DEVICE_ROWS + 1;
  }

  if (oldFirstVisibleIndex != ui.firstVisibleIndex)
  {
    ui.requiresFullRedraw = true;
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

void handleMenuInput(const InputState &input)
{
  if (deviceCount == 0)
  {
    return;
  }

  if (input.encoderMove)
  {
    moveSelection(input.encoderMove);
    renderCurrentScreen(false);
  }

  if (input.back)
  {
    ui.selectedIndex = 0;
    ui.previousSelectedIndex = 0;
    ui.requiresFullRedraw = true;
  }

  if (input.enter)
  {
    openSelectedDevice();
  }
}

bool handleShortcutInput(const InputState &input)
{
  int shortcutNumber = 0;
  bool longPress = false;

  if (input.shortcut1 || input.shortcut1Long)
  {
    shortcutNumber = 1;
    longPress = input.shortcut1Long;
  }
  else if (input.shortcut2 || input.shortcut2Long)
  {
    shortcutNumber = 2;
    longPress = input.shortcut2Long;
  }
  else if (input.shortcut3 || input.shortcut3Long)
  {
    shortcutNumber = 3;
    longPress = input.shortcut3Long;
  }

  if (shortcutNumber == 0)
  {
    return false;
  }

  bool success = sendShortcutEventToHomeAssistant(shortcutNumber, longPress);
  drawShortcutPopup(shortcutNumber, longPress, success);
  return true;
}

void handleControlInput(const InputState &input)
{
  if (!activeDevice().available)
  {
    if (input.back || input.enter)
    {
      returnToDevices();
    }
    return;
  }

  if (input.encoderMove && activeDevice().controllable)
  {
    adjustActiveValue(input.encoderMove);
    renderCurrentScreen(false);
  }

  if (input.back)
  {
    activeDevice().value = ui.originalValue;
    returnToDevices();
  }

  if (input.enter)
  {
    confirmDeviceValue(activeDevice());
    returnToDevices();
  }
}

void handleSensorInput(const InputState &input)
{
  if (input.back || input.enter)
  {
    returnToDevices();
  }
}

void refreshActiveSensorIfNeeded()
{
  if (ui.state != UIState::SensorDetails && ui.state != UIState::BinarySensorDetails)
  {
    return;
  }

  if (millis() - ui.lastActiveRefresh < ACTIVE_SENSOR_REFRESH_MS)
  {
    return;
  }

  ui.lastActiveRefresh = millis();

  if (refreshHomeAssistantEntity(activeDevice()))
  {
    renderCurrentScreen(false);
  }
}

void handleMusicInput(const InputState &input)
{
  if (!activeDevice().available)
  {
    if (input.back || input.enter)
    {
      returnToDevices();
    }
    return;
  }

  if (input.encoderMove)
  {
    adjustActiveValue(input.encoderMove);
    setMediaVolume(activeDevice());
    renderCurrentScreen(false);
  }

  if (input.enter)
  {
    music.playing = !music.playing;
    setMediaPlaying(activeDevice(), music.playing);
    renderCurrentScreen(true);
  }

  if (input.back)
  {
    returnToDevices();
  }
}

void initUI()
{
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(0);
  tft.setTextWrap(false);

  ui.requiresFullRedraw = true;
}

void handleUIInput(const InputState &input)
{
  if (handleShortcutInput(input))
  {
    return;
  }

  if (input.backLong)
  {
    returnToDevices();
    return;
  }

  switch (ui.state)
  {
  case UIState::DevicesMenu:
    handleMenuInput(input);
    break;
  case UIState::LightControl:
  case UIState::FanControl:
    handleControlInput(input);
    break;
  case UIState::SensorDetails:
  case UIState::BinarySensorDetails:
    handleSensorInput(input);
    break;
  case UIState::MusicControl:
    handleMusicInput(input);
    break;
  }
}

void renderUI()
{
  refreshActiveSensorIfNeeded();

  if (ui.popupActive && millis() > ui.popupUntil)
  {
    ui.popupActive = false;
    ui.requiresFullRedraw = true;
  }

  if (ui.lastDeviceRevision != deviceRevision)
  {
    ui.lastDeviceRevision = deviceRevision;

    if (ui.selectedIndex >= deviceCount)
    {
      ui.selectedIndex = max(0, deviceCount - 1);
    }

    ui.previousSelectedIndex = ui.selectedIndex;
    ui.firstVisibleIndex = max(0, min(ui.firstVisibleIndex, max(0, deviceCount - VISIBLE_DEVICE_ROWS)));
    ui.requiresFullRedraw = true;
  }

  if (ui.requiresFullRedraw)
  {
    renderCurrentScreen(true);
    ui.requiresFullRedraw = false;
  }
}
