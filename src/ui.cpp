#include "ui.h"

#include <Arduino.h>
#include <Preferences.h>
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
constexpr const char *AREA_ALL_DEVICES = "All Devices";
constexpr const char *AREA_SETTINGS = "Settings";
constexpr const char *PREF_NAMESPACE = "smartknob";
constexpr const char *PREF_HOME_AREA = "home_area";

enum class UIState
{
  AreaList,
  HomeAreaPicker,
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
  String currentArea = ASSIGNED_AREA_NAME;
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
Preferences preferences;

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

bool isAllDevicesArea(const String &area)
{
  return area == AREA_ALL_DEVICES;
}

int areaListCount()
{
  return areaCount + 2;
}

int homeAreaPickerCount()
{
  return areaCount + 1;
}

String areaNameAt(int index)
{
  if (index >= 0 && index < areaCount)
  {
    return getArea(index);
  }

  if (index == areaCount)
  {
    return AREA_ALL_DEVICES;
  }

  return AREA_SETTINGS;
}

String selectableHomeAreaAt(int index)
{
  if (index >= 0 && index < areaCount)
  {
    return getArea(index);
  }

  return AREA_ALL_DEVICES;
}

int areaIndexForName(const String &area)
{
  for (int i = 0; i < areaCount; i++)
  {
    if (getArea(i) == area)
    {
      return i;
    }
  }

  return areaCount;
}

bool deviceInCurrentArea(int deviceIndex)
{
  if (deviceIndex < 0 || deviceIndex >= deviceCount)
  {
    return false;
  }

  return isAllDevicesArea(ui.currentArea) || devices[deviceIndex].area == ui.currentArea;
}

int visibleDeviceCount()
{
  int count = 0;

  for (int i = 0; i < deviceCount; i++)
  {
    if (deviceInCurrentArea(i))
    {
      count++;
    }
  }

  return count;
}

int deviceIndexForVisible(int visibleIndex)
{
  int visible = 0;

  for (int i = 0; i < deviceCount; i++)
  {
    if (!deviceInCurrentArea(i))
    {
      continue;
    }

    if (visible == visibleIndex)
    {
      return i;
    }

    visible++;
  }

  return -1;
}

int visibleIndexForDevice(int deviceIndex)
{
  int visible = 0;

  for (int i = 0; i < deviceCount; i++)
  {
    if (!deviceInCurrentArea(i))
    {
      continue;
    }

    if (i == deviceIndex)
    {
      return visible;
    }

    visible++;
  }

  return 0;
}

int currentListCount()
{
  if (ui.state == UIState::AreaList)
  {
    return areaListCount();
  }

  if (ui.state == UIState::HomeAreaPicker)
  {
    return homeAreaPickerCount();
  }

  if (ui.state == UIState::DevicesMenu)
  {
    return visibleDeviceCount();
  }

  return deviceCount;
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

  int deviceIndex = deviceIndexForVisible(index);

  if (deviceIndex < 0)
  {
    return;
  }

  const int y = rowY(index);
  const bool selected = index == ui.selectedIndex;
  Device &d = getDevice(deviceIndex);

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

void drawAreaRow(int index)
{
  if (index < ui.firstVisibleIndex || index >= ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS)
  {
    return;
  }

  const int y = rowY(index);
  const bool selected = index == ui.selectedIndex;
  String areaName = areaNameAt(index);
  int areaDevices = 0;
  bool settingsRow = areaName == AREA_SETTINGS;

  for (int i = 0; i < deviceCount; i++)
  {
    if (isAllDevicesArea(areaName) || devices[i].area == areaName)
    {
      areaDevices++;
    }
  }

  tft.fillRect(0, y - 8, SCREEN_W, 42, ST77XX_BLACK);
  if (selected)
  {
    tft.fillRoundRect(4, y - 7, LIST_RIGHT_EDGE - 8, 40, 6, UI_SELECTION_FILL);
    tft.drawRoundRect(4, y - 7, LIST_RIGHT_EDGE - 8, 40, 6, ST77XX_GREEN);
  }

  tft.setTextSize(2);
  tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
  tft.setCursor(14, y);
  tft.print(clippedTextToWidth(areaName, LIST_RIGHT_EDGE - 24, 2));

  tft.setTextSize(1);
  tft.setTextColor(settingsRow ? ST77XX_CYAN : UI_DARK_GREY);
  tft.setCursor(18, y + 22);

  if (settingsRow)
  {
    tft.print("choose home area");
  }
  else
  {
    tft.print(areaDevices);
    tft.print(areaDevices == 1 ? " device" : " devices");
  }
}

void drawHomeAreaRow(int index)
{
  if (index < ui.firstVisibleIndex || index >= ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS)
  {
    return;
  }

  const int y = rowY(index);
  const bool selected = index == ui.selectedIndex;
  String areaName = selectableHomeAreaAt(index);
  bool currentHome = areaName == ui.currentArea;

  tft.fillRect(0, y - 8, SCREEN_W, 42, ST77XX_BLACK);
  if (selected)
  {
    tft.fillRoundRect(4, y - 7, LIST_RIGHT_EDGE - 8, 40, 6, UI_SELECTION_FILL);
    tft.drawRoundRect(4, y - 7, LIST_RIGHT_EDGE - 8, 40, 6, ST77XX_GREEN);
  }

  tft.setTextSize(2);
  tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
  tft.setCursor(14, y);
  tft.print(clippedTextToWidth(areaName, LIST_RIGHT_EDGE - 24, 2));

  tft.setTextSize(1);
  tft.setTextColor(currentHome ? ST77XX_GREEN : UI_DARK_GREY);
  tft.setCursor(18, y + 22);
  tft.print(currentHome ? "current home" : "set as home");
}

void drawListScrollbar(int itemCount)
{
  tft.fillRect(SCROLLBAR_X - 4, SCROLLBAR_TOP - 4, 12, SCROLLBAR_BOTTOM - SCROLLBAR_TOP + 8, ST77XX_BLACK);

  if (itemCount <= VISIBLE_DEVICE_ROWS)
  {
    return;
  }

  const int16_t trackH = SCROLLBAR_BOTTOM - SCROLLBAR_TOP;
  const int maxFirst = max(1, itemCount - VISIBLE_DEVICE_ROWS);
  int16_t thumbH = max(24, (trackH * VISIBLE_DEVICE_ROWS) / itemCount);
  int16_t travel = trackH - thumbH;
  int16_t thumbY = SCROLLBAR_TOP + (travel * ui.firstVisibleIndex) / maxFirst;

  tft.drawFastVLine(SCROLLBAR_X, SCROLLBAR_TOP, trackH, UI_DARK_GREY);
  tft.fillRoundRect(SCROLLBAR_X - 2, thumbY, SCROLLBAR_W, thumbH, 2, ST77XX_GREEN);
}

void drawDeviceList(bool fullRedraw)
{
  int itemCount = visibleDeviceCount();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader(clippedTextToWidth(ui.currentArea, SCREEN_W - 20, 2).c_str());

    int lastVisible = min(itemCount, ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS);

    for (int i = ui.firstVisibleIndex; i < lastVisible; i++)
    {
      drawDeviceRow(i);
    }

    if (itemCount == 0)
    {
      drawCenteredText("No devices", 126, 2, UI_DARK_GREY);
      drawCenteredText("in this area", 152, 1, UI_DARK_GREY);
    }

    if (ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS < itemCount)
    {
      tft.setTextSize(1);
      tft.setTextColor(UI_DARK_GREY);
      tft.setCursor(198, 302);
      tft.print("more");
    }

    drawListScrollbar(itemCount);

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
      drawListScrollbar(itemCount);
    }
    else
    {
      drawDeviceList(true);
    }
  }
}

void drawAreaList(bool fullRedraw)
{
  int itemCount = areaListCount();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader("HOME");

    int lastVisible = min(itemCount, ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS);

    for (int i = ui.firstVisibleIndex; i < lastVisible; i++)
    {
      drawAreaRow(i);
    }

    drawListScrollbar(itemCount);
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
      drawAreaRow(ui.previousSelectedIndex);
      drawAreaRow(ui.selectedIndex);
      drawListScrollbar(itemCount);
    }
    else
    {
      drawAreaList(true);
    }
  }
}

void drawHomeAreaPicker(bool fullRedraw)
{
  int itemCount = homeAreaPickerCount();

  if (fullRedraw)
  {
    tft.fillScreen(ST77XX_BLACK);
    drawHeader("SETTINGS");

    int lastVisible = min(itemCount, ui.firstVisibleIndex + VISIBLE_DEVICE_ROWS);

    for (int i = ui.firstVisibleIndex; i < lastVisible; i++)
    {
      drawHomeAreaRow(i);
    }

    drawListScrollbar(itemCount);
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
      drawHomeAreaRow(ui.previousSelectedIndex);
      drawHomeAreaRow(ui.selectedIndex);
      drawListScrollbar(itemCount);
    }
    else
    {
      drawHomeAreaPicker(true);
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

int lightFieldCount(const Device &device)
{
  int count = 1;

  if (device.supportsColor)
  {
    count += 2;
  }

  if (device.supportsEffects)
  {
    count++;
  }

  return count;
}

int lightEffectField(const Device &device)
{
  if (!device.supportsEffects)
  {
    return -1;
  }

  return device.supportsColor ? 3 : 1;
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

  const char *labels[] = {"Brightness", "Saturation", "Hue"};
  float values[] = {d.value, d.saturation, d.hue};
  float maxValues[] = {100.0f, 100.0f, 360.0f};
  uint16_t colors[] = {ST77XX_YELLOW, ST77XX_CYAN, ST77XX_MAGENTA};
  int fieldCount = lightFieldCount(d);
  int effectField = lightEffectField(d);

  if (ui.lightField >= fieldCount)
  {
    ui.lightField = 0;
  }

  for (int i = 0; i < fieldCount; i++)
  {
    int y = 66 + i * 46;
    bool selected = i == ui.lightField;

    if (selected)
    {
      tft.drawRoundRect(18, y - 7, 204, 40, 5, ST77XX_GREEN);
    }

    tft.setTextSize(1);
    tft.setTextColor(selected ? ST77XX_GREEN : ST77XX_WHITE);
    tft.setCursor(28, y);

    if (i == effectField)
    {
      tft.print("Effect");

      String effectName = d.effectCount > 0 ? d.effects[d.effectIndex] : "None";
      tft.setTextColor(ST77XX_MAGENTA);
      tft.setCursor(28, y + 17);
      tft.print(clippedTextToWidth(effectName, 160, 1));

      tft.setTextColor(UI_DARK_GREY);
      tft.setCursor(190, y + 17);
      tft.print(d.effectIndex + 1);
      tft.print("/");
      tft.print(d.effectCount);
      continue;
    }

    int valueIndex = i;
    tft.print(labels[valueIndex]);

    tft.drawRect(28, y + 18, 150, 10, UI_DARK_GREY);
    int fillW = constrain((int)((values[valueIndex] / maxValues[valueIndex]) * 148.0f), 0, 148);
    tft.fillRect(29, y + 19, fillW, 8, colors[valueIndex]);

    tft.setTextSize(1);
    tft.setTextColor(colors[valueIndex]);
    tft.setCursor(186, y + 16);
    tft.print((int)round(values[valueIndex]));

    if (valueIndex != 2)
    {
      tft.print("%");
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(UI_DARK_GREY);
  tft.setCursor(fieldCount > 1 ? 28 : 42, 276);
  tft.print(fieldCount > 1 ? "Press knob: next field" : "Press knob to confirm");
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
    trendText += sensorValueText(Device{"", "", "", d.unit, DeviceType::Sensor, false, true, true, delta, 0, false, 0, 0, false, 0, 0, {}});

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
  case UIState::AreaList:
    drawAreaList(fullRedraw);
    break;
  case UIState::HomeAreaPicker:
    drawHomeAreaPicker(fullRedraw);
    break;
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
  int deviceIndex = deviceIndexForVisible(ui.selectedIndex);

  if (deviceIndex < 0)
  {
    return;
  }

  ui.activeDeviceIndex = deviceIndex;
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

void openAreaList()
{
  ui.selectedIndex = areaIndexForName(ui.currentArea);
  ui.previousSelectedIndex = ui.selectedIndex;
  ui.firstVisibleIndex = max(0, min(ui.selectedIndex, max(0, areaListCount() - VISIBLE_DEVICE_ROWS)));
  changeState(UIState::AreaList, UIState::DevicesMenu);
}

void openHomeAreaPicker()
{
  int selected = areaIndexForName(ui.currentArea);

  if (selected >= homeAreaPickerCount())
  {
    selected = homeAreaPickerCount() - 1;
  }

  ui.selectedIndex = max(0, selected);
  ui.previousSelectedIndex = ui.selectedIndex;
  ui.firstVisibleIndex = max(0, min(ui.selectedIndex, max(0, homeAreaPickerCount() - VISIBLE_DEVICE_ROWS)));
  changeState(UIState::HomeAreaPicker, UIState::AreaList);
}

void saveHomeArea(const String &area)
{
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putString(PREF_HOME_AREA, area);
  preferences.end();
  Serial.print("Home area saved: ");
  Serial.println(area);
}

String loadHomeArea()
{
  preferences.begin(PREF_NAMESPACE, true);
  String area = preferences.getString(PREF_HOME_AREA, ASSIGNED_AREA_NAME);
  preferences.end();
  area.trim();

  if (area.length() == 0)
  {
    return ASSIGNED_AREA_NAME;
  }

  return area;
}

void openSelectedArea()
{
  String areaName = areaNameAt(ui.selectedIndex);

  if (areaName == AREA_SETTINGS)
  {
    openHomeAreaPicker();
    return;
  }

  ui.currentArea = areaName;
  ui.selectedIndex = 0;
  ui.previousSelectedIndex = 0;
  ui.firstVisibleIndex = 0;
  changeState(UIState::DevicesMenu, UIState::AreaList);
}

void saveSelectedHomeArea()
{
  ui.currentArea = selectableHomeAreaAt(ui.selectedIndex);
  saveHomeArea(ui.currentArea);
  ui.selectedIndex = 0;
  ui.previousSelectedIndex = 0;
  ui.firstVisibleIndex = 0;
  changeState(UIState::DevicesMenu, UIState::HomeAreaPicker);
}

void returnToDevices()
{
  ui.selectedIndex = visibleIndexForDevice(ui.activeDeviceIndex);
  ui.previousSelectedIndex = ui.selectedIndex;
  ui.firstVisibleIndex = max(0, min(ui.firstVisibleIndex, max(0, visibleDeviceCount() - VISIBLE_DEVICE_ROWS)));
  changeState(UIState::DevicesMenu, UIState::DevicesMenu);
}

void moveSelection(int direction)
{
  int itemCount = currentListCount();

  if (itemCount == 0)
  {
    return;
  }

  ui.previousSelectedIndex = ui.selectedIndex;
  int oldFirstVisibleIndex = ui.firstVisibleIndex;
  ui.selectedIndex += direction;

  if (ui.selectedIndex < 0)
  {
    ui.selectedIndex = max(0, itemCount - 1);
  }

  if (ui.selectedIndex >= itemCount)
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
  int effectField = lightEffectField(d);

  if (ui.lightField == 0)
  {
    d.value += move * 5;
    d.value = constrain(d.value, 0.0f, 100.0f);
  }
  else if (ui.lightField == 1)
  {
    d.saturation += move * 5;
    d.saturation = constrain(d.saturation, 0.0f, 100.0f);
  }
  else if (ui.lightField == 2)
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
  else if (ui.lightField == effectField && d.effectCount > 0)
  {
    d.effectIndex += move;

    while (d.effectIndex < 0)
    {
      d.effectIndex += d.effectCount;
    }

    while (d.effectIndex >= d.effectCount)
    {
      d.effectIndex -= d.effectCount;
    }
  }
}

void handleMenuInput(const InputState &input)
{
  if (currentListCount() == 0)
  {
    if (input.back)
    {
      openAreaList();
    }

    return;
  }

  if (input.encoderMove)
  {
    moveSelection(input.encoderMove);
    renderCurrentScreen(false);
  }

  if (input.back)
  {
    openAreaList();
  }

  if (input.enter)
  {
    openSelectedDevice();
  }
}

void handleAreaInput(const InputState &input)
{
  if (input.encoderMove)
  {
    moveSelection(input.encoderMove);
    renderCurrentScreen(false);
  }

  if (input.back)
  {
    ui.selectedIndex = 0;
    ui.previousSelectedIndex = 0;
    ui.firstVisibleIndex = 0;
    changeState(UIState::DevicesMenu, UIState::AreaList);
  }

  if (input.enter)
  {
    openSelectedArea();
  }
}

void handleHomeAreaPickerInput(const InputState &input)
{
  if (input.encoderMove)
  {
    moveSelection(input.encoderMove);
    renderCurrentScreen(false);
  }

  if (input.back)
  {
    openAreaList();
  }

  if (input.enter)
  {
    saveSelectedHomeArea();
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
      ui.lightField = (ui.lightField + 1) % lightFieldCount(activeDevice());
      renderCurrentScreen(false);
    }

    if (input.back)
    {
      returnToDevices();
    }

    return;
  }

  if (ui.state == UIState::LightControl && activeDevice().supportsEffects)
  {
    if (input.encoderMove)
    {
      adjustLightValue(input.encoderMove);
      confirmDeviceValue(activeDevice());
      renderCurrentScreen(false);
    }

    if (input.enter)
    {
      ui.lightField = (ui.lightField + 1) % lightFieldCount(activeDevice());
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

  ui.currentArea = loadHomeArea();

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
  case UIState::AreaList:
    handleAreaInput(input);
    break;
  case UIState::HomeAreaPicker:
    handleHomeAreaPickerInput(input);
    break;
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
    int itemCount = currentListCount();

    if (ui.selectedIndex >= itemCount)
    {
      ui.selectedIndex = max(0, itemCount - 1);
    }

    ui.previousSelectedIndex = ui.selectedIndex;
    ui.firstVisibleIndex = max(0, min(ui.firstVisibleIndex, max(0, itemCount - VISIBLE_DEVICE_ROWS)));
    ui.requiresFullRedraw = true;
  }

  if (ui.requiresFullRedraw)
  {
    renderCurrentScreen(true);
    ui.requiresFullRedraw = false;
  }
}
