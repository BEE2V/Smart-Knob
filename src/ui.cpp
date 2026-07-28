#include "ui.h"

#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <lvgl.h>

#include "battery.h"
#include "config.h"
#include "device_control.h"
#include "devices.h"
#include "homeassistant.h"
#include "power_management.h"

namespace
{
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
Preferences preferences;

constexpr int VISIBLE_ROWS = 6;
constexpr int SENSOR_HISTORY_SIZE = 28;
constexpr unsigned long ACTIVE_SENSOR_REFRESH_MS = 2000;
constexpr unsigned long POPUP_MS = 1000;
constexpr unsigned long CONTROL_SEND_DELAY_MS = 180;
constexpr const char *AREA_ALL_DEVICES = "All Devices";
constexpr const char *AREA_SETTINGS = "Settings";
constexpr const char *PREF_NAMESPACE = "smartknob";
constexpr const char *PREF_HOME_AREA = "home_area";
constexpr const char *PREF_SLEEP_SECONDS = "sleep_s";
constexpr int SETTINGS_COUNT = 5;
constexpr int SLEEP_OPTION_COUNT = 7;
constexpr uint16_t DRAW_ROWS = 24;

const char *settingsLabels[SETTINGS_COUNT] = {
    "Refresh", "Home Area", "Sleep Timer", "Battery", "Reboot"};
const char *sleepLabels[SLEEP_OPTION_COUNT] = {
    "Off", "10 seconds", "30 seconds", "1 minute",
    "2 minutes", "5 minutes", "10 minutes"};
const unsigned long sleepSecondsOptions[SLEEP_OPTION_COUNT] = {
    0, 10, 30, 60, 120, 300, 600};

enum class UIState
{
  AreaList,
  SettingsMenu,
  HomeAreaPicker,
  SleepTimerPicker,
  BatteryDetails,
  DevicesMenu,
  LightControl,
  FanControl,
  SensorDetails,
  BinarySensorDetails,
  MusicControl,
  Ota
};

struct UIContext
{
  UIState state = UIState::DevicesMenu;
  int selected = 0;
  int firstVisible = 0;
  int activeDevice = 0;
  int originalValue = 0;
  int lightField = 0;
  String currentArea = ASSIGNED_AREA_NAME;
  unsigned long lastDeviceRevision = 0;
  uint32_t lastBatteryRevision = 0;
  unsigned long lastActiveRefresh = 0;
  unsigned long lastInputAt = 0;
  unsigned long popupUntil = 0;
  unsigned long pendingSendAt = 0;
  unsigned long sleepSeconds = 0;
  unsigned long screenSleptAt = 0;
  bool popupActive = false;
  bool screenSleeping = false;
  bool pendingSend = false;
};

UIContext ui;
bool mediaPlaying = false;

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawPixels[SCREEN_W * DRAW_ROWS];
lv_disp_drv_t displayDriver;
lv_style_t screenStyle;
lv_style_t cardStyle;
lv_style_t selectedStyle;
lv_style_t mutedStyle;
lv_obj_t *headerStatus = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *otaBar = nullptr;
lv_obj_t *otaPercent = nullptr;
lv_obj_t *otaMessage = nullptr;
unsigned long lastLvTick = 0;

float sensorHistory[MAX_DEVICES][SENSOR_HISTORY_SIZE];
int historyCount[MAX_DEVICES] = {0};
int historyNext[MAX_DEVICES] = {0};
unsigned long historyLastAppend[MAX_DEVICES] = {0};
bool historyLoaded[MAX_DEVICES] = {false};
bool historyLoading[MAX_DEVICES] = {false};

TaskHandle_t historyTaskHandle = nullptr;
volatile bool historyTaskRunning = false;
volatile bool historyTaskReady = false;
int historyTaskIndex = -1;
int historyTaskCount = 0;
Device historyTaskDevice;
float historyTaskSamples[SENSOR_HISTORY_SIZE];

lv_color_t hex(uint32_t value) { return lv_color_hex(value); }

lv_color_t deviceColor(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Light: return hex(0xF8C85A);
  case DeviceType::Fan: return hex(0x55D6BE);
  case DeviceType::Sensor: return hex(0x38BDF8);
  case DeviceType::BinarySensor: return hex(0xC084FC);
  case DeviceType::Media: return hex(0xFB7185);
  }
  return hex(0x94A3B8);
}

const char *deviceGlyph(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Light: return LV_SYMBOL_EYE_OPEN;
  case DeviceType::Fan: return LV_SYMBOL_REFRESH;
  case DeviceType::Sensor: return LV_SYMBOL_CHARGE;
  case DeviceType::BinarySensor: return LV_SYMBOL_HOME;
  case DeviceType::Media: return LV_SYMBOL_AUDIO;
  }
  return LV_SYMBOL_SETTINGS;
}

String valueText(const Device &d)
{
  if (!d.available) return "Unavailable";
  if (d.type == DeviceType::BinarySensor) return d.state ? "Detected" : "Clear";

  String value = fabs(d.value - round(d.value)) < 0.05f
                     ? String(static_cast<int>(round(d.value)))
                     : String(d.value, d.unit == "V" ? 2 : 1);
  if (d.unit.length()) value += " " + d.unit;
  else if (d.controllable) value += "%";
  return value;
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
  uint16_t w = area->x2 - area->x1 + 1;
  uint16_t h = area->y2 - area->y1 + 1;
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels(reinterpret_cast<uint16_t *>(pixels), w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(driver);
}

void updateStatus()
{
  if (!headerStatus) return;
  String status = WiFi.status() == WL_CONNECTED ? LV_SYMBOL_WIFI : "offline";
  if (hasBatteryReading()) status += "  " + String(getBatteryPercentage()) + "%";
  lv_label_set_text(headerStatus, status.c_str());
}

void resetScreen(const String &title, const char *footer)
{
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  content = nullptr;
  headerStatus = nullptr;
  otaBar = nullptr;
  otaPercent = nullptr;
  otaMessage = nullptr;
  lv_obj_add_style(screen, &screenStyle, 0);

  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, SCREEN_W - 20, 43);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t *titleLabel = lv_label_create(header);
  lv_label_set_text(titleLabel, title.c_str());
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(titleLabel, 150);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(titleLabel, hex(0xF8FAFC), 0);
  lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 0, 0);

  headerStatus = lv_label_create(header);
  lv_obj_set_style_text_font(headerStatus, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(headerStatus, hex(0x94A3B8), 0);
  lv_obj_align(headerStatus, LV_ALIGN_RIGHT_MID, 0, 0);
  updateStatus();

  lv_obj_t *line = lv_obj_create(screen);
  lv_obj_remove_style_all(line);
  lv_obj_set_size(line, SCREEN_W, 1);
  lv_obj_set_style_bg_color(line, hex(0x1E3A5F), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 46);

  content = lv_obj_create(screen);
  lv_obj_remove_style_all(content);
  lv_obj_set_size(content, SCREEN_W, 244);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 48);

  lv_obj_t *footerLabel = lv_label_create(screen);
  lv_label_set_text(footerLabel, footer);
  lv_obj_set_style_text_font(footerLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(footerLabel, hex(0x64748B), 0);
  lv_obj_align(footerLabel, LV_ALIGN_BOTTOM_MID, 0, -5);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const String &text, const lv_font_t *font,
                    lv_color_t color, lv_align_t align, int x, int y)
{
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text.c_str());
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_align(label, align, x, y);
  return label;
}

void makeRow(int row, bool selected, const String &primary,
             const String &secondary, lv_color_t accent,
             const char *glyph = nullptr)
{
  lv_obj_t *card = lv_obj_create(content);
  lv_obj_add_style(card, selected ? &selectedStyle : &cardStyle, 0);
  lv_obj_set_size(card, SCREEN_W - 22, 37);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, row * 39 + 3);

  int left = 0;
  if (glyph)
  {
    makeLabel(card, glyph, &lv_font_montserrat_14, accent, LV_ALIGN_LEFT_MID, 0, 0);
    left = 24;
  }

  lv_obj_t *primaryLabel = makeLabel(
      card, primary, &lv_font_montserrat_14,
      selected ? hex(0xF8FAFC) : hex(0xCBD5E1), LV_ALIGN_LEFT_MID, left, -8);
  lv_obj_set_width(primaryLabel, glyph ? 130 : 158);
  lv_label_set_long_mode(primaryLabel,
                         selected ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);

  lv_obj_t *secondaryLabel = makeLabel(
      card, secondary, &lv_font_montserrat_14,
      secondary == "Unavailable" ? hex(0xF59E0B) : hex(0x64748B),
      LV_ALIGN_LEFT_MID, left, 9);
  lv_obj_set_width(secondaryLabel, glyph ? 130 : 170);
  lv_label_set_long_mode(secondaryLabel, LV_LABEL_LONG_DOT);
}

void makeScrollbar(int itemCount)
{
  if (itemCount <= VISIBLE_ROWS) return;
  lv_obj_t *bar = lv_bar_create(content);
  lv_obj_set_size(bar, 4, 224);
  lv_obj_align(bar, LV_ALIGN_RIGHT_MID, -3, 0);
  lv_bar_set_range(bar, 0, max(1, itemCount - 1));
  lv_bar_set_value(bar, ui.selected, LV_ANIM_ON);
  lv_obj_set_style_bg_color(bar, hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, hex(0x38BDF8), LV_PART_INDICATOR);
}

void fitListWindow(int count)
{
  if (count <= 0)
  {
    ui.selected = 0;
    ui.firstVisible = 0;
    return;
  }
  if (ui.selected < 0) ui.selected = count - 1;
  if (ui.selected >= count) ui.selected = 0;
  if (ui.selected < ui.firstVisible) ui.firstVisible = ui.selected;
  if (ui.selected >= ui.firstVisible + VISIBLE_ROWS)
    ui.firstVisible = ui.selected - VISIBLE_ROWS + 1;
  ui.firstVisible = constrain(ui.firstVisible, 0, max(0, count - VISIBLE_ROWS));
}

bool allDevices() { return ui.currentArea == AREA_ALL_DEVICES; }
int areaListCount() { return areaCount + 2; }
int homeAreaCount() { return areaCount + 1; }

String areaAt(int index)
{
  if (index < areaCount) return getArea(index);
  return index == areaCount ? AREA_ALL_DEVICES : AREA_SETTINGS;
}

String homeAreaAt(int index)
{
  return index < areaCount ? getArea(index) : AREA_ALL_DEVICES;
}

int areaIndex(const String &name)
{
  for (int i = 0; i < areaCount; ++i)
    if (getArea(i) == name) return i;
  return areaCount;
}

bool visibleDevice(int index)
{
  return index >= 0 && index < deviceCount &&
         (allDevices() || getDevice(index).area == ui.currentArea);
}

int visibleDeviceCount()
{
  int count = 0;
  for (int i = 0; i < deviceCount; ++i) if (visibleDevice(i)) ++count;
  return count;
}

int deviceForVisible(int visible)
{
  int current = 0;
  for (int i = 0; i < deviceCount; ++i)
  {
    if (!visibleDevice(i)) continue;
    if (current++ == visible) return i;
  }
  return -1;
}

int visibleForDevice(int deviceIndex)
{
  int current = 0;
  for (int i = 0; i < deviceCount; ++i)
  {
    if (!visibleDevice(i)) continue;
    if (i == deviceIndex) return current;
    ++current;
  }
  return 0;
}

int devicesInArea(const String &area)
{
  if (area == AREA_ALL_DEVICES) return deviceCount;
  int count = 0;
  for (int i = 0; i < deviceCount; ++i)
    if (getDevice(i).area == area) ++count;
  return count;
}

int currentListCount()
{
  switch (ui.state)
  {
  case UIState::AreaList: return areaListCount();
  case UIState::SettingsMenu: return SETTINGS_COUNT;
  case UIState::HomeAreaPicker: return homeAreaCount();
  case UIState::SleepTimerPicker: return SLEEP_OPTION_COUNT;
  case UIState::DevicesMenu: return visibleDeviceCount();
  default: return 0;
  }
}

void renderAreaList()
{
  resetScreen("Areas", "turn to select    press to open");
  int count = areaListCount();
  fitListWindow(count);
  int last = min(count, ui.firstVisible + VISIBLE_ROWS);
  for (int i = ui.firstVisible; i < last; ++i)
  {
    String area = areaAt(i);
    String secondary;
    const char *glyph = LV_SYMBOL_HOME;
    lv_color_t accent = hex(0x38BDF8);
    if (area == AREA_SETTINGS)
    {
      secondary = "device settings";
      glyph = LV_SYMBOL_SETTINGS;
      accent = hex(0xC084FC);
    }
    else
    {
      int countForArea = devicesInArea(area);
      secondary = String(countForArea) + (countForArea == 1 ? " device" : " devices");
    }
    makeRow(i - ui.firstVisible, i == ui.selected, area, secondary, accent, glyph);
  }
  makeScrollbar(count);
}

void renderDeviceList()
{
  resetScreen(ui.currentArea, LV_SYMBOL_LEFT " areas    press to open");
  int count = visibleDeviceCount();
  fitListWindow(count);
  if (!count)
  {
    makeLabel(content, "No devices in this area", &lv_font_montserrat_14,
              hex(0x64748B), LV_ALIGN_CENTER, 0, 0);
    return;
  }
  int last = min(count, ui.firstVisible + VISIBLE_ROWS);
  for (int i = ui.firstVisible; i < last; ++i)
  {
    int index = deviceForVisible(i);
    const Device &d = getDevice(index);
    makeRow(i - ui.firstVisible, i == ui.selected, d.name, valueText(d),
            deviceColor(d.type), deviceGlyph(d.type));
  }
  makeScrollbar(count);
}

String sleepSettingText()
{
  if (!ui.sleepSeconds) return "Off";
  for (int i = 0; i < SLEEP_OPTION_COUNT; ++i)
    if (sleepSecondsOptions[i] == ui.sleepSeconds) return sleepLabels[i];
  return String(ui.sleepSeconds) + " seconds";
}

void renderSettings()
{
  resetScreen("Settings", LV_SYMBOL_LEFT " areas    press to select");
  fitListWindow(SETTINGS_COUNT);
  for (int i = 0; i < SETTINGS_COUNT; ++i)
  {
    String secondary;
    if (i == 0) secondary = "fetch Home Assistant devices";
    else if (i == 1) secondary = ui.currentArea;
    else if (i == 2) secondary = sleepSettingText();
    else if (i == 3)
      secondary = hasBatteryReading()
                      ? String(getBatteryPercentage()) + "%  " + String(getBatteryVoltage(), 2) + " V"
                      : "measuring...";
    else secondary = "restart controller";
    makeRow(i, i == ui.selected, settingsLabels[i], secondary,
            i == 4 ? hex(0xFB7185) : hex(0x38BDF8), LV_SYMBOL_SETTINGS);
  }
}

void renderHomeAreaPicker()
{
  resetScreen("Home Area", LV_SYMBOL_LEFT " cancel    press to save");
  int count = homeAreaCount();
  fitListWindow(count);
  int last = min(count, ui.firstVisible + VISIBLE_ROWS);
  for (int i = ui.firstVisible; i < last; ++i)
  {
    String area = homeAreaAt(i);
    makeRow(i - ui.firstVisible, i == ui.selected, area,
            area == ui.currentArea ? "current home" : "set as home",
            hex(0x55D6BE), LV_SYMBOL_HOME);
  }
  makeScrollbar(count);
}

int sleepIndex(unsigned long seconds)
{
  for (int i = 0; i < SLEEP_OPTION_COUNT; ++i)
    if (sleepSecondsOptions[i] == seconds) return i;
  return 0;
}

void renderSleepPicker()
{
  resetScreen("Sleep Timer", LV_SYMBOL_LEFT " cancel    press to save");
  fitListWindow(SLEEP_OPTION_COUNT);
  int last = min(SLEEP_OPTION_COUNT, ui.firstVisible + VISIBLE_ROWS);
  for (int i = ui.firstVisible; i < last; ++i)
    makeRow(i - ui.firstVisible, i == ui.selected, sleepLabels[i],
            ui.sleepSeconds == sleepSecondsOptions[i] ? "current timer" : "set timer",
            hex(0x55D6BE), LV_SYMBOL_BELL);
  makeScrollbar(SLEEP_OPTION_COUNT);
}

void renderBattery()
{
  resetScreen("Battery", LV_SYMBOL_LEFT " back");
  if (!hasBatteryReading())
  {
    makeLabel(content, "Measuring...", &lv_font_montserrat_18,
              hex(0x64748B), LV_ALIGN_CENTER, 0, 0);
    return;
  }
  int pct = getBatteryPercentage();
  lv_color_t accent = pct > 50 ? hex(0x55D6BE) : pct > 20 ? hex(0xF8C85A) : hex(0xFB7185);
  makeLabel(content, String(pct) + "%", &lv_font_montserrat_22,
            accent, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *bar = lv_bar_create(content);
  lv_obj_set_size(bar, 180, 24);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 63);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, pct, LV_ANIM_ON);
  lv_obj_set_style_bg_color(bar, hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);

  makeLabel(content, String(getBatteryVoltage(), 2) + " V", &lv_font_montserrat_22,
            hex(0x38BDF8), LV_ALIGN_TOP_MID, 0, 110);
  makeLabel(content, "Battery voltage", &lv_font_montserrat_14,
            hex(0x64748B), LV_ALIGN_TOP_MID, 0, 139);
  makeLabel(content, "Sense  " + String(getBatterySenseVoltage(), 3) + " V",
            &lv_font_montserrat_14, hex(0xCBD5E1), LV_ALIGN_TOP_MID, 0, 170);
  makeLabel(content, "ADC raw  " + String(getBatteryRawAdcVoltage(), 3) + " V",
            &lv_font_montserrat_14, hex(0x64748B), LV_ALIGN_TOP_MID, 0, 196);
}

Device &activeDevice() { return getDevice(ui.activeDevice); }

int lightFieldCount(const Device &d)
{
  return 1 + (d.supportsColor ? 2 : 0) + (d.supportsEffects ? 1 : 0);
}

int effectField(const Device &d)
{
  return d.supportsEffects ? (d.supportsColor ? 3 : 1) : -1;
}

void addValueBar(lv_obj_t *parent, int y, int value, int maximum,
                 lv_color_t accent, bool selected)
{
  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_size(bar, 142, 9);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 10, y);
  lv_bar_set_range(bar, 0, maximum);
  lv_bar_set_value(bar, value, LV_ANIM_ON);
  lv_obj_set_style_bg_color(bar, hex(0x273449), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, accent, LV_PART_INDICATOR);
  if (selected)
  {
    lv_obj_set_style_outline_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_outline_color(bar, hex(0x55D6BE), LV_PART_MAIN);
  }
}

void addGradientBar(lv_obj_t *parent, int y, int value, int maximum,
                    lv_color_t left, lv_color_t right, bool selected)
{
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 142, 9);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 10, y);
  lv_obj_set_style_bg_color(bar, left, 0);
  lv_obj_set_style_bg_grad_color(bar, right, 0);
  lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  if (selected)
  {
    lv_obj_set_style_outline_width(bar, 2, 0);
    lv_obj_set_style_outline_color(bar, hex(0x55D6BE), 0);
  }

  lv_obj_t *marker = lv_obj_create(parent);
  lv_obj_remove_style_all(marker);
  lv_obj_set_size(marker, 3, 15);
  lv_obj_set_style_bg_color(marker, hex(0xF8FAFC), 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_align(marker, LV_ALIGN_TOP_LEFT,
               10 + map(constrain(value, 0, maximum), 0, maximum, 0, 139), y - 3);
}

void addHueBar(lv_obj_t *parent, int y, int value, bool selected)
{
  const lv_color_t colors[] = {
      hex(0xFF0000), hex(0xFFFF00), hex(0x00FF00),
      hex(0x00FFFF), hex(0x0000FF), hex(0xFF00FF), hex(0xFF0000)};
  for (int i = 0; i < 6; ++i)
  {
    lv_obj_t *segment = lv_obj_create(parent);
    lv_obj_remove_style_all(segment);
    lv_obj_set_size(segment, i == 5 ? 27 : 24, 9);
    lv_obj_align(segment, LV_ALIGN_TOP_LEFT, 10 + i * 23, y);
    lv_obj_set_style_bg_color(segment, colors[i], 0);
    lv_obj_set_style_bg_grad_color(segment, colors[i + 1], 0);
    lv_obj_set_style_bg_grad_dir(segment, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
  }
  if (selected)
  {
    lv_obj_t *outline = lv_obj_create(parent);
    lv_obj_remove_style_all(outline);
    lv_obj_set_size(outline, 146, 13);
    lv_obj_align(outline, LV_ALIGN_TOP_LEFT, 8, y - 2);
    lv_obj_set_style_border_width(outline, 2, 0);
    lv_obj_set_style_border_color(outline, hex(0x55D6BE), 0);
    lv_obj_set_style_bg_opa(outline, LV_OPA_TRANSP, 0);
  }
  lv_obj_t *marker = lv_obj_create(parent);
  lv_obj_remove_style_all(marker);
  lv_obj_set_size(marker, 3, 15);
  lv_obj_set_style_bg_color(marker, hex(0xF8FAFC), 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_align(marker, LV_ALIGN_TOP_LEFT,
               10 + map(constrain(value, 0, 360), 0, 360, 0, 139), y - 3);
}

void renderLight()
{
  Device &d = activeDevice();
  resetScreen(d.name, LV_SYMBOL_LEFT " save/back    press next");
  int count = lightFieldCount(d);
  ui.lightField = constrain(ui.lightField, 0, count - 1);
  int field = 0;

  auto addField = [&](const String &name, const String &value, int amount,
                      int maximum, lv_color_t accent, int gradientType = 0)
  {
    int y = 8 + field * 53;
    bool selected = field == ui.lightField;
    makeLabel(content, name, &lv_font_montserrat_14,
              selected ? hex(0x55D6BE) : hex(0xCBD5E1),
              LV_ALIGN_TOP_LEFT, 20, y);
    makeLabel(content, value, &lv_font_montserrat_14, accent,
              LV_ALIGN_TOP_RIGHT, -20, y);
    if (gradientType == 1)
      addGradientBar(content, y + 25, amount, maximum, hex(0xF8FAFC),
                     lv_color_hsv_to_rgb(static_cast<uint16_t>(d.hue), 100, 100),
                     selected);
    else if (gradientType == 2)
      addHueBar(content, y + 25, amount, selected);
    else
      addValueBar(content, y + 25, amount, maximum, accent, selected);
    ++field;
  };

  addField("Brightness", String(static_cast<int>(round(d.value))) + "%",
           static_cast<int>(round(d.value)), 100, hex(0xF8C85A));
  if (d.supportsColor)
  {
    addField("Saturation", String(static_cast<int>(round(d.saturation))) + "%",
             static_cast<int>(round(d.saturation)), 100, hex(0x38BDF8), 1);
    addField("Hue", String(static_cast<int>(round(d.hue))) + "°",
             static_cast<int>(round(d.hue)), 360, hex(0xC084FC), 2);
  }
  if (d.supportsEffects)
  {
    int y = 8 + field * 53;
    bool selected = field == ui.lightField;
    makeLabel(content, "Effect", &lv_font_montserrat_14,
              selected ? hex(0x55D6BE) : hex(0xCBD5E1),
              LV_ALIGN_TOP_LEFT, 20, y);
    String effect = d.effectCount ? d.effects[d.effectIndex] : "None";
    lv_obj_t *effectLabel = makeLabel(content, effect, &lv_font_montserrat_14,
                                      hex(0xC084FC), LV_ALIGN_TOP_LEFT, 20, y + 24);
    lv_obj_set_width(effectLabel, 170);
    lv_label_set_long_mode(effectLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    makeLabel(content, String(d.effectIndex + 1) + "/" + String(d.effectCount),
              &lv_font_montserrat_14, hex(0x64748B), LV_ALIGN_TOP_RIGHT, -18, y + 24);
  }
  if (!d.available)
    makeLabel(content, "Entity unavailable", &lv_font_montserrat_14,
              hex(0xF59E0B), LV_ALIGN_BOTTOM_MID, 0, -8);
}

void renderFan()
{
  Device &d = activeDevice();
  resetScreen(d.name, LV_SYMBOL_LEFT " cancel    press save");
  lv_obj_t *arc = lv_arc_create(content);
  lv_obj_set_size(arc, 150, 150);
  lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 20);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, max(1, static_cast<int>(d.maxValue)));
  lv_arc_set_value(arc, static_cast<int>(d.value));
  lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(arc, hex(0x273449), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, hex(0x55D6BE), LV_PART_INDICATOR);
  makeLabel(content, String(static_cast<int>(round(d.value))), &lv_font_montserrat_22,
            hex(0xF8FAFC), LV_ALIGN_TOP_MID, 0, 80);
  makeLabel(content, "Speed", &lv_font_montserrat_14,
            hex(0x64748B), LV_ALIGN_TOP_MID, 0, 112);
  if (!d.available)
    makeLabel(content, "Entity unavailable", &lv_font_montserrat_14,
              hex(0xF59E0B), LV_ALIGN_BOTTOM_MID, 0, -14);
}

void recordHistory(int index, float value)
{
  if (index < 0 || index >= MAX_DEVICES) return;
  sensorHistory[index][historyNext[index]] = value;
  historyNext[index] = (historyNext[index] + 1) % SENSOR_HISTORY_SIZE;
  if (historyCount[index] < SENSOR_HISTORY_SIZE) ++historyCount[index];
}

void clearHistory(int index)
{
  historyCount[index] = historyNext[index] = 0;
  historyLastAppend[index] = 0;
  historyLoaded[index] = historyLoading[index] = false;
}

unsigned long historyBucketMs()
{
  return max(1000UL, (HA_HISTORY_MINUTES * 60UL * 1000UL) / SENSOR_HISTORY_SIZE);
}

void updateHistory(int index, float value)
{
  if (!historyCount[index] || millis() - historyLastAppend[index] >= historyBucketMs())
  {
    recordHistory(index, value);
    historyLastAppend[index] = millis();
    return;
  }
  int latest = (historyNext[index] - 1 + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
  sensorHistory[index][latest] = value;
}

void historyTask(void *)
{
  historyTaskCount = fetchHomeAssistantHistory(
      historyTaskDevice, historyTaskSamples, SENSOR_HISTORY_SIZE);
  historyTaskReady = true;
  historyTaskRunning = false;
  historyTaskHandle = nullptr;
  vTaskDelete(nullptr);
}

void requestHistory(int index, const Device &device)
{
  if (index < 0 || index >= MAX_DEVICES || historyLoaded[index] ||
      historyLoading[index] || historyTaskRunning || historyTaskReady) return;
  historyLoading[index] = true;
  historyTaskIndex = index;
  historyTaskDevice = device;
  historyTaskCount = 0;
  historyTaskReady = false;
  historyTaskRunning = true;
  if (xTaskCreatePinnedToCore(historyTask, "ha_history", 8192, nullptr, 1,
                              &historyTaskHandle, 0) != pdPASS)
  {
    historyTaskRunning = false;
    historyLoading[index] = false;
    recordHistory(index, device.value);
  }
}

void applyHistory()
{
  if (!historyTaskReady) return;
  int index = historyTaskIndex;
  int count = historyTaskCount;
  historyTaskReady = false;
  if (index < 0 || index >= MAX_DEVICES) return;
  clearHistory(index);
  if (!count) recordHistory(index, devices[index].value);
  else for (int i = 0; i < count && i < SENSOR_HISTORY_SIZE; ++i)
    recordHistory(index, historyTaskSamples[i]);
  historyLastAppend[index] = millis();
  historyLoaded[index] = count > 0;
  historyLoading[index] = false;
  if (ui.state == UIState::SensorDetails && ui.activeDevice == index)
    ui.lastActiveRefresh = 0;
}

void renderSensor()
{
  Device &d = activeDevice();
  resetScreen(d.name, LV_SYMBOL_LEFT " back");
  makeLabel(content, valueText(d), &lv_font_montserrat_22,
            hex(0x38BDF8), LV_ALIGN_TOP_MID, 0, 8);

  if (historyCount[ui.activeDevice] >= 2)
  {
    int latest = (historyNext[ui.activeDevice] - 1 + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
    int previous = (historyNext[ui.activeDevice] - 2 + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
    float delta = sensorHistory[ui.activeDevice][latest] -
                  sensorHistory[ui.activeDevice][previous];
    String trend = fabs(delta) < 0.01f ? LV_SYMBOL_MINUS :
                   delta > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN;
    trend += " " + String(delta > 0 ? "+" : "") + String(delta, 2);
    if (d.unit.length()) trend += " " + d.unit;
    makeLabel(content, trend, &lv_font_montserrat_14,
              delta > 0.01f ? hex(0x55D6BE) :
              delta < -0.01f ? hex(0xFB7185) : hex(0x64748B),
              LV_ALIGN_TOP_MID, 0, 43);
  }

  float minimum = d.value;
  float maximum = d.value;
  for (int i = 0; i < historyCount[ui.activeDevice]; ++i)
  {
    int sample = (historyNext[ui.activeDevice] - historyCount[ui.activeDevice] +
                  i + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
    minimum = min(minimum, sensorHistory[ui.activeDevice][sample]);
    maximum = max(maximum, sensorHistory[ui.activeDevice][sample]);
  }
  if (historyCount[ui.activeDevice])
  {
    String range = "Range " + String(minimum, 1) + " - " + String(maximum, 1);
    if (d.unit.length()) range += " " + d.unit;
    makeLabel(content, range, &lv_font_montserrat_14, hex(0x64748B),
              LV_ALIGN_TOP_MID, 0, 63);
  }

  lv_obj_t *chart = lv_chart_create(content);
  lv_obj_set_size(chart, 205, 125);
  lv_obj_align(chart, LV_ALIGN_TOP_MID, 0, 79);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, max(2, historyCount[ui.activeDevice]));
  lv_obj_set_style_bg_color(chart, hex(0x111827), LV_PART_MAIN);
  lv_obj_set_style_border_color(chart, hex(0x273449), LV_PART_MAIN);
  lv_obj_set_style_line_color(chart, hex(0x273449), LV_PART_MAIN);
  float largest = max(fabs(minimum), fabs(maximum));
  int scale = largest > 300.0f ? 1 : largest > 30.0f ? 10 : 100;
  int chartMin = static_cast<int>(floor(minimum * scale));
  int chartMax = static_cast<int>(ceil(maximum * scale));
  if (chartMax <= chartMin) chartMax = chartMin + scale;
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, chartMin, chartMax);
  lv_chart_series_t *series = lv_chart_add_series(chart, hex(0x38BDF8), LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < historyCount[ui.activeDevice]; ++i)
  {
    int sample = (historyNext[ui.activeDevice] - historyCount[ui.activeDevice] +
                  i + SENSOR_HISTORY_SIZE) % SENSOR_HISTORY_SIZE;
    lv_chart_set_next_value(chart, series,
                            static_cast<lv_coord_t>(sensorHistory[ui.activeDevice][sample] * scale));
  }
  if (!historyCount[ui.activeDevice])
    makeLabel(chart, "Loading history...", &lv_font_montserrat_14,
              hex(0x64748B), LV_ALIGN_CENTER, 0, 0);
  requestHistory(ui.activeDevice, d);
}

void renderBinarySensor()
{
  Device &d = activeDevice();
  resetScreen(d.name, LV_SYMBOL_LEFT " back");
  makeLabel(content, d.state ? LV_SYMBOL_WARNING : LV_SYMBOL_OK,
            &lv_font_montserrat_22, d.state ? hex(0xC084FC) : hex(0x55D6BE),
            LV_ALIGN_CENTER, 0, -34);
  makeLabel(content, d.available ? (d.state ? "Detected" : "Clear") : "Unavailable",
            &lv_font_montserrat_22,
            !d.available ? hex(0xF59E0B) :
            d.state ? hex(0xC084FC) : hex(0x55D6BE),
            LV_ALIGN_CENTER, 0, 8);
}

void renderMusic()
{
  Device &d = activeDevice();
  resetScreen(d.name, LV_SYMBOL_LEFT " back    turn volume    press play");
  makeLabel(content, "Evening Lights", &lv_font_montserrat_18,
            hex(0xF8FAFC), LV_ALIGN_TOP_MID, 0, 14);
  makeLabel(content, "Local Mock", &lv_font_montserrat_14,
            hex(0x38BDF8), LV_ALIGN_TOP_MID, 0, 42);
  makeLabel(content, mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY,
            &lv_font_montserrat_22,
            mediaPlaying ? hex(0x55D6BE) : hex(0xFB7185),
            LV_ALIGN_TOP_MID, 0, 82);
  addValueBar(content, 142, static_cast<int>(d.value),
              max(1, static_cast<int>(d.maxValue)), hex(0x38BDF8), false);
  makeLabel(content, "Volume " + String(static_cast<int>(round(d.value))) + "%",
            &lv_font_montserrat_14, hex(0xCBD5E1), LV_ALIGN_TOP_MID, 0, 174);
}

void renderCurrent()
{
  switch (ui.state)
  {
  case UIState::AreaList: renderAreaList(); break;
  case UIState::SettingsMenu: renderSettings(); break;
  case UIState::HomeAreaPicker: renderHomeAreaPicker(); break;
  case UIState::SleepTimerPicker: renderSleepPicker(); break;
  case UIState::BatteryDetails: renderBattery(); break;
  case UIState::DevicesMenu: renderDeviceList(); break;
  case UIState::LightControl: renderLight(); break;
  case UIState::FanControl: renderFan(); break;
  case UIState::SensorDetails: renderSensor(); break;
  case UIState::BinarySensorDetails: renderBinarySensor(); break;
  case UIState::MusicControl: renderMusic(); break;
  case UIState::Ota: break;
  }
}

void changeState(UIState state, int selection = 0)
{
  ui.state = state;
  ui.selected = selection;
  ui.firstVisible = 0;
  ui.lastActiveRefresh = 0;
  renderCurrent();
}

void showPopup(const String &title, const String &subtitle, bool success)
{
  lv_obj_t *popup = lv_obj_create(lv_scr_act());
  lv_obj_set_size(popup, SCREEN_W - 34, 102);
  lv_obj_align(popup, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(popup, 12, 0);
  lv_obj_set_style_bg_color(popup, hex(0x111827), 0);
  lv_obj_set_style_bg_opa(popup, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_border_color(popup, success ? hex(0x55D6BE) : hex(0xFB7185), 0);
  makeLabel(popup, title, &lv_font_montserrat_18, hex(0xF8FAFC),
            LV_ALIGN_TOP_MID, 0, 10);
  makeLabel(popup, subtitle, &lv_font_montserrat_14,
            success ? hex(0x55D6BE) : hex(0xFB7185),
            LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_move_foreground(popup);
  ui.popupActive = true;
  ui.popupUntil = millis() + POPUP_MS;
}

void saveHomeArea()
{
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putString(PREF_HOME_AREA, ui.currentArea);
  preferences.end();
}

void saveSleep()
{
  preferences.begin(PREF_NAMESPACE, false);
  preferences.putULong(PREF_SLEEP_SECONDS, ui.sleepSeconds);
  preferences.end();
}

void scheduleSend()
{
  ui.pendingSend = true;
  ui.pendingSendAt = millis();
}

void flushSend()
{
  if (!ui.pendingSend) return;
  ui.pendingSend = false;
  confirmDeviceValue(activeDevice());
}

bool hasInput(const InputState &input)
{
  return input.encoderMove || input.enter || input.back || input.backLong ||
         input.shortcut1 || input.shortcut2 || input.shortcut3 ||
         input.shortcut1Long || input.shortcut2Long || input.shortcut3Long;
}

void handleShortcut(const InputState &input)
{
  int number = input.shortcut1 || input.shortcut1Long ? 1 :
               input.shortcut2 || input.shortcut2Long ? 2 :
               input.shortcut3 || input.shortcut3Long ? 3 : 0;
  if (!number) return;
  bool held = input.shortcut1Long || input.shortcut2Long || input.shortcut3Long;
  bool sent = sendShortcutEventToHomeAssistant(number, held);
  showPopup("Shortcut " + String(number), sent ? "SENT" : "FAILED", sent);
}

void moveSelection(int direction)
{
  int count = currentListCount();
  if (!count) return;
  ui.selected += direction > 0 ? 1 : -1;
  fitListWindow(count);
  renderCurrent();
}

void openDevice()
{
  int index = deviceForVisible(ui.selected);
  if (index < 0) return;
  ui.activeDevice = index;
  ui.originalValue = static_cast<int>(activeDevice().value);
  ui.lightField = 0;
  switch (activeDevice().type)
  {
  case DeviceType::Light: changeState(UIState::LightControl); break;
  case DeviceType::Fan: changeState(UIState::FanControl); break;
  case DeviceType::Sensor: changeState(UIState::SensorDetails); break;
  case DeviceType::BinarySensor: changeState(UIState::BinarySensorDetails); break;
  case DeviceType::Media: changeState(UIState::MusicControl); break;
  }
}

void returnToDevices()
{
  flushSend();
  int selected = visibleForDevice(ui.activeDevice);
  changeState(UIState::DevicesMenu, selected);
}

void handleSettings(const InputState &input)
{
  if (input.back) { changeState(UIState::AreaList, areaIndex(ui.currentArea)); return; }
  if (input.encoderMove) moveSelection(input.encoderMove);
  if (!input.enter) return;

  if (ui.selected == 0)
  {
    if (historyTaskRunning || historyTaskReady)
      showPopup("Refresh", "graph busy", false);
    else
    {
      for (int i = 0; i < MAX_DEVICES; ++i) clearHistory(i);
      bool refreshed = refreshHomeAssistantDevices();
      ui.lastDeviceRevision = deviceRevision;
      renderSettings();
      showPopup("Refresh", refreshed ? "UPDATED" : "FAILED", refreshed);
    }
  }
  else if (ui.selected == 1)
    changeState(UIState::HomeAreaPicker, areaIndex(ui.currentArea));
  else if (ui.selected == 2)
    changeState(UIState::SleepTimerPicker, sleepIndex(ui.sleepSeconds));
  else if (ui.selected == 3)
    changeState(UIState::BatteryDetails);
  else
  {
    showPopup("Reboot", "RESTARTING", true);
    lv_timer_handler();
    delay(250);
    ESP.restart();
  }
}

void adjustLight(int move)
{
  Device &d = activeDevice();
  int effects = effectField(d);
  if (ui.lightField == 0)
    d.value = constrain(d.value + move * 5, 0.0f, 100.0f);
  else if (ui.lightField == 1)
    d.saturation = constrain(d.saturation + move * 5, 0.0f, 100.0f);
  else if (ui.lightField == 2)
  {
    d.hue += move * 8;
    while (d.hue < 0) d.hue += 360;
    while (d.hue >= 360) d.hue -= 360;
  }
  else if (ui.lightField == effects && d.effectCount)
  {
    d.effectIndex = (d.effectIndex + move) % d.effectCount;
    if (d.effectIndex < 0) d.effectIndex += d.effectCount;
  }
}

void setScreenAwake(bool awake)
{
  if (TFT_BL >= 0) digitalWrite(TFT_BL, awake ? HIGH : LOW);
  tft.enableDisplay(awake);
}

void renderOta(const char *message, uint8_t percentage, lv_color_t accent)
{
  bool rebuild = ui.state != UIState::Ota || !otaBar;
  ui.state = UIState::Ota;
  if (rebuild)
  {
    resetScreen("OTA Update", "Keep the device powered");
    makeLabel(content, LV_SYMBOL_DOWNLOAD, &lv_font_montserrat_22,
              accent, LV_ALIGN_TOP_MID, 0, 24);
    otaMessage = makeLabel(content, message, &lv_font_montserrat_18,
                           hex(0xF8FAFC), LV_ALIGN_TOP_MID, 0, 67);
    otaBar = lv_bar_create(content);
    lv_obj_set_size(otaBar, 190, 20);
    lv_obj_align(otaBar, LV_ALIGN_TOP_MID, 0, 115);
    lv_bar_set_range(otaBar, 0, 100);
    lv_obj_set_style_bg_color(otaBar, hex(0x273449), LV_PART_MAIN);
    otaPercent = makeLabel(content, "0%", &lv_font_montserrat_18,
                           accent, LV_ALIGN_TOP_MID, 0, 153);
  }
  lv_label_set_text(otaMessage, message);
  lv_bar_set_value(otaBar, percentage, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(otaBar, accent, LV_PART_INDICATOR);
  String pct = String(percentage) + "%";
  lv_label_set_text(otaPercent, pct.c_str());
  lv_obj_set_style_text_color(otaPercent, accent, 0);
  lv_timer_handler();
}
} // namespace

void initUI()
{
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(SCREEN_W, SCREEN_H);
  tft.setRotation(0);
  if (TFT_BL >= 0)
  {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, SCREEN_W * DRAW_ROWS);
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = SCREEN_W;
  displayDriver.ver_res = SCREEN_H;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  lv_style_init(&screenStyle);
  lv_style_set_bg_color(&screenStyle, hex(0x070B14));
  lv_style_set_bg_opa(&screenStyle, LV_OPA_COVER);

  lv_style_init(&cardStyle);
  lv_style_set_radius(&cardStyle, 8);
  lv_style_set_bg_color(&cardStyle, hex(0x111827));
  lv_style_set_bg_opa(&cardStyle, LV_OPA_COVER);
  lv_style_set_border_width(&cardStyle, 1);
  lv_style_set_border_color(&cardStyle, hex(0x1E293B));
  lv_style_set_pad_left(&cardStyle, 9);
  lv_style_set_pad_right(&cardStyle, 9);

  lv_style_init(&selectedStyle);
  lv_style_set_radius(&selectedStyle, 8);
  lv_style_set_bg_color(&selectedStyle, hex(0x172033));
  lv_style_set_bg_opa(&selectedStyle, LV_OPA_COVER);
  lv_style_set_border_width(&selectedStyle, 2);
  lv_style_set_border_color(&selectedStyle, hex(0x38BDF8));
  lv_style_set_pad_left(&selectedStyle, 8);
  lv_style_set_pad_right(&selectedStyle, 8);

  lv_style_init(&mutedStyle);
  lv_style_set_text_font(&mutedStyle, &lv_font_montserrat_14);
  lv_style_set_text_color(&mutedStyle, hex(0x64748B));

  preferences.begin(PREF_NAMESPACE, true);
  ui.currentArea = preferences.getString(PREF_HOME_AREA, ASSIGNED_AREA_NAME);
  ui.sleepSeconds = preferences.getULong(PREF_SLEEP_SECONDS, 0);
  preferences.end();
  if (!ui.currentArea.length()) ui.currentArea = ASSIGNED_AREA_NAME;
  ui.sleepSeconds = sleepSecondsOptions[sleepIndex(ui.sleepSeconds)];

  ui.lastInputAt = millis();
  ui.lastDeviceRevision = deviceRevision;
  ui.lastBatteryRevision = getBatteryReadingRevision();
  lastLvTick = millis();
  renderCurrent();
}

void handleUIInput(const InputState &input)
{
  if (!hasInput(input)) return;
  ui.lastInputAt = millis();
  if (ui.screenSleeping)
  {
    ui.screenSleeping = false;
    ui.screenSleptAt = 0;
    setScreenAwake(true);
    lv_obj_invalidate(lv_scr_act());
    return;
  }
  if (ui.state == UIState::Ota) return;

  if (input.shortcut1 || input.shortcut2 || input.shortcut3 ||
      input.shortcut1Long || input.shortcut2Long || input.shortcut3Long)
  {
    handleShortcut(input);
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
    if (input.encoderMove) moveSelection(input.encoderMove);
    if (input.back) changeState(UIState::DevicesMenu);
    if (input.enter)
    {
      String area = areaAt(ui.selected);
      if (area == AREA_SETTINGS) changeState(UIState::SettingsMenu);
      else
      {
        ui.currentArea = area;
        changeState(UIState::DevicesMenu);
      }
    }
    break;
  case UIState::DevicesMenu:
    if (input.encoderMove) moveSelection(input.encoderMove);
    if (input.back) changeState(UIState::AreaList, areaIndex(ui.currentArea));
    if (input.enter) openDevice();
    break;
  case UIState::SettingsMenu:
    handleSettings(input);
    break;
  case UIState::HomeAreaPicker:
    if (input.encoderMove) moveSelection(input.encoderMove);
    if (input.back) changeState(UIState::SettingsMenu, 1);
    if (input.enter)
    {
      ui.currentArea = homeAreaAt(ui.selected);
      saveHomeArea();
      changeState(UIState::SettingsMenu, 1);
    }
    break;
  case UIState::SleepTimerPicker:
    if (input.encoderMove) moveSelection(input.encoderMove);
    if (input.back) changeState(UIState::SettingsMenu, 2);
    if (input.enter)
    {
      ui.sleepSeconds = sleepSecondsOptions[ui.selected];
      saveSleep();
      ui.lastInputAt = millis();
      changeState(UIState::SettingsMenu, 2);
    }
    break;
  case UIState::BatteryDetails:
    if (input.back || input.enter) changeState(UIState::SettingsMenu, 3);
    break;
  case UIState::LightControl:
  {
    Device &d = activeDevice();
    if (!d.available)
    {
      if (input.back || input.enter) returnToDevices();
      break;
    }
    if (input.encoderMove)
    {
      adjustLight(input.encoderMove);
      scheduleSend();
      renderLight();
    }
    if (input.back)
    {
      if (d.supportsColor || d.supportsEffects)
        returnToDevices();
      else
      {
        ui.pendingSend = false;
        d.value = ui.originalValue;
        changeState(UIState::DevicesMenu, visibleForDevice(ui.activeDevice));
      }
    }
    if (input.enter)
    {
      if (d.supportsColor || d.supportsEffects)
      {
        ui.lightField = (ui.lightField + 1) % lightFieldCount(d);
        renderLight();
      }
      else
      {
        flushSend();
        returnToDevices();
      }
    }
    break;
  }
  case UIState::FanControl:
  {
    Device &d = activeDevice();
    if (input.encoderMove && d.controllable && d.available)
    {
      d.value = constrain(d.value + input.encoderMove, 0.0f, d.maxValue);
      renderFan();
    }
    if (input.back)
    {
      d.value = ui.originalValue;
      changeState(UIState::DevicesMenu, visibleForDevice(ui.activeDevice));
    }
    if (input.enter && d.available)
    {
      confirmDeviceValue(d);
      changeState(UIState::DevicesMenu, visibleForDevice(ui.activeDevice));
    }
    break;
  }
  case UIState::SensorDetails:
  case UIState::BinarySensorDetails:
    if (input.back || input.enter) returnToDevices();
    break;
  case UIState::MusicControl:
  {
    Device &d = activeDevice();
    if (input.encoderMove && d.available)
    {
      d.value = constrain(d.value + input.encoderMove * 5, 0.0f, d.maxValue);
      setMediaVolume(d);
      renderMusic();
    }
    if (input.enter && d.available)
    {
      mediaPlaying = !mediaPlaying;
      setMediaPlaying(d, mediaPlaying);
      renderMusic();
    }
    if (input.back) returnToDevices();
    break;
  }
  case UIState::Ota:
    break;
  }
}

void renderUI()
{
  unsigned long now = millis();
  lv_tick_inc(now - lastLvTick);
  lastLvTick = now;

  if (!ui.screenSleeping && ui.sleepSeconds &&
      now - ui.lastInputAt >= ui.sleepSeconds * 1000UL)
  {
    flushSend();
    ui.screenSleeping = true;
    ui.screenSleptAt = now;
    setScreenAwake(false);
  }
  if (ui.screenSleeping)
  {
    if (ui.sleepSeconds &&
        now - ui.screenSleptAt >= DEEP_SLEEP_DELAY_SECONDS * 1000UL)
      enterDeepSleep();
    return;
  }

  if (ui.pendingSend && now - ui.pendingSendAt >= CONTROL_SEND_DELAY_MS)
    flushSend();
  applyHistory();

  if ((ui.state == UIState::SensorDetails ||
       ui.state == UIState::BinarySensorDetails) &&
      !historyTaskRunning && now - ui.lastActiveRefresh >= ACTIVE_SENSOR_REFRESH_MS)
  {
    ui.lastActiveRefresh = now;
    if (refreshHomeAssistantEntity(activeDevice()))
    {
      if (ui.state == UIState::SensorDetails)
        updateHistory(ui.activeDevice, activeDevice().value);
      renderCurrent();
    }
  }

  if (ui.popupActive && now >= ui.popupUntil)
  {
    ui.popupActive = false;
    renderCurrent();
  }
  if (ui.state != UIState::Ota && ui.lastDeviceRevision != deviceRevision)
  {
    ui.lastDeviceRevision = deviceRevision;
    fitListWindow(currentListCount());
    renderCurrent();
  }
  if (ui.lastBatteryRevision != getBatteryReadingRevision())
  {
    ui.lastBatteryRevision = getBatteryReadingRevision();
    if (ui.state == UIState::BatteryDetails) renderBattery();
    else updateStatus();
  }
  lv_timer_handler();
}

void showOtaUpdateStart()
{
  ui.screenSleeping = false;
  setScreenAwake(true);
  renderOta("Receiving firmware", 0, hex(0x38BDF8));
}

void showOtaUpdateProgress(uint8_t percentage)
{
  renderOta("Receiving firmware", percentage > 100 ? 100 : percentage, hex(0x38BDF8));
}

void showOtaUpdateComplete()
{
  renderOta("Update complete", 100, hex(0x55D6BE));
}

void showOtaUpdateError(const char *message)
{
  renderOta(message, 0, hex(0xFB7185));
}
