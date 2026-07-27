#include <Arduino.h>

#include "battery.h"
#include "homeassistant.h"
#include "input.h"
#include "mqtt_telemetry.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);

  initBatteryMonitor();
  initUI();
  initInput();
  initHomeAssistant();
  initMqttTelemetry();
}

void loop()
{
  InputState input = readInput();

  updateBatteryMonitor();
  updateHomeAssistant();
  updateMqttTelemetry();
  handleUIInput(input);
  renderUI();
}
