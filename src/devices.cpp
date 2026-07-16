#include "devices.h"

namespace
{
const Device fallbackDevices[] = {
    {"light.living_room_light", "Living Room Light", "Living Room", "%", DeviceType::Light, true, true, true, 80, 100, false, 0, 0},
    {"fan.bedroom_fan", "Bedroom Fan", "Bedroom", "speed", DeviceType::Fan, true, true, true, 3, 5, false, 0, 0},
    {"sensor.bedroom_temperature", "Temperature", "Bedroom", "C", DeviceType::Sensor, false, true, true, 25, 50, false, 0, 0},
    {"media_player.living_room", "Music Player", "Living Room", "%", DeviceType::Media, true, true, false, 60, 100, false, 0, 0}};

const int fallbackDeviceCount = sizeof(fallbackDevices) / sizeof(Device);
}

Device devices[MAX_DEVICES];
int deviceCount = 0;
unsigned long deviceRevision = 0;

Device &getDevice(int index)
{
  return devices[index];
}

void resetDevicesToFallback()
{
  clearDevices();

  for (int i = 0; i < fallbackDeviceCount; i++)
  {
    addDevice(fallbackDevices[i]);
  }

  deviceRevision++;
}

void clearDevices()
{
  deviceCount = 0;
  deviceRevision++;
}

bool addDevice(const Device &device)
{
  if (deviceCount >= MAX_DEVICES)
  {
    return false;
  }

  devices[deviceCount] = device;
  deviceCount++;
  deviceRevision++;
  return true;
}
