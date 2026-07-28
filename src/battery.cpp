#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace
{
constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 2000;
constexpr int ADC_SAMPLES_PER_READING = 16;
constexpr int MOVING_AVERAGE_SIZE = 8;

struct BatteryCalibrationPoint
{
  float adcVoltage;
  float senseVoltage;
  float batteryVoltage;
};

// Bench-calibrated against DMM measurements on 2026-07-28. Each point is the
// average of the descending and ascending voltage sweeps.
constexpr BatteryCalibrationPoint BATTERY_CALIBRATION[] = {
    {2.0865f, 2.1075f, 3.1750f},
    {2.1795f, 2.1810f, 3.2930f},
    {2.2320f, 2.2450f, 3.3925f},
    {2.2985f, 2.3120f, 3.4930f},
    {2.3700f, 2.3805f, 3.5965f},
    {2.4350f, 2.4465f, 3.6955f},
    {2.5025f, 2.5135f, 3.7975f},
    {2.5755f, 2.5765f, 3.8925f},
    {2.6565f, 2.6475f, 3.9990f},
    {2.7175f, 2.7135f, 4.0995f},
    {2.7840f, 2.7720f, 4.1895f},
};
constexpr int BATTERY_CALIBRATION_POINT_COUNT =
    sizeof(BATTERY_CALIBRATION) / sizeof(BATTERY_CALIBRATION[0]);

uint32_t lastBatterySampleMs = 0;
uint32_t batteryReadingRevision = 0;
float batteryRawAdcVoltage = 0.0f;
float batterySenseVoltage = 0.0f;
float batteryVoltage = 0.0f;
int batteryPercentage = 0;
bool batteryReadingAvailable = false;
float adcVoltageHistory[MOVING_AVERAGE_SIZE] = {0.0f};
float adcVoltageSum = 0.0f;
int nextAdcVoltageIndex = 0;

float interpolateCalibration(float adcVoltage, bool returnBatteryVoltage)
{
  int lowerIndex = 0;

  if (adcVoltage >= BATTERY_CALIBRATION[BATTERY_CALIBRATION_POINT_COUNT - 1].adcVoltage)
  {
    lowerIndex = BATTERY_CALIBRATION_POINT_COUNT - 2;
  }
  else if (adcVoltage > BATTERY_CALIBRATION[0].adcVoltage)
  {
    for (int i = 1; i < BATTERY_CALIBRATION_POINT_COUNT; ++i)
    {
      if (adcVoltage <= BATTERY_CALIBRATION[i].adcVoltage)
      {
        lowerIndex = i - 1;
        break;
      }
    }
  }

  const BatteryCalibrationPoint &lower = BATTERY_CALIBRATION[lowerIndex];
  const BatteryCalibrationPoint &upper = BATTERY_CALIBRATION[lowerIndex + 1];
  const float position =
      (adcVoltage - lower.adcVoltage) / (upper.adcVoltage - lower.adcVoltage);
  const float lowerValue =
      returnBatteryVoltage ? lower.batteryVoltage : lower.senseVoltage;
  const float upperValue =
      returnBatteryVoltage ? upper.batteryVoltage : upper.senseVoltage;

  return lowerValue + position * (upperValue - lowerValue);
}

float readAdcVoltage()
{
  uint32_t millivoltTotal = 0;

  for (int sample = 0; sample < ADC_SAMPLES_PER_READING; ++sample)
  {
    millivoltTotal += analogReadMilliVolts(BATTERY_SENSE_PIN);
  }

  return (millivoltTotal / static_cast<float>(ADC_SAMPLES_PER_READING)) / 1000.0f;
}

float filterAdcVoltage(float reading)
{
  if (!batteryReadingAvailable)
  {
    adcVoltageSum = reading * MOVING_AVERAGE_SIZE;
    for (int i = 0; i < MOVING_AVERAGE_SIZE; ++i)
    {
      adcVoltageHistory[i] = reading;
    }
    nextAdcVoltageIndex = 0;
    return reading;
  }

  adcVoltageSum -= adcVoltageHistory[nextAdcVoltageIndex];
  adcVoltageHistory[nextAdcVoltageIndex] = reading;
  adcVoltageSum += reading;
  nextAdcVoltageIndex = (nextAdcVoltageIndex + 1) % MOVING_AVERAGE_SIZE;

  return adcVoltageSum / MOVING_AVERAGE_SIZE;
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

  batteryRawAdcVoltage = filterAdcVoltage(readAdcVoltage());
  batterySenseVoltage = interpolateCalibration(batteryRawAdcVoltage, false);
  batteryVoltage = interpolateCalibration(batteryRawAdcVoltage, true);
  batteryPercentage = voltageToPercentage(batteryVoltage);
  batteryReadingAvailable = true;
  batteryReadingRevision++;

  Serial.print("Battery: ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" V (sense: ");
  Serial.print(batterySenseVoltage, 3);
  Serial.print(" V, ADC raw: ");
  Serial.print(batteryRawAdcVoltage, 3);
  Serial.print(" V, ");
  Serial.print(batteryPercentage);
  Serial.println("%)");
}

bool hasBatteryReading()
{
  return batteryReadingAvailable;
}

float getBatteryRawAdcVoltage()
{
  return batteryRawAdcVoltage;
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
