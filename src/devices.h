#pragma once

#include <Arduino.h>

constexpr int MAX_DEVICES = 24;

enum class DeviceType
{
  Light,
  Fan,
  Sensor,
  BinarySensor,
  Media
};

struct Device
{
  String entityId;
  String name;
  String area;
  String unit;
  DeviceType type;
  bool controllable;
  bool available;
  bool state;
  int value;
  int maxValue;
};

extern Device devices[];
extern int deviceCount;
extern unsigned long deviceRevision;

Device &getDevice(int index);
void resetDevicesToFallback();
void clearDevices();
bool addDevice(const Device &device);
