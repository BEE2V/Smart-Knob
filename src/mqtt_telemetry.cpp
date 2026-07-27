#include "mqtt_telemetry.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "battery.h"
#include "secrets.h"

namespace
{
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t MQTT_PUBLISH_INTERVAL_MS = 30000;
constexpr const char *FIRMWARE_VERSION = "0.2.0";

WiFiClient mqttNetworkClient;
PubSubClient mqttClient(mqttNetworkClient);

String deviceIdentifier;
String stateTopic;
String availabilityTopic;
uint32_t lastMqttConnectAttempt = 0;
uint32_t lastTelemetryPublish = 0;
bool discoveryRequested = false;

String compactMacAddress()
{
  const uint64_t chipId = ESP.getEfuseMac();
  char id[13];
  snprintf(id, sizeof(id), "%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));
  return String(id);
}

void mqttMessageReceived(char *topic, byte *payload, unsigned int length)
{
  if (String(topic) != "homeassistant/status")
  {
    return;
  }

  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; ++i)
  {
    message += static_cast<char>(payload[i]);
  }

  if (message == "online")
  {
    discoveryRequested = true;
  }
}

bool publishSensorDiscovery(const char *objectId,
                            const char *name,
                            const char *valueKey,
                            const char *deviceClass,
                            const char *unit,
                            const char *stateClass,
                            bool diagnostic,
                            int precision)
{
  DynamicJsonDocument doc(1024);
  doc["name"] = name;
  doc["unique_id"] = deviceIdentifier + "_" + objectId;
  doc["state_topic"] = stateTopic;
  doc["value_template"] = String("{{ value_json.") + valueKey + " }}";
  doc["availability_topic"] = availabilityTopic;
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  if (deviceClass != nullptr && deviceClass[0] != '\0')
  {
    doc["device_class"] = deviceClass;
  }
  if (unit != nullptr && unit[0] != '\0')
  {
    doc["unit_of_measurement"] = unit;
  }
  if (stateClass != nullptr && stateClass[0] != '\0')
  {
    doc["state_class"] = stateClass;
  }
  if (diagnostic)
  {
    doc["entity_category"] = "diagnostic";
  }
  if (precision >= 0)
  {
    doc["suggested_display_precision"] = precision;
  }

  JsonObject device = doc.createNestedObject("device");
  JsonArray identifiers = device.createNestedArray("identifiers");
  identifiers.add(deviceIdentifier);
  JsonArray connections = device.createNestedArray("connections");
  JsonArray macConnection = connections.createNestedArray();
  macConnection.add("mac");
  macConnection.add(WiFi.macAddress());
  device["name"] = "Smart Knob";
  device["manufacturer"] = "DIY";
  device["model"] = "ESP32-S3 Smart Knob";
  device["sw_version"] = FIRMWARE_VERSION;

  JsonObject origin = doc.createNestedObject("origin");
  origin["name"] = "Smart Knob firmware";
  origin["sw_version"] = FIRMWARE_VERSION;

  String payload;
  serializeJson(doc, payload);
  String discoveryTopic = "homeassistant/sensor/" + deviceIdentifier + "_" + objectId + "/config";
  return mqttClient.publish(discoveryTopic.c_str(), payload.c_str(), true);
}

void publishDiscovery()
{
  bool success = true;
  success &= publishSensorDiscovery("battery", "Battery", "battery_percentage",
                                    "battery", "%", "measurement", false, 0);
  success &= publishSensorDiscovery("battery_voltage", "Battery voltage", "battery_voltage",
                                    "voltage", "V", "measurement", false, 2);
  success &= publishSensorDiscovery("sense_voltage", "Battery sense voltage", "sense_voltage",
                                    "voltage", "V", "measurement", true, 3);
  success &= publishSensorDiscovery("wifi_signal", "Wi-Fi signal", "wifi_rssi",
                                    "signal_strength", "dBm", "measurement", true, 0);
  success &= publishSensorDiscovery("uptime", "Uptime", "uptime",
                                    "duration", "s", "total_increasing", true, 0);

  Serial.println(success ? "MQTT discovery published" : "MQTT discovery publish failed");
}

void publishTelemetry()
{
  if (!mqttClient.connected() || !hasBatteryReading())
  {
    return;
  }

  StaticJsonDocument<256> doc;
  doc["battery_percentage"] = getBatteryPercentage();
  doc["battery_voltage"] = serialized(String(getBatteryVoltage(), 3));
  doc["sense_voltage"] = serialized(String(getBatterySenseVoltage(), 3));
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000UL;

  String payload;
  serializeJson(doc, payload);

  if (mqttClient.publish(stateTopic.c_str(), payload.c_str(), true))
  {
    Serial.print("MQTT telemetry: ");
    Serial.println(payload);
  }
  else
  {
    Serial.println("MQTT telemetry publish failed");
  }
}

void connectMqtt()
{
  const uint32_t now = millis();
  if (now - lastMqttConnectAttempt < MQTT_RECONNECT_INTERVAL_MS)
  {
    return;
  }
  lastMqttConnectAttempt = now;

  Serial.print("MQTT connecting to ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  if (!mqttClient.connect(deviceIdentifier.c_str(),
                          MQTT_USERNAME,
                          MQTT_PASSWORD,
                          availabilityTopic.c_str(),
                          0,
                          true,
                          "offline"))
  {
    Serial.print("MQTT connection failed, state=");
    Serial.println(mqttClient.state());
    return;
  }

  Serial.println("MQTT connected");
  mqttClient.subscribe("homeassistant/status");
  mqttClient.publish(availabilityTopic.c_str(), "online", true);
  publishDiscovery();
  publishTelemetry();
  lastTelemetryPublish = now;
}
} // namespace

void initMqttTelemetry()
{
  deviceIdentifier = "smart_knob_" + compactMacAddress();
  stateTopic = "smartknob/" + deviceIdentifier + "/state";
  availabilityTopic = "smartknob/" + deviceIdentifier + "/availability";

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttMessageReceived);
  mqttClient.setBufferSize(1400);
  mqttClient.setKeepAlive(30);

  // Allow the first connection attempt immediately after Wi-Fi comes online.
  lastMqttConnectAttempt = millis() - MQTT_RECONNECT_INTERVAL_MS;
}

void updateMqttTelemetry()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (!mqttClient.connected())
  {
    connectMqtt();
    return;
  }

  mqttClient.loop();

  if (discoveryRequested)
  {
    discoveryRequested = false;
    publishDiscovery();
    mqttClient.publish(availabilityTopic.c_str(), "online", true);
  }

  const uint32_t now = millis();
  if (now - lastTelemetryPublish >= MQTT_PUBLISH_INTERVAL_MS)
  {
    lastTelemetryPublish = now;
    publishTelemetry();
  }
}
