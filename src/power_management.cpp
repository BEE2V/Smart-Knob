#include "power_management.h"

#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "config.h"
#include "mqtt_telemetry.h"
#include "ota_update.h"

namespace
{
const char *resetReasonText(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON: return "power-on";
  case ESP_RST_EXT: return "external reset pin";
  case ESP_RST_SW: return "software restart";
  case ESP_RST_PANIC: return "exception/panic";
  case ESP_RST_INT_WDT: return "interrupt watchdog";
  case ESP_RST_TASK_WDT: return "task watchdog";
  case ESP_RST_WDT: return "other watchdog";
  case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
  case ESP_RST_BROWNOUT: return "brownout";
  case ESP_RST_SDIO: return "SDIO";
  default: return "unknown";
  }
}
} // namespace

void initPowerManagement()
{
  const esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.print("Reset reason: ");
  Serial.print(resetReasonText(resetReason));
  Serial.print(" (");
  Serial.print(static_cast<int>(resetReason));
  Serial.println(")");

  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0)
  {
    Serial.println("Wake reason: rotary encoder");
  }
}

void enterDeepSleep()
{
  // Use one RTC-capable encoder phase and wake on its opposite level.
  // Either rotation direction changes ENC_CLK during a complete click.
  const int clockLevel = digitalRead(ENC_CLK);
  const int wakeLevel = clockLevel == HIGH ? LOW : HIGH;

  Serial.println("Entering deep sleep; rotate encoder to wake");
  prepareMqttForDeepSleep();
  prepareOtaForDeepSleep();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  rtc_gpio_pullup_en(static_cast<gpio_num_t>(ENC_CLK));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(ENC_CLK));

  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(ENC_CLK), wakeLevel);
  delay(20);
  esp_deep_sleep_start();
}
