#include "homeassistant.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"

namespace
{
bool wifiStarted = false;
bool haConfigured = false;
unsigned long lastStatusPrint = 0;

bool hasText(const char *value)
{
  return value != nullptr && value[0] != '\0';
}

const char *serviceForValue(const Device &device)
{
  switch (device.type)
  {
  case DeviceType::Light:
    return "light/turn_on";
  case DeviceType::Fan:
    return "fan/set_percentage";
  case DeviceType::Media:
    return "media_player/volume_set";
  case DeviceType::Sensor:
    return "";
  }

  return "";
}

String valuePayload(const Device &device)
{
  String payload = "{\"entity_id\":\"";
  payload += device.entityId;
  payload += "\"";

  switch (device.type)
  {
  case DeviceType::Light:
    payload += ",\"brightness_pct\":";
    payload += device.value;
    break;
  case DeviceType::Fan:
    payload += ",\"percentage\":";
    payload += map(device.value, 0, device.maxValue, 0, 100);
    break;
  case DeviceType::Media:
    payload += ",\"volume_level\":";
    payload += String(device.value / 100.0f, 2);
    break;
  case DeviceType::Sensor:
    break;
  }

  payload += "}";
  return payload;
}

bool postService(const char *service, const String &payload)
{
  if (!isHomeAssistantReady())
  {
    Serial.println("HA command skipped: not connected");
    return false;
  }

  HTTPClient http;
  String url = String(HA_BASE_URL) + "/api/services/" + service;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int status = http.POST(payload);

  Serial.print("HA POST ");
  Serial.print(service);
  Serial.print(" -> ");
  Serial.println(status);

  http.end();

  return status >= 200 && status < 300;
}
}

void initHomeAssistant()
{
  haConfigured = hasText(WIFI_SSID) && hasText(HA_BASE_URL) && hasText(HA_TOKEN);

  if (!haConfigured)
  {
    Serial.println("HA disabled: Wi-Fi/Home Assistant config is empty");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiStarted = true;

  Serial.print("Wi-Fi connecting to ");
  Serial.println(WIFI_SSID);
}

void updateHomeAssistant()
{
  if (!haConfigured || !wifiStarted)
  {
    return;
  }

  if (millis() - lastStatusPrint < 5000)
  {
    return;
  }

  lastStatusPrint = millis();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.print("Wi-Fi connected: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("Wi-Fi connecting...");
  }
}

bool isHomeAssistantReady()
{
  return haConfigured && WiFi.status() == WL_CONNECTED;
}

bool sendDeviceValueToHomeAssistant(const Device &device)
{
  const char *service = serviceForValue(device);

  if (!hasText(service))
  {
    Serial.println("HA command skipped: unsupported device type");
    return false;
  }

  return postService(service, valuePayload(device));
}

bool sendMediaPlaybackToHomeAssistant(const Device &device, bool playing)
{
  String payload = "{\"entity_id\":\"";
  payload += device.entityId;
  payload += "\"}";

  return postService(playing ? "media_player/media_play" : "media_player/media_pause", payload);
}

bool sendMediaVolumeToHomeAssistant(const Device &device)
{
  return postService("media_player/volume_set", valuePayload(device));
}
