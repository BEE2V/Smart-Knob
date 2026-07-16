#pragma once

#include "devices.h"

void initHomeAssistant();
void updateHomeAssistant();
bool isHomeAssistantReady();
bool hasHomeAssistantDeviceList();
bool refreshHomeAssistantEntity(Device &device);
int fetchHomeAssistantHistory(const Device &device, float *samples, int maxSamples);

bool sendDeviceValueToHomeAssistant(const Device &device);
bool sendMediaPlaybackToHomeAssistant(const Device &device, bool playing);
bool sendMediaVolumeToHomeAssistant(const Device &device);
bool sendShortcutEventToHomeAssistant(int shortcutNumber, bool longPress);
