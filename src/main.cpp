#include <Arduino.h>

#define ENCODER_CLK 20
#define ENCODER_DT 21

volatile int encoderCount = 0;
volatile int lastCLK = HIGH;

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
  int currentCLK = digitalRead(ENCODER_CLK);

  if (currentCLK != lastCLK && currentCLK == LOW)
  {
    if (digitalRead(ENCODER_DT) != currentCLK)
    {
      encoderCount++;
    }
    else
    {
      encoderCount--;
    }

    Serial.print("Count: ");
    Serial.println(encoderCount);
  }

  lastCLK = currentCLK;
}