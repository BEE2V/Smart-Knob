#include "power_management.h"

#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>

#include "config.h"
#include "mqtt_telemetry.h"
#include "ota_update.h"

namespace
{
constexpr uint32_t RUNTIME_STAGE_MAGIC = 0x534B5744;

RTC_NOINIT_ATTR uint32_t runtimeStageMagic;
RTC_NOINIT_ATTR uint8_t runtimeStages[2];

const char *currentResetReason = "unknown";
char resetDiagnostic[64] = "unknown";

const char *runtimeStageText(uint8_t stage)
{
  switch (static_cast<RuntimeStage>(stage))
  {
  case RuntimeStage::Boot: return "boot";
  case RuntimeStage::System: return "system";
  case RuntimeStage::Input: return "input";
  case RuntimeStage::Battery: return "battery";
  case RuntimeStage::HomeAssistant: return "HA";
  case RuntimeStage::Mqtt: return "MQTT";
  case RuntimeStage::Ota: return "OTA";
  case RuntimeStage::UiInput: return "UI input";
  case RuntimeStage::UiRender: return "UI render";
  case RuntimeStage::History: return "history";
  case RuntimeStage::Idle: return "idle";
  }
  return "invalid";
}

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
  currentResetReason = resetReasonText(resetReason);
  if (resetReason == ESP_RST_INT_WDT && runtimeStageMagic == RUNTIME_STAGE_MAGIC)
  {
    snprintf(resetDiagnostic, sizeof(resetDiagnostic), "WDT c0:%s c1:%s",
             runtimeStageText(runtimeStages[0]),
             runtimeStageText(runtimeStages[1]));
  }
  else
  {
    snprintf(resetDiagnostic, sizeof(resetDiagnostic), "%s", currentResetReason);
  }

  Serial.print("Reset reason: ");
  Serial.print(currentResetReason);
  Serial.print(" (");
  Serial.print(static_cast<int>(resetReason));
  Serial.print("), diagnostic: ");
  Serial.println(resetDiagnostic);

  runtimeStageMagic = RUNTIME_STAGE_MAGIC;
  runtimeStages[0] = static_cast<uint8_t>(RuntimeStage::System);
  runtimeStages[1] = static_cast<uint8_t>(RuntimeStage::Boot);

  const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
  if (wakeCause == ESP_SLEEP_WAKEUP_EXT0)
  {
    Serial.println("Wake reason: rotary encoder");
  }
}

const char *getResetReasonText()
{
  return currentResetReason;
}

const char *getResetDiagnosticText()
{
  return resetDiagnostic;
}

void recordRuntimeStage(RuntimeStage stage)
{
  const BaseType_t core = xPortGetCoreID();
  if (core >= 0 && core < 2)
  {
    runtimeStages[core] = static_cast<uint8_t>(stage);
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
