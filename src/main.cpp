#include <Arduino.h>

#define ENCODER_CLK 20
#define ENCODER_DT 21

int count = 0;
int lastCLK;

void setup()
{
  Serial.begin(115200);

  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);

  lastCLK = digitalRead(ENCODER_CLK);

  Serial.println("Encoder Test");
}

void loop()
{
  int clk = digitalRead(ENCODER_CLK);

  if (clk != lastCLK)
  {

    if (clk == digitalRead(ENCODER_DT))
      count--;
    else
      count++;

    Serial.println(count);
  }

  lastCLK = clk;
}