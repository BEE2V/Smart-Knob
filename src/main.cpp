#include <Arduino.h>

#include "battery.h"
#include "homeassistant.h"
#include "input.h"
#include "mqtt_telemetry.h"
#include "ota_update.h"
#include "power_management.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);

  initPowerManagement();
  initBatteryMonitor();
  initUI();
  initInput();
  initHomeAssistant();
  initMqttTelemetry();
  initOtaUpdate();
}

void loop()
{
  recordRuntimeStage(RuntimeStage::Input);
  InputState input = readInput();

  recordRuntimeStage(RuntimeStage::Battery);
  updateBatteryMonitor();
  recordRuntimeStage(RuntimeStage::HomeAssistant);
  updateHomeAssistant();
  recordRuntimeStage(RuntimeStage::Mqtt);
  updateMqttTelemetry();
  recordRuntimeStage(RuntimeStage::Ota);
  updateOta();
  recordRuntimeStage(RuntimeStage::UiInput);
  handleUIInput(input);
  recordRuntimeStage(RuntimeStage::UiRender);
  renderUI();
  recordRuntimeStage(RuntimeStage::Idle);
  delay(1);
}
