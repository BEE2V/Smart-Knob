#include "homeassistant.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config.h"
#include "devices.h"

namespace
{
bool wifiStarted = false;
bool haConfigured = false;
bool stateSyncDone = false;
bool dynamicDeviceListLoaded = false;
unsigned long lastStatusPrint = 0;
unsigned long lastStateSyncAttempt = 0;

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
  case DeviceType::BinarySensor:
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
  case DeviceType::BinarySensor:
    break;
  }

  payload += "}";
  return payload;
}

bool isUnavailableState(const char *state)
{
  return strcmp(state, "unavailable") == 0 || strcmp(state, "unknown") == 0;
}

bool isNumericState(const char *state)
{
  if (!hasText(state))
  {
    return false;
  }

  char *end = nullptr;
  strtod(state, &end);
  return end != state && *end == '\0';
}

DeviceType typeFromEntityId(const char *entityId)
{
  String id = entityId;

  if (id.startsWith("light."))
  {
    return DeviceType::Light;
  }

  if (id.startsWith("fan."))
  {
    return DeviceType::Fan;
  }

  if (id.startsWith("media_player."))
  {
    return DeviceType::Media;
  }

  if (id.startsWith("binary_sensor."))
  {
    return DeviceType::BinarySensor;
  }

  return DeviceType::Sensor;
}

bool isUsefulSensor(const char *entityId, const char *state, JsonObject attributes)
{
  String id = entityId;
  const char *deviceClass = attributes["device_class"] | "";
  const char *unit = attributes["unit_of_measurement"] | "";

  if (id.startsWith("sensor."))
  {
    if (!isNumericState(state))
    {
      return false;
    }

    return hasText(unit) ||
           strcmp(deviceClass, "temperature") == 0 ||
           strcmp(deviceClass, "humidity") == 0 ||
           strcmp(deviceClass, "illuminance") == 0;
  }

  if (id.startsWith("binary_sensor."))
  {
    return strcmp(deviceClass, "motion") == 0 ||
           strcmp(deviceClass, "occupancy") == 0 ||
           strcmp(deviceClass, "presence") == 0;
  }

  return false;
}

bool isSupportedEntity(const char *entityId, const char *state, JsonObject attributes)
{
  String id = entityId;

  if (id.startsWith("light.") ||
      id.startsWith("fan.") ||
      id.startsWith("media_player."))
  {
    return true;
  }

  return isUsefulSensor(entityId, state, attributes);
}

String unitForEntity(DeviceType type, JsonObject attributes)
{
  const char *unit = attributes["unit_of_measurement"] | "";
  const char *deviceClass = attributes["device_class"] | "";

  if (hasText(unit))
  {
    return unit;
  }

  if (type == DeviceType::Light || type == DeviceType::Media)
  {
    return "%";
  }

  if (type == DeviceType::Fan)
  {
    return "%";
  }

  if (type == DeviceType::Sensor && strcmp(deviceClass, "temperature") == 0)
  {
    return "C";
  }

  if (type == DeviceType::Sensor && strcmp(deviceClass, "humidity") == 0)
  {
    return "%";
  }

  if (type == DeviceType::Sensor && strcmp(deviceClass, "illuminance") == 0)
  {
    return "lx";
  }

  return "";
}

int readDeviceValue(DeviceType type, const char *state, JsonObject attributes)
{
  switch (type)
  {
  case DeviceType::Light:
    if (attributes["brightness"].is<int>())
    {
      return map(attributes["brightness"].as<int>(), 0, 255, 0, 100);
    }
    return strcmp(state, "on") == 0 ? 100 : 0;
  case DeviceType::Fan:
    if (attributes["percentage"].is<int>())
    {
      return attributes["percentage"].as<int>();
    }
    return strcmp(state, "on") == 0 ? 100 : 0;
  case DeviceType::Media:
    if (attributes["volume_level"].is<float>())
    {
      return round(attributes["volume_level"].as<float>() * 100.0f);
    }
    return 0;
  case DeviceType::Sensor:
    return round(atof(state));
  case DeviceType::BinarySensor:
    return strcmp(state, "on") == 0 ? 1 : 0;
  }

  return 0;
}

int maxValueForType(DeviceType type)
{
  switch (type)
  {
  case DeviceType::Fan:
  case DeviceType::Light:
  case DeviceType::Media:
    return 100;
  case DeviceType::Sensor:
    return 100;
  case DeviceType::BinarySensor:
    return 1;
  }

  return 100;
}

bool syncStatesFromHomeAssistant()
{
  if (!isHomeAssistantReady())
  {
    return false;
  }

  HTTPClient http;
  String url = String(HA_BASE_URL) + "/api/states";

  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int status = http.GET();

  Serial.print("HA GET states -> ");
  Serial.println(status);

  if (status != 200)
  {
    http.end();
    return false;
  }

  StaticJsonDocument<256> filter;
  filter[0]["entity_id"] = true;
  filter[0]["state"] = true;
  filter[0]["attributes"]["friendly_name"] = true;
  filter[0]["attributes"]["brightness"] = true;
  filter[0]["attributes"]["percentage"] = true;
  filter[0]["attributes"]["volume_level"] = true;
  filter[0]["attributes"]["device_class"] = true;
  filter[0]["attributes"]["unit_of_measurement"] = true;

  DynamicJsonDocument doc(32768);
  DeserializationError error = deserializeJson(
      doc,
      http.getStream(),
      DeserializationOption::Filter(filter));

  http.end();

  if (error)
  {
    Serial.print("HA states parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  clearDevices();

  for (JsonObject entity : doc.as<JsonArray>())
  {
    const char *entityId = entity["entity_id"] | "";
    const char *state = entity["state"] | "";

    JsonObject attributes = entity["attributes"];

    if (!isSupportedEntity(entityId, state, attributes))
    {
      continue;
    }

    DeviceType type = typeFromEntityId(entityId);
    const char *friendlyName = attributes["friendly_name"] | entityId;
    bool available = !isUnavailableState(state);

    Device device;
    device.entityId = entityId;
    device.name = friendlyName;
    device.area = "Home Assistant";
    device.unit = unitForEntity(type, attributes);
    device.type = type;
    device.controllable = type == DeviceType::Light || type == DeviceType::Fan || type == DeviceType::Media;
    device.available = available;
    device.state = strcmp(state, "on") == 0 || strcmp(state, "playing") == 0;
    device.value = available ? readDeviceValue(type, state, attributes) : 0;
    device.maxValue = maxValueForType(type);

    addDevice(device);
  }

  if (deviceCount == 0)
  {
    resetDevicesToFallback();
    dynamicDeviceListLoaded = false;
    Serial.println("HA states contained no supported entities; using fallback devices");
    return false;
  }

  dynamicDeviceListLoaded = true;
  Serial.print("HA devices loaded: ");
  Serial.println(deviceCount);
  return true;
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
  resetDevicesToFallback();
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

    if (!stateSyncDone && millis() - lastStateSyncAttempt > 2000)
    {
      lastStateSyncAttempt = millis();
      stateSyncDone = syncStatesFromHomeAssistant();
    }
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

bool hasHomeAssistantDeviceList()
{
  return dynamicDeviceListLoaded;
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
