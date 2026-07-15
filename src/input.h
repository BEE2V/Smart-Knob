#pragma once

struct InputState
{
  int encoderMove = 0;
  bool up = false;
  bool down = false;
  bool back = false;
  bool enter = false;
};

void initInput();
InputState readInput();
