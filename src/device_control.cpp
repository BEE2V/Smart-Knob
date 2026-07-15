#include "device_control.h"

#include <Arduino.h>

void confirmDeviceValue(const Device &device)
{
  Serial.print("Confirmed ");
  Serial.print(device.name);
  Serial.print(": ");
  Serial.println(device.value);
}

void setMediaPlaying(Device &device, bool playing)
{
  device.state = playing;

  Serial.print("Media ");
  Serial.print(device.name);
  Serial.print(": ");
  Serial.println(playing ? "playing" : "paused");
}

void setMediaVolume(const Device &device)
{
  Serial.print("Media ");
  Serial.print(device.name);
  Serial.print(" volume: ");
  Serial.println(device.value);
}
