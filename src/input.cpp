#include "input.h"

#include <Arduino.h>

#include "config.h"

struct Button
{
  int pin;
  bool last;
  unsigned long timer;
};

int lastEncoded = 0;
int encoderSteps = 0;

Button buttons[5] = {
    {BTN1, false, 0},
    {BTN2, false, 0},
    {BTN3, false, 0},
    {BTN4, false, 0},
    {ENC_SW, false, 0}};

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

bool buttonPressed(int i)
{
  bool now = !digitalRead(buttons[i].pin);

  if (now != buttons[i].last && millis() - buttons[i].timer > 60)
  {
    buttons[i].timer = millis();
    buttons[i].last = now;

    if (now)
    {
      return true;
    }
  }

  return false;
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
  input.up = buttonPressed(0);
  input.down = buttonPressed(2);
  input.back = buttonPressed(3);
  input.enter = buttonPressed(4);

  return input;
}
