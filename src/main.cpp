#include <Arduino.h>

#include "battery.h"
#include "homeassistant.h"
#include "input.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);

  initBatteryMonitor();
  initUI();
  initInput();
  initHomeAssistant();
}

void loop()
{
  InputState input = readInput();

  updateBatteryMonitor();
  updateHomeAssistant();
  handleUIInput(input);
  renderUI();
}
