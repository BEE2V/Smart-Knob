#include "devices.h"

Device devices[] = {
    {"light.living_room_light", "Living Room Light", "Living Room", DeviceType::Light, true, true, 80, 100},
    {"fan.bedroom_fan", "Bedroom Fan", "Bedroom", DeviceType::Fan, true, true, 3, 5},
    {"sensor.bedroom_temperature", "Temperature", "Bedroom", DeviceType::Sensor, false, true, 25, 50},
    {"media_player.living_room", "Music Player", "Living Room", DeviceType::Media, true, false, 60, 100}};

const int deviceCount = sizeof(devices) / sizeof(Device);

Device &getDevice(int index)
{
  return devices[index];
}
