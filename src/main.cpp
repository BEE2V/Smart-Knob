#include <Arduino.h>

#include "homeassistant.h"
#include "input.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);

  initUI();
  initInput();
  initHomeAssistant();
}

void loop()
{
  InputState input = readInput();

  updateHomeAssistant();
  handleUIInput(input);
  renderUI();
}
