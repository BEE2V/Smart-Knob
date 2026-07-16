#include "homeassistant.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>

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
constexpr int HISTORY_SAMPLE_LIMIT = 32;

bool hasText(const char *value)
{
  return value != nullptr && value[0] != '\0';
}

String isoTime(time_t value)
{
  struct tm timeinfo;
  gmtime_r(&value, &timeinfo);

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buffer);
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
    payload += round(device.value);

    if (device.supportsColor)
    {
      payload += ",\"hs_color\":[";
      payload += String(device.hue, 1);
      payload += ",";
      payload += String(device.saturation, 1);
      payload += "]";
    }
    break;
  case DeviceType::Fan:
    payload += ",\"percentage\":";
    payload += round((device.value / device.maxValue) * 100.0f);
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
    String normalizedUnit = unit;
    String asciiUnit;

    for (unsigned int i = 0; i < normalizedUnit.length(); i++)
    {
      char c = normalizedUnit.charAt(i);

      if (c >= 32 && c <= 126)
      {
        asciiUnit += c;
      }
    }

    normalizedUnit = asciiUnit;
    return normalizedUnit;
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

String displayNameForEntity(const char *friendlyName, const char *entityId)
{
  String name = hasText(friendlyName) ? String(friendlyName) : String(entityId);

  const char *prefixes[] = {
      "VM HA ",
      "VMHA ",
      "Home Assistant ",
      "HA "};

  for (const char *prefix : prefixes)
  {
    if (name.startsWith(prefix))
    {
      name.remove(0, strlen(prefix));
      break;
    }
  }

  name.trim();

  if (name.length() == 0)
  {
    return entityId;
  }

  return name;
}

float readDeviceValue(DeviceType type, const char *state, JsonObject attributes)
{
  switch (type)
  {
  case DeviceType::Light:
    if (attributes["brightness"].is<int>())
    {
      return (attributes["brightness"].as<int>() / 255.0f) * 100.0f;
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
    return atof(state);
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

void applyEntityState(Device &device, const char *state, JsonObject attributes)
{
  bool available = !isUnavailableState(state);

  device.unit = unitForEntity(device.type, attributes);
  device.available = available;
  device.state = strcmp(state, "on") == 0 || strcmp(state, "playing") == 0;
  device.value = available ? readDeviceValue(device.type, state, attributes) : 0;
  device.maxValue = maxValueForType(device.type);

  if (device.type == DeviceType::Light)
  {
    device.supportsColor = false;

    JsonArray colorModes = attributes["supported_color_modes"].as<JsonArray>();

    for (JsonVariant mode : colorModes)
    {
      const char *modeText = mode | "";

      if (strcmp(modeText, "hs") == 0 ||
          strcmp(modeText, "rgb") == 0 ||
          strcmp(modeText, "rgbw") == 0 ||
          strcmp(modeText, "rgbww") == 0)
      {
        device.supportsColor = true;
      }
    }

    JsonArray hsColor = attributes["hs_color"].as<JsonArray>();

    if (hsColor.size() >= 2)
    {
      device.hue = hsColor[0].as<float>();
      device.saturation = hsColor[1].as<float>();
    }
  }
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
  filter[0]["attributes"]["supported_color_modes"] = true;
  filter[0]["attributes"]["hs_color"] = true;
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

    Device device;
    device.entityId = entityId;
    device.name = displayNameForEntity(friendlyName, entityId);
    device.area = "Home Assistant";
    device.type = type;
    device.controllable = type == DeviceType::Light || type == DeviceType::Fan || type == DeviceType::Media;
    device.supportsColor = false;
    device.hue = 0;
    device.saturation = 0;
    applyEntityState(device, state, attributes);

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

bool postApi(const char *path, const String &payload)
{
  if (!isHomeAssistantReady())
  {
    Serial.println("HA command skipped: not connected");
    return false;
  }

  HTTPClient http;
  String url = String(HA_BASE_URL) + "/api/" + path;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int status = http.POST(payload);

  Serial.print("HA POST ");
  Serial.print(path);
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
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
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

bool refreshHomeAssistantEntity(Device &device)
{
  if (!isHomeAssistantReady() || device.entityId.length() == 0)
  {
    return false;
  }

  HTTPClient http;
  String url = String(HA_BASE_URL) + "/api/states/" + device.entityId;

  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int status = http.GET();

  if (status != 200)
  {
    Serial.print("HA GET ");
    Serial.print(device.entityId);
    Serial.print(" -> ");
    Serial.println(status);
    http.end();
    return false;
  }

  StaticJsonDocument<256> filter;
  filter["state"] = true;
  filter["attributes"]["brightness"] = true;
  filter["attributes"]["percentage"] = true;
  filter["attributes"]["volume_level"] = true;
  filter["attributes"]["supported_color_modes"] = true;
  filter["attributes"]["hs_color"] = true;
  filter["attributes"]["device_class"] = true;
  filter["attributes"]["unit_of_measurement"] = true;

  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(
      doc,
      http.getStream(),
      DeserializationOption::Filter(filter));

  http.end();

  if (error)
  {
    Serial.print("HA entity parse failed: ");
    Serial.println(error.c_str());
    return false;
  }

  const char *state = doc["state"] | "";
  JsonObject attributes = doc["attributes"];

  applyEntityState(device, state, attributes);

  return true;
}

int fetchHomeAssistantHistory(const Device &device, float *samples, int maxSamples)
{
  if (!isHomeAssistantReady() ||
      device.entityId.length() == 0 ||
      samples == nullptr ||
      maxSamples <= 0)
  {
    return 0;
  }

  HTTPClient http;
  time_t now = time(nullptr);
  String url = String(HA_BASE_URL) + "/api/history/period";

  if (now > 1700000000)
  {
    url += "/";
    url += isoTime(now - (HA_HISTORY_MINUTES * 60));
  }

  url += "?filter_entity_id=";
  url += device.entityId;
  url += "&minimal_response&no_attributes&significant_changes_only";

  if (now > 1700000000)
  {
    url += "&end_time=";
    url += isoTime(now);
  }

  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);

  int status = http.GET();
  int contentLength = http.getSize();

  Serial.print("HA history GET ");
  Serial.print(device.entityId);
  Serial.print(" -> ");
  Serial.print(status);
  Serial.print(" bytes=");
  Serial.print(contentLength);
  Serial.print(" heap=");
  Serial.println(ESP.getFreeHeap());

  if (status != 200)
  {
    Serial.print("HA GET history ");
    Serial.print(device.entityId);
    Serial.print(" -> ");
    Serial.println(status);
    http.end();
    return 0;
  }

  WiFiClient *stream = http.getStreamPtr();
  maxSamples = min(maxSamples, HISTORY_SAMPLE_LIMIT);
  float bucketSum[HISTORY_SAMPLE_LIMIT];
  int bucketCount[HISTORY_SAMPLE_LIMIT];

  for (int i = 0; i < maxSamples; i++)
  {
    bucketSum[i] = 0;
    bucketCount[i] = 0;
  }

  const char *shortKey = "\"s\":\"";
  const char *longKey = "\"state\":\"";
  int shortMatch = 0;
  int longMatch = 0;
  bool readingValue = false;
  char valueBuffer[20];
  int valueLength = 0;
  int bytesRead = 0;
  int availableSamples = 0;
  unsigned long startedAt = millis();

  while ((http.connected() || stream->available()) && millis() - startedAt < 6000)
  {
    while (stream->available())
    {
      char c = stream->read();
      bytesRead++;

      if (readingValue)
      {
        if (c == '"' || valueLength >= (int)sizeof(valueBuffer) - 1)
        {
          valueBuffer[valueLength] = '\0';

          if (isNumericState(valueBuffer))
          {
            int bucket = 0;

            if (contentLength > 0)
            {
              bucket = ((long)bytesRead * maxSamples) / contentLength;
              bucket = constrain(bucket, 0, maxSamples - 1);
            }
            else
            {
              bucket = availableSamples % maxSamples;
            }

            bucketSum[bucket] += atof(valueBuffer);
            bucketCount[bucket]++;
            availableSamples++;
          }

          readingValue = false;
          valueLength = 0;
          shortMatch = 0;
          longMatch = 0;
        }
        else
        {
          valueBuffer[valueLength++] = c;
        }

        continue;
      }

      if (c == shortKey[shortMatch])
      {
        shortMatch++;

        if (shortKey[shortMatch] == '\0')
        {
          readingValue = true;
          valueLength = 0;
        }
      }
      else
      {
        shortMatch = c == shortKey[0] ? 1 : 0;
      }

      if (c == longKey[longMatch])
      {
        longMatch++;

        if (longKey[longMatch] == '\0')
        {
          readingValue = true;
          valueLength = 0;
        }
      }
      else
      {
        longMatch = c == longKey[0] ? 1 : 0;
      }
    }

    delay(1);
  }

  http.end();

  int count = 0;

  for (int i = 0; i < maxSamples; i++)
  {
    if (bucketCount[i] == 0)
    {
      continue;
    }

    samples[count] = bucketSum[i] / bucketCount[i];
    count++;
  }

  Serial.print("HA history samples ");
  Serial.print(device.entityId);
  Serial.print(": ");
  Serial.print(count);
  Serial.print(" from numeric=");
  Serial.print(availableSamples);
  Serial.print(" bytes_read=");
  Serial.print(bytesRead);
  Serial.print(" heap=");
  Serial.println(ESP.getFreeHeap());

  return count;
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

bool sendShortcutEventToHomeAssistant(int shortcutNumber, bool longPress)
{
  String payload = "{\"controller\":\"smart_knob\",\"button\":";
  payload += shortcutNumber;
  payload += ",\"press_type\":\"";
  payload += longPress ? "long" : "short";
  payload += "\"}";

  return postApi("events/esp32_smart_knob_shortcut", payload);
}
