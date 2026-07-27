#include "power_management.h"

#include <Arduino.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "config.h"
#include "mqtt_telemetry.h"
#include "ota_update.h"

void initPowerManagement()
{
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
