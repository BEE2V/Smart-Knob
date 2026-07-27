#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace
{
constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 2000;
constexpr int ADC_SAMPLES_PER_READING = 16;
constexpr int MOVING_AVERAGE_SIZE = 8;

uint32_t lastBatterySampleMs = 0;
uint32_t batteryReadingRevision = 0;
float batterySenseVoltage = 0.0f;
float batteryVoltage = 0.0f;
int batteryPercentage = 0;
bool batteryReadingAvailable = false;
float senseVoltageHistory[MOVING_AVERAGE_SIZE] = {0.0f};
float senseVoltageSum = 0.0f;
int nextSenseVoltageIndex = 0;

float readSenseVoltage()
{
  uint32_t millivoltTotal = 0;

  for (int sample = 0; sample < ADC_SAMPLES_PER_READING; ++sample)
  {
    millivoltTotal += analogReadMilliVolts(BATTERY_SENSE_PIN);
  }

  const float adcVoltage =
      (millivoltTotal / static_cast<float>(ADC_SAMPLES_PER_READING)) / 1000.0f;
  return adcVoltage * BATTERY_ADC_CORRECTION_RATIO;
}

float filterSenseVoltage(float reading)
{
  if (!batteryReadingAvailable)
  {
    senseVoltageSum = reading * MOVING_AVERAGE_SIZE;
    for (int i = 0; i < MOVING_AVERAGE_SIZE; ++i)
    {
      senseVoltageHistory[i] = reading;
    }
    nextSenseVoltageIndex = 0;
    return reading;
  }

  senseVoltageSum -= senseVoltageHistory[nextSenseVoltageIndex];
  senseVoltageHistory[nextSenseVoltageIndex] = reading;
  senseVoltageSum += reading;
  nextSenseVoltageIndex = (nextSenseVoltageIndex + 1) % MOVING_AVERAGE_SIZE;

  return senseVoltageSum / MOVING_AVERAGE_SIZE;
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

  // Allow the first filtered reading to be available immediately.
  lastBatterySampleMs = millis() - BATTERY_SAMPLE_INTERVAL_MS;
}

void updateBatteryMonitor()
{
  const uint32_t now = millis();
  if (now - lastBatterySampleMs < BATTERY_SAMPLE_INTERVAL_MS)
  {
    return;
  }
  lastBatterySampleMs = now;

  batterySenseVoltage = filterSenseVoltage(readSenseVoltage());
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
