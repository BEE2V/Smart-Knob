#pragma once

struct InputState
{
  int encoderMove = 0;
  bool shortcut1 = false;
  bool shortcut2 = false;
  bool shortcut3 = false;
  bool shortcut1Long = false;
  bool shortcut2Long = false;
  bool shortcut3Long = false;
  bool back = false;
  bool backLong = false;
  bool enter = false;
};

void initInput();
InputState readInput();
