#include "input.h"

#include <Arduino.h>

#include "config.h"

struct Button
{
  int pin;
  bool stablePressed;
  bool lastReading;
  bool longReported;
  unsigned long changedAt;
  unsigned long pressedAt;
};

struct ButtonEvent
{
  bool shortPress = false;
  bool longPress = false;
};

int lastEncoded = 0;
int encoderSteps = 0;

Button buttons[5] = {
    {BTN1, false, false, false, 0, 0},
    {BTN2, false, false, false, 0, 0},
    {BTN3, false, false, false, 0, 0},
    {BTN4, false, false, false, 0, 0},
    {ENC_SW, false, false, false, 0, 0}};

constexpr unsigned long BUTTON_DEBOUNCE_MS = 60;
constexpr unsigned long BUTTON_LONG_PRESS_MS = 800;

int readEncoder()
{
  int msb = digitalRead(ENC_CLK);
  int lsb = digitalRead(ENC_DT);
  int encoded = (msb << 1) | lsb;
  int sum = (lastEncoded << 2) | encoded;
  int move = 0;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
  {
    move = 1;
  }

  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
  {
    move = -1;
  }

  lastEncoded = encoded;

  if (move)
  {
    encoderSteps += move;

    if (abs(encoderSteps) >= 2)
    {
      encoderSteps = 0;
      return move;
    }
  }

  return 0;
}

ButtonEvent readButton(int i)
{
  ButtonEvent event;
  bool now = !digitalRead(buttons[i].pin);
  unsigned long currentMillis = millis();

  if (now != buttons[i].lastReading)
  {
    buttons[i].lastReading = now;
    buttons[i].changedAt = currentMillis;
  }

  if (currentMillis - buttons[i].changedAt > BUTTON_DEBOUNCE_MS && now != buttons[i].stablePressed)
  {
    buttons[i].stablePressed = now;

    if (now)
    {
      buttons[i].pressedAt = currentMillis;
      buttons[i].longReported = false;
    }
    else if (!buttons[i].longReported)
    {
      event.shortPress = true;
    }
  }

  if (buttons[i].stablePressed &&
      !buttons[i].longReported &&
      currentMillis - buttons[i].pressedAt >= BUTTON_LONG_PRESS_MS)
  {
    buttons[i].longReported = true;
    event.longPress = true;
  }

  return event;
}

void initInput()
{
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  lastEncoded = (digitalRead(ENC_CLK) << 1) | digitalRead(ENC_DT);
}

InputState readInput()
{
  InputState input;

  input.encoderMove = readEncoder();
  ButtonEvent shortcut1 = readButton(0);
  ButtonEvent shortcut2 = readButton(1);
  ButtonEvent shortcut3 = readButton(2);
  ButtonEvent back = readButton(3);
  ButtonEvent enter = readButton(4);

  input.shortcut1 = shortcut1.shortPress;
  input.shortcut2 = shortcut2.shortPress;
  input.shortcut3 = shortcut3.shortPress;
  input.shortcut1Long = shortcut1.longPress;
  input.shortcut2Long = shortcut2.longPress;
  input.shortcut3Long = shortcut3.longPress;
  input.back = back.shortPress;
  input.backLong = back.longPress;
  input.enter = enter.shortPress;

  return input;
}
