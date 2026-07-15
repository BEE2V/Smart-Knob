#pragma once

enum class DeviceType
{
  Light,
  Fan,
  Sensor,
  Media
};

struct Device
{
  const char *entityId;
  const char *name;
  const char *area;
  DeviceType type;
  bool controllable;
  bool state;
  int value;
  int maxValue;
};

extern Device devices[];
extern const int deviceCount;

Device &getDevice(int index);
