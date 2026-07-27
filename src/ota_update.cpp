#include "ota_update.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "battery.h"
#include "secrets.h"
#include "ui.h"

namespace
{
constexpr int MINIMUM_OTA_BATTERY_PERCENT = 30;
constexpr uint32_t OTA_STATUS_INTERVAL_MS = 30000;

bool otaServiceStarted = false;
bool otaUpdateRunning = false;
uint8_t lastProgress = 255;
uint32_t lastOtaStatusPrint = 0;
String otaHostname;

String compactChipId()
{
  const uint64_t chipId = ESP.getEfuseMac();
  char id[13];
  snprintf(id, sizeof(id), "%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));
  return String(id);
}

const char *otaErrorText(ota_error_t error)
{
  switch (error)
  {
  case OTA_AUTH_ERROR:
    return "authentication";
  case OTA_BEGIN_ERROR:
    return "begin failed";
  case OTA_CONNECT_ERROR:
    return "connection";
  case OTA_RECEIVE_ERROR:
    return "receive failed";
  case OTA_END_ERROR:
    return "finalize failed";
  }

  return "unknown error";
}

void startOtaService()
{
  ArduinoOTA.begin();
  otaServiceStarted = true;

  Serial.print("OTA ready: ");
  Serial.print(otaHostname);
  Serial.print(".local (");
  Serial.print(WiFi.localIP());
  Serial.println(")");
}

void stopOtaService()
{
  if (!otaServiceStarted || otaUpdateRunning)
  {
    return;
  }

  ArduinoOTA.end();
  otaServiceStarted = false;
  Serial.println("OTA paused: Wi-Fi disconnected");
}
} // namespace

void initOtaUpdate()
{
  otaHostname = "smart-knob-" + compactChipId();

  ArduinoOTA.setHostname(otaHostname.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setRebootOnSuccess(true);

  ArduinoOTA.onStart([]()
                     {
    otaUpdateRunning = true;
    lastProgress = 255;
    Serial.println("OTA update started");
    showOtaUpdateStart(); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
    if (total == 0)
    {
      return;
    }

    const uint8_t percentage = static_cast<uint8_t>((progress * 100U) / total);
    if (percentage != lastProgress)
    {
      lastProgress = percentage;
      Serial.print("OTA progress: ");
      Serial.print(percentage);
      Serial.println("%");
      showOtaUpdateProgress(percentage);
    } });

  ArduinoOTA.onEnd([]()
                   {
    showOtaUpdateProgress(100);
    Serial.println("OTA update complete; restarting");
    showOtaUpdateComplete(); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    otaUpdateRunning = false;
    const char *message = otaErrorText(error);
    Serial.print("OTA error: ");
    Serial.println(message);
    showOtaUpdateError(message); });
}

void updateOta()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    stopOtaService();
    return;
  }

  if (!otaServiceStarted)
  {
    if (!hasBatteryReading() ||
        getBatteryPercentage() < MINIMUM_OTA_BATTERY_PERCENT)
    {
      const uint32_t now = millis();
      if (now - lastOtaStatusPrint >= OTA_STATUS_INTERVAL_MS)
      {
        lastOtaStatusPrint = now;
        Serial.print("OTA unavailable: battery must be at least ");
        Serial.print(MINIMUM_OTA_BATTERY_PERCENT);
        Serial.println("%");
      }
      return;
    }

    startOtaService();
  }

  ArduinoOTA.handle();
}

void prepareOtaForDeepSleep()
{
  if (!otaServiceStarted || otaUpdateRunning)
  {
    return;
  }

  ArduinoOTA.end();
  otaServiceStarted = false;
}
