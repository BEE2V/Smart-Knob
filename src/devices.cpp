#include "devices.h"

Device devices[] = {
    {"Living Room Light", "Living Room", DeviceType::Light, true, true, 80, 100},
    {"Bedroom Fan", "Bedroom", DeviceType::Fan, true, true, 3, 5},
    {"Temperature", "Bedroom", DeviceType::Sensor, false, true, 25, 50},
    {"Music Player", "Living Room", DeviceType::Media, true, false, 60, 100}};

const int deviceCount = sizeof(devices) / sizeof(Device);

Device &getDevice(int index)
{
  return devices[index];
}
