#include <Arduino.h>

#include "battery.h"
#include "homeassistant.h"
#include "input.h"
#include "mqtt_telemetry.h"
#include "ota_update.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);

  initBatteryMonitor();
  initUI();
  initInput();
  initHomeAssistant();
  initMqttTelemetry();
  initOtaUpdate();
}

void loop()
{
  InputState input = readInput();

  updateBatteryMonitor();
  updateHomeAssistant();
  updateMqttTelemetry();
  updateOta();
  handleUIInput(input);
  renderUI();
}
