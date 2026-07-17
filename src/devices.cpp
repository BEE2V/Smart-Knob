#include "devices.h"

namespace
{
const Device fallbackDevices[] = {
    {"light.living_room_light", "Living Room Light", "Living Room", "%", DeviceType::Light, true, true, true, 80, 100, false, 0, 0, false, 0, 0, {}},
    {"fan.bedroom_fan", "Bedroom Fan", "Bedroom", "speed", DeviceType::Fan, true, true, true, 3, 5, false, 0, 0, false, 0, 0, {}},
    {"sensor.bedroom_temperature", "Temperature", "Bedroom", "C", DeviceType::Sensor, false, true, true, 25, 50, false, 0, 0, false, 0, 0, {}},
    {"media_player.living_room", "Music Player", "Living Room", "%", DeviceType::Media, true, true, false, 60, 100, false, 0, 0, false, 0, 0, {}}};

const int fallbackDeviceCount = sizeof(fallbackDevices) / sizeof(Device);
}

Device devices[MAX_DEVICES];
int deviceCount = 0;
String areas[MAX_AREAS];
int areaCount = 0;
unsigned long deviceRevision = 0;

Device &getDevice(int index)
{
  return devices[index];
}

String getArea(int index)
{
  if (index < 0 || index >= areaCount)
  {
    return "";
  }

  return areas[index];
}

void rebuildAreaList()
{
  areaCount = 0;

  for (int i = 0; i < deviceCount; i++)
  {
    if (devices[i].area.length() == 0)
    {
      continue;
    }

    bool alreadyAdded = false;

    for (int j = 0; j < areaCount; j++)
    {
      if (areas[j] == devices[i].area)
      {
        alreadyAdded = true;
        break;
      }
    }

    if (!alreadyAdded && areaCount < MAX_AREAS)
    {
      areas[areaCount] = devices[i].area;
      areaCount++;
    }
  }

  if (areaCount == 0 && deviceCount > 0)
  {
    areas[0] = "Home Assistant";
    areaCount = 1;
  }

  deviceRevision++;
}

void resetDevicesToFallback()
{
  clearDevices();

  for (int i = 0; i < fallbackDeviceCount; i++)
  {
    addDevice(fallbackDevices[i]);
  }

  rebuildAreaList();
}

void clearDevices()
{
  deviceCount = 0;
  areaCount = 0;
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
