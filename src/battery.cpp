#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace
{
constexpr uint32_t BATTERY_PRINT_INTERVAL_MS = 1000;
constexpr int BATTERY_SAMPLE_COUNT = 16;

uint32_t lastBatteryPrintMs = 0;
uint32_t batteryReadingRevision = 0;
float batterySenseVoltage = 0.0f;
float batteryVoltage = 0.0f;
int batteryPercentage = 0;
bool batteryReadingAvailable = false;

float readSenseVoltage()
{
  uint32_t millivoltTotal = 0;

  for (int sample = 0; sample < BATTERY_SAMPLE_COUNT; ++sample)
  {
    millivoltTotal += analogReadMilliVolts(BATTERY_SENSE_PIN);
  }

  return (millivoltTotal / static_cast<float>(BATTERY_SAMPLE_COUNT)) / 1000.0f;
}

int voltageToPercentage(float voltage)
{
  // Approximate resting-voltage curve for a single-cell Li-ion battery.
  constexpr float VOLTAGES[] = {3.20f, 3.40f, 3.55f, 3.65f, 3.72f, 3.78f,
                                3.85f, 3.92f, 4.00f, 4.10f, 4.19f};
  constexpr int PERCENTAGES[] = {0, 5, 10, 20, 30, 40, 50, 65, 80, 95, 100};
  constexpr int POINT_COUNT = sizeof(VOLTAGES) / sizeof(VOLTAGES[0]);

  if (voltage <= VOLTAGES[0])
  {
    return 0;
  }

  for (int i = 1; i < POINT_COUNT; ++i)
  {
    if (voltage <= VOLTAGES[i])
    {
      const float position = (voltage - VOLTAGES[i - 1]) /
                             (VOLTAGES[i] - VOLTAGES[i - 1]);
      return static_cast<int>(roundf(PERCENTAGES[i - 1] +
                                     position * (PERCENTAGES[i] - PERCENTAGES[i - 1])));
    }
  }

  return 100;
}
} // namespace

void initBatteryMonitor()
{
  pinMode(BATTERY_SENSE_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_SENSE_PIN, ADC_11db);

  // Allow the first update to print immediately.
  lastBatteryPrintMs = millis() - BATTERY_PRINT_INTERVAL_MS;
}

void updateBatteryMonitor()
{
  const uint32_t now = millis();
  if (now - lastBatteryPrintMs < BATTERY_PRINT_INTERVAL_MS)
  {
    return;
  }
  lastBatteryPrintMs = now;

  batterySenseVoltage = readSenseVoltage();
  batteryVoltage = batterySenseVoltage * BATTERY_CALIBRATION_RATIO;
  batteryPercentage = voltageToPercentage(batteryVoltage);
  batteryReadingAvailable = true;
  batteryReadingRevision++;

  Serial.print("Battery: ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" V (sense: ");
  Serial.print(batterySenseVoltage, 3);
  Serial.print(" V, ");
  Serial.print(batteryPercentage);
  Serial.println("%)");
}

bool hasBatteryReading()
{
  return batteryReadingAvailable;
}

float getBatterySenseVoltage()
{
  return batterySenseVoltage;
}

float getBatteryVoltage()
{
  return batteryVoltage;
}

int getBatteryPercentage()
{
  return batteryPercentage;
}

uint32_t getBatteryReadingRevision()
{
  return batteryReadingRevision;
}
