#include <Arduino.h>

#include "input.h"
#include "ui.h"

void setup()
{
  Serial.begin(115200);

  initUI();
  initInput();
}

void loop()
{
  InputState input = readInput();

  handleUIInput(input);
  renderUI();
}
