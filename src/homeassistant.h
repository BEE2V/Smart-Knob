#pragma once

#include "devices.h"

void initHomeAssistant();
void updateHomeAssistant();
bool isHomeAssistantReady();
bool hasHomeAssistantDeviceList();
bool refreshHomeAssistantEntity(Device &device);

bool sendDeviceValueToHomeAssistant(const Device &device);
bool sendMediaPlaybackToHomeAssistant(const Device &device, bool playing);
bool sendMediaVolumeToHomeAssistant(const Device &device);
