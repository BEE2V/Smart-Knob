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
constexpr int16_t LIST_RIGHT_EDGE = SCREEN_W - 16;
constexpr int16_t SCROLLBAR_X = SCREEN_W - 8;
constexpr int16_t SCROLLBAR_TOP = 52;
constexpr int16_t SCROLLBAR_BOTTOM = 296;
constexpr int16_t SCROLLBAR_W = 5;
constexpr int SENSOR_HISTORY_SIZE = 28;
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
  int lightField = 0;
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

float sensorHistory[MAX_DEVICES][SENSOR_HISTORY_SIZE];
int sensorHistoryCount[MAX_DEVICES] = {0};
int sensorHistoryNext[MAX_DEVICES] = {0};
unsigned long sensorHistoryLastAppend[MAX_DEVICES] = {0};
bool sensorHistoryLoaded[MAX_DEVICES] = {false};

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

String clippedTextToWidth(const String &text, int16_t maxWidth, uint8_t textSize)
{
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;

  tft.setTextSize(textSize);
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  if (w <= maxWidth)
  {
    return text;
  }

  String clipped = text;

  while (clipped.length() > 1)
  {
    clipped.remove(clipped.length() - 1);
    String candidate = clipped + "~";
    tft.getTextBounds(candidate, 0, 0, &x1, &y1, &w, &h);

    if (w <= maxWidth)
    {
      return candidate;
    }
  }

  return "~";
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

String axisValueText(float value)
{
  if (fabs(value) < 10.0f)
  {
    return String(value, 2);
  }

  return String(value, 1);
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
  tft.setCursor(((SCREEN_W - w) / 2) - x1, y);
  tft.print(text);
}

bool latestSensorDelta(int index, float &delta)
{
  if (index < 0 || index >= MAX_DEVICES || sensorHistoryCount[index] < 2)
  {
    return false;
  }

  int latestIndex = sensorHistoryNext[index] - 1;
  int previousIndex = sensorHistoryNext[index] - 2;

  if (latestIndex < 0)
  {
    latestIndex += SENSOR_HISTORY_SIZE;
  }

  if (previousIndex < 0)
  {
    previousIndex += SENSOR_HISTORY_SIZE;
  }

  delta = sensorHistory[index][latestIndex] - sensorHistory[index][previousIndex];
  return true;
}

bool sensorHistoryRange(int index, float &minValue, float &maxValue)
{
  if (index < 0 || index >= MAX_DEVICES || sensorHistoryCount[index] == 0)
  {
    return false;
  }

  int firstIndex = (sensorHistoryNext[index] - sensorHistoryCount[index] + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
  minValue = sensorHistory[index][firstIndex];
  maxValue = sensorHistory[index][firstIndex];

  for (int i = 0; i < sensorHistoryCount[index]; i++)
  {
    int sampleIndex = (sensorHistoryNext[index] - sensorHistoryCount[index] + i + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
    float value = sensorHistory[index][sampleIndex];
    minValue = min(minValue, value);
    maxValue = max(maxValue, value);
  }

  return true;
}

void drawTrendArrow(float delta)
{
  const int16_t x = 196;
  const int16_t y = 116;

  if (fabs(delta) < 0.01f)
  {
    tft.drawFastHLine(x - 7, y, 14, UI_DARK_GREY);
    return;
  }

  if (delta > 0)
  {
    tft.fillTriangle(x, y - 10, x - 8, y + 8, x + 8, y + 8, ST77XX_GREEN);
  }
  else
  {
    tft.fillTriangle(x, y + 10, x - 8, y - 8, x + 8, y - 8, ST77XX_RED);
  }
}

void recordSensorSample(int index, float value)
{
  if (index < 0 || index >= MAX_DEVICES)
  {
    return;
  }

  sensorHistory[index][sensorHistoryNext[index]] = value;
  sensorHistoryNext[index] = (sensorHistoryNext[index] + 1) % SENSOR_HISTORY_SIZE;

  if (sensorHistoryCount[index] < SENSOR_HISTORY_SIZE)
  {
    sensorHistoryCount[index]++;
  }
}

unsigned long sensorHistoryBucketMs()
{
  return max(1000UL, (HA_HISTORY_MINUTES * 60UL * 1000UL) / SENSOR_HISTORY_SIZE);
}

void updateLatestSensorSample(int index, float value)
{
  if (index < 0 || index >= MAX_DEVICES)
  {
    return;
  }

  if (sensorHistoryCount[index] == 0)
  {
    recordSensorSample(index, value);
    sensorHistoryLastAppend[index] = millis();
    return;
  }

  unsigned long currentMillis = millis();

  if (currentMillis - sensorHistoryLastAppend[index] >= sensorHistoryBucketMs())
  {
    recordSensorSample(index, value);
    sensorHistoryLastAppend[index] = currentMillis;
    return;
  }

  int latestIndex = sensorHistoryNext[index] - 1;

  if (latestIndex < 0)
  {
    latestIndex = SENSOR_HISTORY_SIZE - 1;
  }

  sensorHistory[index][latestIndex] = value;
}

void clearSensorHistory(int index)
{
  if (index < 0 || index >= MAX_DEVICES)
  {
    return;
  }

  sensorHistoryCount[index] = 0;
  sensorHistoryNext[index] = 0;
  sensorHistoryLastAppend[index] = 0;
  sensorHistoryLoaded[index] = false;
}

void seedSensorHistoryFromHomeAssistant(int index, const Device &device)
{
  if (index < 0 || index >= MAX_DEVICES)
  {
    return;
  }

  float samples[SENSOR_HISTORY_SIZE];
  int count = fetchHomeAssistantHistory(device, samples, SENSOR_HISTORY_SIZE);

  clearSensorHistory(index);

  if (count == 0)
  {
    recordSensorSample(index, device.value);
    sensorHistoryLoaded[index] = false;
    return;
  }

  for (int i = 0; i < count; i++)
  {
    recordSensorSample(index, samples[i]);
  }

  sensorHistoryLastAppend[index] = millis();
  sensorHistoryLoaded[index] = true;
}

void drawSensorGraph(int index)
{
  if (index < 0 || index >= MAX_DEVICES || sensorHistoryCount[index] < 2)
  {
    const int16_t x = 44;
    const int16_t y = 184;
    const int16_t w = 174;
    const int16_t h = 78;

    tft.fillRect(0, y - 12, SCREEN_W, h + 38, ST77XX_BLACK);
    tft.drawRoundRect(x, y, w, h, 4, UI_DARK_GREY);
    drawCenteredText("Graph", y + 16, 2, UI_DARK_GREY);
    drawCenteredText("loading...", y + 40, 1, UI_DARK_GREY);
    return;
  }

  const int16_t x = 44;
  const int16_t y = 184;
  const int16_t w = 174;
  const int16_t h = 78;

  float minValue = sensorHistory[index][0];
  float maxValue = sensorHistory[index][0];

  for (int i = 0; i < sensorHistoryCount[index]; i++)
  {
    float value = sensorHistory[index][i];
    minValue = min(minValue, value);
    maxValue = max(maxValue, value);
  }

  if (fabs(maxValue - minValue) < 0.01f)
  {
    maxValue = minValue + 1.0f;
  }

  tft.fillRect(0, y - 12, SCREEN_W, h + 38, ST77XX_BLACK);
  tft.drawRoundRect(x, y, w, h, 4, UI_DARK_GREY);

  tft.setTextSize(1);
  tft.setTextColor(UI_DARK_GREY);
  tft.setCursor(2, y + 2);
  tft.print(axisValueText(maxValue));
  tft.setCursor(2, y + h - 10);
  tft.print(axisValueText(minValue));
  tft.setCursor(x, y + h + 8);
  tft.print("-");
  tft.print(HA_HISTORY_MINUTES);
  tft.print("m");
  tft.setCursor(x + w - 18, y + h + 8);
  tft.print("now");

  int previousX = 0;
  int previousY = 0;

  for (int i = 0; i < sensorHistoryCount[index]; i++)
  {
    int sampleIndex = (sensorHistoryNext[index] - sensorHistoryCount[index] + i + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
    float value = sensorHistory[index][sampleIndex];
    int pointX = x + 6 + ((w - 12) * i) / max(1, sensorHistoryCount[index] - 1);
    int pointY = y + h - 7 - (int)(((value - minValue) / (maxValue - minValue)) * (h - 14));

    if (i > 0)
    {
      tft.drawLine(previousX, previousY, pointX, pointY, ST77XX_CYAN);
    }

    previousX = pointX;
    previousY = pointY;
  }
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
    tft.fillRoundRect(4, y - 7, LIST_RIGHT_EDGE - 8, 40, 6, UI_SELECTION_FILL);
    tft.drawRoundRect(4, y - 7, LIST_RIGHT_EDGE - 8, 40, 6, ST77XX_GREEN);
  }

  tft.setTextSize(2);
  tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
  tft.setCursor(14, y);
  tft.print(clippedTextToWidth(d.name, LIST_RIGHT_EDGE - 24, 2));

  tft.setTextSize(1);
  tft.fillCircle(18, y + 25, 3, d.available ? ST77XX_GREEN : ST77XX_YELLOW);
  tft.setTextColor(d.available ? UI_DARK_GREY : ST77XX_YELLOW);
  tft.setCursor(28, y + 21);
  tft.print(d.available ? "online" : "offline");
}

void drawDeviceScrollbar()
{
  tft.fillRect(SCROLLBAR_X - 4, SCROLLBAR_TOP - 4, 12, SCROLLBAR_BOTTOM - SCROLLBAR_TOP + 8, ST77XX_BLACK);

  if (deviceCount <= VISIBLE_DEVICE_ROWS)
  {
    return;
  }

  const int16_t trackH = SCROLLBAR_BOTTOM - SCROLLBAR_TOP;
  const int maxFirst = max(1, deviceCount - VISIBLE_DEVICE_ROWS);
  int16_t thumbH = max(24, (trackH * VISIBLE_DEVICE_ROWS) / deviceCount);
  int16_t travel = trackH - thumbH;
  int16_t thumbY = SCROLLBAR_TOP + (travel * ui.firstVisibleIndex) / maxFirst;

  tft.drawFastVLine(SCROLLBAR_X, SCROLLBAR_TOP, trackH, UI_DARK_GREY);
  tft.fillRoundRect(SCROLLBAR_X - 2, thumbY, SCROLLBAR_W, thumbH, 2, ST77XX_GREEN);
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

    drawDeviceScrollbar();

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
      drawDeviceScrollbar();
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

void drawLightControl(bool fullRedraw)
{
  Device &d = activeDevice();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(d.name.c_str());
  }

  tft.fillRect(0, 58, SCREEN_W, 220, ST77XX_BLACK);

  const char *labels[] = {"Brightness", "Hue", "Saturation"};
  float values[] = {d.value, d.hue, d.saturation};
  float maxValues[] = {100.0f, 360.0f, 100.0f};
  uint16_t colors[] = {ST77XX_YELLOW, ST77XX_MAGENTA, ST77XX_CYAN};
  int fieldCount = d.supportsColor ? 3 : 1;

  for (int i = 0; i < fieldCount; i++)
  {
    int y = 78 + i * 58;
    bool selected = i == ui.lightField;

    if (selected)
    {
      tft.drawRoundRect(18, y - 8, 204, 46, 5, ST77XX_GREEN);
    }

    tft.setTextSize(1);
    tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
    tft.setCursor(28, y);
    tft.print(labels[i]);

    tft.drawRect(28, y + 18, 150, 10, UI_DARK_GREY);
    int fillW = constrain((int)((values[i] / maxValues[i]) * 148.0f), 0, 148);
    tft.fillRect(29, y + 19, fillW, 8, colors[i]);

    tft.setTextSize(1);
    tft.setTextColor(colors[i]);
    tft.setCursor(186, y + 16);
    tft.print((int)round(values[i]));

    if (i != 1)
    {
      tft.print("%");
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(UI_DARK_GREY);
  tft.setCursor(d.supportsColor ? 28 : 42, 276);
  tft.print(d.supportsColor ? "Press knob: next field" : "Press knob to confirm");
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
  float delta = 0;
  bool hasDelta = latestSensorDelta(ui.activeDeviceIndex, delta);
  float minValue = 0;
  float maxValue = 0;
  bool hasRange = sensorHistoryRange(ui.activeDeviceIndex, minValue, maxValue);

  tft.fillRect(0, 58, SCREEN_W, 118, ST77XX_BLACK);
  drawCenteredText(value, 82, value.length() > 7 ? 3 : 4, ST77XX_CYAN);

  if (hasDelta)
  {
    drawTrendArrow(delta);

    String trendText = delta > 0 ? "+" : "";
    trendText += sensorValueText(Device{"", "", "", d.unit, DeviceType::Sensor, false, true, true, delta, 0, false, 0, 0});

    tft.setTextSize(1);
    tft.setTextColor(delta > 0.01f ? ST77XX_GREEN : (delta < -0.01f ? ST77XX_RED : UI_DARK_GREY));
    tft.setCursor(20, 146);
    tft.print("Trend ");
    tft.print(trendText);
  }

  if (hasRange)
  {
    tft.setTextSize(1);
    tft.setTextColor(UI_DARK_GREY);
    tft.setCursor(20, 160);
    tft.print("Range ");
    tft.print(axisValueText(minValue));
    tft.print("-");
    tft.print(axisValueText(maxValue));

    if (d.unit.length() > 0)
    {
      tft.print(" ");
      tft.print(d.unit);
    }
  }

  if (fullRedraw && !sensorHistoryLoaded[ui.activeDeviceIndex])
  {
    drawSensorGraph(ui.activeDeviceIndex);
    seedSensorHistoryFromHomeAssistant(ui.activeDeviceIndex, d);
  }

  drawSensorGraph(ui.activeDeviceIndex);
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
    drawLightControl(fullRedraw);
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
  ui.lightField = 0;

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

void adjustLightValue(int move)
{
  Device &d = activeDevice();

  if (ui.lightField == 0)
  {
    d.value += move * 5;
    d.value = constrain(d.value, 0.0f, 100.0f);
  }
  else if (ui.lightField == 1)
  {
    d.hue += move * 8;

    if (d.hue < 0)
    {
      d.hue += 360;
    }

    if (d.hue >= 360)
    {
      d.hue -= 360;
    }
  }
  else
  {
    d.saturation += move * 5;
    d.saturation = constrain(d.saturation, 0.0f, 100.0f);
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

  if (ui.state == UIState::LightControl && activeDevice().supportsColor)
  {
    if (input.encoderMove)
    {
      adjustLightValue(input.encoderMove);
      confirmDeviceValue(activeDevice());
      renderCurrentScreen(false);
    }

    if (input.enter)
    {
      ui.lightField = (ui.lightField + 1) % 3;
      renderCurrentScreen(false);
    }

    if (input.back)
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
    updateLatestSensorSample(ui.activeDeviceIndex, activeDevice().value);
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
