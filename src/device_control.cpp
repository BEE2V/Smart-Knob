#include "device_control.h"

#include <Arduino.h>

#include "homeassistant.h"

void confirmDeviceValue(const Device &device)
{
  Serial.print("Confirmed ");
  Serial.print(device.name);
  Serial.print(": ");
  Serial.println(device.value);

  sendDeviceValueToHomeAssistant(device);
}

void setMediaPlaying(Device &device, bool playing)
{
  device.state = playing;

  Serial.print("Media ");
  Serial.print(device.name);
  Serial.print(": ");
  Serial.println(playing ? "playing" : "paused");

  sendMediaPlaybackToHomeAssistant(device, playing);
}

void setMediaVolume(const Device &device)
{
  Serial.print("Media ");
  Serial.print(device.name);
  Serial.print(" volume: ");
  Serial.println(device.value);

  sendMediaVolumeToHomeAssistant(device);
}
