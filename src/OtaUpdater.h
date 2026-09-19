#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#error "FW_VERSION build flag is required"
#endif

#ifndef OTA_REPO
#error "OTA_REPO build flag is required (e.g. owner/repo)"
#endif

namespace OtaUpdater {

bool connectWifi(const char* ssid = nullptr, const char* password = nullptr, uint32_t timeoutMs = 15000);

bool checkAndUpdate();

void run(const char* ssid = nullptr, const char* password = nullptr);

}
