#pragma once

#include <Arduino.h>

void initBatteryMonitor();
void updateBatteryMonitor();

bool hasBatteryReading();
float getBatterySenseVoltage();
float getBatteryVoltage();
int getBatteryPercentage();
uint32_t getBatteryReadingRevision();
