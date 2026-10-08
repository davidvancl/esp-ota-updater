#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#error "FW_VERSION build flag is required"
#endif

#ifndef OTA_REPO
#error "OTA_REPO build flag is required (e.g. owner/repo)"
#endif

namespace OtaUpdater {

struct WifiNetwork {
  const char* ssid;
  const char* password;
};

bool connectWifi(const char* ssid = nullptr, const char* password = nullptr, uint32_t timeoutMs = 15000);

bool connectWifi(const WifiNetwork* networks, size_t count, uint32_t timeoutMs = 15000);

// Small key/value store in EEPROM for tokens and other secrets (bytes 128-1023 in total).
// Writes only when the value changed.
bool saveSecret(const char* key, const char* value);

// Returns an empty string if the key was never saved.
String loadSecret(const char* key);

bool checkAndUpdate();

void run(const char* ssid = nullptr, const char* password = nullptr);

void run(const WifiNetwork* networks, size_t count);

}
