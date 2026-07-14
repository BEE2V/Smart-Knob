#include <Arduino.h>

// Encoder
#define ENC_CLK 20
#define ENC_DT 21
#define ENC_SW 47

// Buttons
#define BTN1 48
#define BTN2 45
#define BTN3 0
#define BTN4 35

int encoderCount = 0;
int lastCLK;

void setup()
{
  Serial.begin(115200);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  lastCLK = digitalRead(ENC_CLK);

  Serial.println("Smart Home Input Test");
}

void loop()
{

  // Encoder
  int clk = digitalRead(ENC_CLK);

  if (clk != lastCLK)
  {

    if (clk == digitalRead(ENC_DT))
      encoderCount--;
    else
      encoderCount++;

    Serial.print("Encoder: ");
    Serial.println(encoderCount);
  }

  lastCLK = clk;

  // Encoder button
  if (!digitalRead(ENC_SW))
  {
    Serial.println("Encoder SW");
    delay(200);
  }

  // Buttons
  if (!digitalRead(BTN1))
  {
    Serial.println("BTN1");
    delay(200);
  }

  if (!digitalRead(BTN2))
  {
    Serial.println("BTN2");
    delay(200);
  }

  if (!digitalRead(BTN3))
  {
    Serial.println("BTN3");
    delay(200);
  }

  if (!digitalRead(BTN4))
  {
    Serial.println("BTN4");
    delay(200);
  }
}