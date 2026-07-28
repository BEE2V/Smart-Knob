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

constexpr uint16_t DRAW_BUFFER_ROWS = 24;
constexpr unsigned long TOAST_DURATION_MS = 1200;
constexpr const char *PREF_NAMESPACE = "smartknob";
constexpr const char *PREF_SLEEP_SECONDS = "sleep_s";

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawPixels[SCREEN_W * DRAW_BUFFER_ROWS];
lv_disp_drv_t displayDriver;

lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *content = nullptr;
lv_obj_t *footerLabel = nullptr;
lv_obj_t *toast = nullptr;
lv_obj_t *toastLabel = nullptr;
lv_obj_t *otaBar = nullptr;
lv_obj_t *otaPercentLabel = nullptr;
lv_obj_t *otaMessageLabel = nullptr;

lv_style_t screenStyle;
lv_style_t cardStyle;
lv_style_t selectedCardStyle;
lv_style_t mutedTextStyle;

enum class View
{
  Dashboard,
  Detail,
  Ota
};

View currentView = View::Dashboard;
int selectedDevice = 0;
int activeDevice = -1;
unsigned long seenDeviceRevision = 0;
uint32_t seenBatteryRevision = 0;
unsigned long lastLvTick = 0;
unsigned long lastInputAt = 0;
unsigned long screenSleptAt = 0;
unsigned long toastUntil = 0;
unsigned long sleepSeconds = 0;
bool screenSleeping = false;

lv_color_t color(uint32_t hex)
{
  return lv_color_hex(hex);
}

const char *deviceTypeName(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Light:
    return "LIGHT";
  case DeviceType::Fan:
    return "FAN";
  case DeviceType::Sensor:
    return "SENSOR";
  case DeviceType::BinarySensor:
    return "CONTACT";
  case DeviceType::Media:
    return "MEDIA";
  }

  return "DEVICE";
}

const char *deviceGlyph(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Light:
    return LV_SYMBOL_EYE_OPEN;
  case DeviceType::Fan:
    return LV_SYMBOL_REFRESH;
  case DeviceType::Sensor:
    return LV_SYMBOL_CHARGE;
  case DeviceType::BinarySensor:
    return LV_SYMBOL_HOME;
  case DeviceType::Media:
    return LV_SYMBOL_AUDIO;
  }

  return LV_SYMBOL_SETTINGS;
}

lv_color_t deviceColor(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Light:
    return color(0xF8C85A);
  case DeviceType::Fan:
    return color(0x55D6BE);
  case DeviceType::Sensor:
    return color(0x63B3ED);
  case DeviceType::BinarySensor:
    return color(0xC084FC);
  case DeviceType::Media:
    return color(0xFB7185);
  }

  return color(0x94A3B8);
}

String deviceValue(const Device &device)
{
  if (!device.available)
  {
    return "Unavailable";
  }

  if (device.type == DeviceType::BinarySensor)
  {
    return device.state ? "Open" : "Closed";
  }

  if (device.type == DeviceType::Light && !device.state && device.value <= 0)
  {
    return "Off";
  }

  String value;
  if (fabs(device.value - round(device.value)) < 0.05f)
  {
    value = String(static_cast<int>(round(device.value)));
  }
  else
  {
    value = String(device.value, 1);
  }

  if (device.unit.length())
  {
    value += " ";
    value += device.unit;
  }
  else if (device.controllable)
  {
    value += "%";
  }

  return value;
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
  const uint16_t width = area->x2 - area->x1 + 1;
  const uint16_t height = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, width, height);
  tft.writePixels(reinterpret_cast<uint16_t *>(pixels), width * height, true);
  tft.endWrite();

  lv_disp_flush_ready(driver);
}

void setScreenAwake(bool awake)
{
  if (TFT_BL >= 0)
  {
    digitalWrite(TFT_BL, awake ? HIGH : LOW);
  }

  tft.enableDisplay(awake);
}

void wakeScreen()
{
  if (!screenSleeping)
  {
    return;
  }

  screenSleeping = false;
  screenSleptAt = 0;
  setScreenAwake(true);
  lv_obj_invalidate(lv_scr_act());
}

void sleepScreen()
{
  if (screenSleeping)
  {
    return;
  }

  screenSleeping = true;
  screenSleptAt = millis();
  setScreenAwake(false);
}

void clearContent()
{
  lv_obj_clean(content);
}

void updateStatus()
{
  if (!statusLabel)
  {
    return;
  }

  String status = WiFi.status() == WL_CONNECTED ? LV_SYMBOL_WIFI : "offline";
  if (hasBatteryReading())
  {
    status += "  ";
    status += getBatteryPercentage();
    status += "%";
  }

  lv_label_set_text(statusLabel, status.c_str());
}

void createShell()
{
  lv_obj_t *screen = lv_scr_act();
  lv_obj_clean(screen);
  toast = nullptr;
  toastLabel = nullptr;
  toastUntil = 0;
  otaBar = nullptr;
  otaPercentLabel = nullptr;
  otaMessageLabel = nullptr;
  lv_obj_add_style(screen, &screenStyle, 0);

  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, SCREEN_W - 24, 42);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

  titleLabel = lv_label_create(header);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(titleLabel, color(0xF8FAFC), 0);
  lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 0, 0);

  statusLabel = lv_label_create(header);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(statusLabel, color(0x94A3B8), 0);
  lv_obj_align(statusLabel, LV_ALIGN_RIGHT_MID, 0, 0);

  content = lv_obj_create(screen);
  lv_obj_remove_style_all(content);
  lv_obj_set_size(content, SCREEN_W, 246);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 50);

  footerLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(footerLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(footerLabel, color(0x64748B), 0);
  lv_obj_align(footerLabel, LV_ALIGN_BOTTOM_MID, 0, -7);

  updateStatus();
}

void showToast(const String &message, lv_color_t accent)
{
  if (!toast)
  {
    toast = lv_obj_create(lv_scr_act());
    lv_obj_set_size(toast, SCREEN_W - 30, 48);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_set_style_radius(toast, 12, 0);
    lv_obj_set_style_bg_color(toast, color(0x111827), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(toast, 1, 0);

    toastLabel = lv_label_create(toast);
    lv_obj_set_style_text_font(toastLabel, &lv_font_montserrat_14, 0);
    lv_obj_center(toastLabel);
  }

  lv_obj_set_style_border_color(toast, accent, 0);
  lv_obj_set_style_text_color(toastLabel, accent, 0);
  lv_label_set_text(toastLabel, message.c_str());
  lv_obj_clear_flag(toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(toast);
  toastUntil = millis() + TOAST_DURATION_MS;
}

void addEmptyState()
{
  lv_obj_t *icon = lv_label_create(content);
  lv_label_set_text(icon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(icon, color(0x475569), 0);
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, -22);

  lv_obj_t *label = lv_label_create(content);
  lv_label_set_text(label, "Waiting for devices");
  lv_obj_add_style(label, &mutedTextStyle, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 16);
}

void renderDashboard()
{
  currentView = View::Dashboard;
  activeDevice = -1;
  createShell();
  lv_label_set_text(titleLabel, "Smart Knob");
  lv_label_set_text(footerLabel, LV_SYMBOL_UP " turn    press " LV_SYMBOL_RIGHT);

  clearContent();
  if (deviceCount <= 0)
  {
    addEmptyState();
    return;
  }

  selectedDevice = constrain(selectedDevice, 0, deviceCount - 1);
  const int visibleRows = 4;
  const int first = max(0, min(selectedDevice - 1, max(0, deviceCount - visibleRows)));

  for (int row = 0; row < visibleRows && first + row < deviceCount; row++)
  {
    const int index = first + row;
    const Device &device = getDevice(index);

    lv_obj_t *card = lv_obj_create(content);
    lv_obj_add_style(card, index == selectedDevice ? &selectedCardStyle : &cardStyle, 0);
    lv_obj_set_size(card, SCREEN_W - 24, 53);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, row * 58 + 4);

    lv_obj_t *glyph = lv_label_create(card);
    lv_label_set_text(glyph, deviceGlyph(device.type));
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(glyph, deviceColor(device.type), 0);
    lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, device.name.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 118);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(name, device.available ? color(0xF1F5F9) : color(0x64748B), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 30, -10);

    lv_obj_t *type = lv_label_create(card);
    lv_label_set_text(type, deviceTypeName(device.type));
    lv_obj_set_style_text_font(type, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(type, color(0x64748B), 0);
    lv_obj_align(type, LV_ALIGN_LEFT_MID, 30, 11);

    lv_obj_t *value = lv_label_create(card);
    String text = deviceValue(device);
    lv_label_set_text(value, text.c_str());
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(value, deviceColor(device.type), 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

void renderDetail()
{
  if (activeDevice < 0 || activeDevice >= deviceCount)
  {
    renderDashboard();
    return;
  }

  currentView = View::Detail;
  createShell();
  Device &device = getDevice(activeDevice);
  lv_label_set_text(titleLabel, deviceTypeName(device.type));
  lv_label_set_text(footerLabel, LV_SYMBOL_LEFT " back    turn    press save");
  clearContent();

  lv_obj_t *name = lv_label_create(content);
  lv_label_set_text(name, device.name.c_str());
  lv_label_set_long_mode(name, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(name, SCREEN_W - 32);
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(name, color(0xF8FAFC), 0);
  lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *value = lv_label_create(content);
  String valueText = deviceValue(device);
  lv_label_set_text(value, valueText.c_str());
  lv_obj_set_style_text_font(value, &lv_font_montserrat_22, 0);
  lv_obj_set_style_text_color(value, deviceColor(device.type), 0);
  lv_obj_align(value, LV_ALIGN_TOP_MID, 0, 65);

  if (device.controllable && device.available)
  {
    lv_obj_t *slider = lv_slider_create(content);
    lv_obj_set_size(slider, SCREEN_W - 56, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 115);
    lv_slider_set_range(slider, 0, max(1, static_cast<int>(device.maxValue)));
    lv_slider_set_value(slider, static_cast<int>(device.value), LV_ANIM_ON);
    lv_obj_set_style_bg_color(slider, color(0x273449), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, deviceColor(device.type), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, color(0xF8FAFC), LV_PART_KNOB);

    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, "Rotate to adjust");
    lv_obj_add_style(hint, &mutedTextStyle, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 150);
  }
  else
  {
    lv_obj_t *hint = lv_label_create(content);
    lv_label_set_text(hint, device.available ? "Read-only entity" : "Entity unavailable");
    lv_obj_add_style(hint, &mutedTextStyle, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 125);
  }

  lv_obj_t *area = lv_label_create(content);
  String areaText = LV_SYMBOL_HOME "  " + device.area;
  lv_label_set_text(area, areaText.c_str());
  lv_obj_add_style(area, &mutedTextStyle, 0);
  lv_obj_align(area, LV_ALIGN_BOTTOM_MID, 0, -24);
}

void renderOta(const char *message, uint8_t percentage, lv_color_t accent)
{
  const bool rebuild = currentView != View::Ota || otaBar == nullptr;
  currentView = View::Ota;

  if (rebuild)
  {
    createShell();
    lv_label_set_text(titleLabel, "OTA Update");
    lv_label_set_text(footerLabel, "Keep the device powered");
    clearContent();

    lv_obj_t *icon = lv_label_create(content);
    lv_label_set_text(icon, LV_SYMBOL_DOWNLOAD);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(icon, accent, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 26);

    otaMessageLabel = lv_label_create(content);
    lv_obj_set_style_text_font(otaMessageLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(otaMessageLabel, color(0xF8FAFC), 0);
    lv_obj_align(otaMessageLabel, LV_ALIGN_TOP_MID, 0, 70);

    otaBar = lv_bar_create(content);
    lv_obj_set_size(otaBar, SCREEN_W - 48, 18);
    lv_obj_align(otaBar, LV_ALIGN_TOP_MID, 0, 120);
    lv_bar_set_range(otaBar, 0, 100);
    lv_obj_set_style_bg_color(otaBar, color(0x273449), LV_PART_MAIN);

    otaPercentLabel = lv_label_create(content);
    lv_obj_set_style_text_font(otaPercentLabel, &lv_font_montserrat_18, 0);
    lv_obj_align(otaPercentLabel, LV_ALIGN_TOP_MID, 0, 155);
  }

  lv_label_set_text(otaMessageLabel, message);
  lv_bar_set_value(otaBar, percentage, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(otaBar, accent, LV_PART_INDICATOR);

  String pctText = String(percentage) + "%";
  lv_label_set_text(otaPercentLabel, pctText.c_str());
  lv_obj_set_style_text_color(otaPercentLabel, accent, 0);

  lv_timer_handler();
}

bool inputHasActivity(const InputState &input)
{
  return input.encoderMove || input.enter || input.back || input.backLong ||
         input.shortcut1 || input.shortcut2 || input.shortcut3 ||
         input.shortcut1Long || input.shortcut2Long || input.shortcut3Long;
}

void handleShortcut(const InputState &input)
{
  int shortcut = input.shortcut1 || input.shortcut1Long ? 1 :
                 input.shortcut2 || input.shortcut2Long ? 2 :
                 input.shortcut3 || input.shortcut3Long ? 3 : 0;
  if (!shortcut)
  {
    return;
  }

  const bool held = input.shortcut1Long || input.shortcut2Long || input.shortcut3Long;
  const bool sent = sendShortcutEventToHomeAssistant(shortcut, held);
  showToast("Shortcut " + String(shortcut) + (sent ? " sent" : " failed"),
            sent ? color(0x55D6BE) : color(0xFB7185));
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
  lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, SCREEN_W * DRAW_BUFFER_ROWS);
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = SCREEN_W;
  displayDriver.ver_res = SCREEN_H;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  lv_style_init(&screenStyle);
  lv_style_set_bg_color(&screenStyle, color(0x070B14));
  lv_style_set_bg_opa(&screenStyle, LV_OPA_COVER);

  lv_style_init(&cardStyle);
  lv_style_set_radius(&cardStyle, 10);
  lv_style_set_bg_color(&cardStyle, color(0x111827));
  lv_style_set_bg_opa(&cardStyle, LV_OPA_COVER);
  lv_style_set_border_width(&cardStyle, 1);
  lv_style_set_border_color(&cardStyle, color(0x1E293B));
  lv_style_set_pad_left(&cardStyle, 10);
  lv_style_set_pad_right(&cardStyle, 10);

  lv_style_init(&selectedCardStyle);
  lv_style_set_radius(&selectedCardStyle, 10);
  lv_style_set_bg_color(&selectedCardStyle, color(0x172033));
  lv_style_set_bg_opa(&selectedCardStyle, LV_OPA_COVER);
  lv_style_set_border_width(&selectedCardStyle, 2);
  lv_style_set_border_color(&selectedCardStyle, color(0x38BDF8));
  lv_style_set_pad_left(&selectedCardStyle, 9);
  lv_style_set_pad_right(&selectedCardStyle, 9);

  lv_style_init(&mutedTextStyle);
  lv_style_set_text_font(&mutedTextStyle, &lv_font_montserrat_14);
  lv_style_set_text_color(&mutedTextStyle, color(0x64748B));

  lastLvTick = millis();
  lastInputAt = millis();
  Preferences preferences;
  if (preferences.begin(PREF_NAMESPACE, true))
  {
    sleepSeconds = preferences.getULong(PREF_SLEEP_SECONDS, 0);
    preferences.end();
  }
  seenDeviceRevision = deviceRevision;
  seenBatteryRevision = getBatteryReadingRevision();
  renderDashboard();
}

void handleUIInput(const InputState &input)
{
  if (!inputHasActivity(input))
  {
    return;
  }

  lastInputAt = millis();
  if (screenSleeping)
  {
    wakeScreen();
    return;
  }

  handleShortcut(input);
  if (currentView == View::Ota)
  {
    return;
  }

  if (currentView == View::Dashboard)
  {
    if (input.encoderMove && deviceCount > 0)
    {
      selectedDevice += input.encoderMove > 0 ? 1 : -1;
      if (selectedDevice < 0)
      {
        selectedDevice = deviceCount - 1;
      }
      if (selectedDevice >= deviceCount)
      {
        selectedDevice = 0;
      }
      renderDashboard();
    }

    if (input.enter && deviceCount > 0)
    {
      activeDevice = selectedDevice;
      renderDetail();
    }
    return;
  }

  if (currentView == View::Detail)
  {
    if (input.back || input.backLong)
    {
      renderDashboard();
      return;
    }

    if (activeDevice < 0 || activeDevice >= deviceCount)
    {
      renderDashboard();
      return;
    }

    Device &device = getDevice(activeDevice);
    if (input.encoderMove && device.controllable && device.available)
    {
      const int step = device.type == DeviceType::Light || device.type == DeviceType::Media ? 5 : 1;
      device.value = constrain(device.value + input.encoderMove * step, 0.0f, device.maxValue);
      renderDetail();
    }

    if (input.enter && device.controllable && device.available)
    {
      confirmDeviceValue(device);
      showToast("Saved", color(0x55D6BE));
    }
  }
}

void renderUI()
{
  const unsigned long now = millis();
  lv_tick_inc(now - lastLvTick);
  lastLvTick = now;

  if (!screenSleeping && sleepSeconds > 0 && now - lastInputAt >= sleepSeconds * 1000UL)
  {
    sleepScreen();
  }

  if (screenSleeping)
  {
    if (sleepSeconds > 0 && now - screenSleptAt >= DEEP_SLEEP_DELAY_SECONDS * 1000UL)
    {
      enterDeepSleep();
    }
    return;
  }

  if (currentView != View::Ota && seenDeviceRevision != deviceRevision)
  {
    seenDeviceRevision = deviceRevision;
    selectedDevice = constrain(selectedDevice, 0, max(0, deviceCount - 1));
    currentView == View::Detail ? renderDetail() : renderDashboard();
  }

  if (seenBatteryRevision != getBatteryReadingRevision())
  {
    seenBatteryRevision = getBatteryReadingRevision();
    updateStatus();
  }

  if (toast && toastUntil && now >= toastUntil)
  {
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    toastUntil = 0;
  }

  lv_timer_handler();
}

void showOtaUpdateStart()
{
  wakeScreen();
  renderOta("Receiving firmware", 0, color(0x38BDF8));
}

void showOtaUpdateProgress(uint8_t percentage)
{
  renderOta("Receiving firmware", percentage > 100 ? 100 : percentage, color(0x38BDF8));
}

void showOtaUpdateComplete()
{
  renderOta("Update complete", 100, color(0x55D6BE));
}

void showOtaUpdateError(const char *message)
{
  renderOta(message, 0, color(0xFB7185));
}
