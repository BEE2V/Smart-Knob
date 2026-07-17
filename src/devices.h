#pragma once

#include <Arduino.h>

constexpr int MAX_DEVICES = 24;
constexpr int MAX_AREAS = 8;
constexpr int MAX_LIGHT_EFFECTS = 8;

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
  float value;
  float maxValue;
  bool supportsColor;
  float hue;
  float saturation;
  bool supportsEffects;
  int effectCount;
  int effectIndex;
  String effects[MAX_LIGHT_EFFECTS];
};

extern Device devices[];
extern int deviceCount;
extern String areas[];
extern int areaCount;
extern unsigned long deviceRevision;

Device &getDevice(int index);
String getArea(int index);
void resetDevicesToFallback();
void clearDevices();
bool addDevice(const Device &device);
void rebuildAreaList();
