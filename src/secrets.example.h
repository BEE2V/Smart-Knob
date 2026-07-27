#pragma once

// Copy this file to secrets.h and fill in the MQTT device credentials.
constexpr const char *MQTT_HOST = "192.168.8.104";
constexpr uint16_t MQTT_PORT = 1883;
constexpr const char *MQTT_USERNAME = "smartknob_mqtt";
constexpr const char *MQTT_PASSWORD = "replace_with_your_password";

// Use a different password from Wi-Fi and MQTT.
constexpr const char *OTA_HOST = "192.168.8.105";
constexpr const char *OTA_PASSWORD = "replace_with_a_strong_ota_password";
