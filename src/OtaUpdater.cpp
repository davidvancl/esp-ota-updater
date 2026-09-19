#include "OtaUpdater.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>

#ifndef OTA_VERSION_ASSET
#define OTA_VERSION_ASSET "version.txt"
#endif

#ifndef OTA_FIRMWARE_ASSET
#define OTA_FIRMWARE_ASSET "firmware.bin"
#endif

namespace {

String assetUrl(const char* asset) {
  return String("https://github.com/") + OTA_REPO + "/releases/latest/download/" + asset;
}

bool parseVersion(const String& text, int out[3]) {
  out[0] = out[1] = out[2] = 0;
  return sscanf(text.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]) >= 1;
}

bool isNewer(const String& remote, const String& local) {
  int r[3], l[3];
  if (!parseVersion(remote, r) || !parseVersion(local, l)) return false;
  for (int i = 0; i < 3; i++) {
    if (r[i] != l[i]) return r[i] > l[i];
  }
  return false;
}

bool fetchRemoteVersion(String& version) {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  if (!http.begin(client, assetUrl(OTA_VERSION_ASSET))) return false;

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    version = http.getString();
    version.trim();
  }
  http.end();
  return code == HTTP_CODE_OK;
}

}

namespace OtaUpdater {

bool connectWifi(const char* ssid, const char* password, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  if (ssid) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin();
  }
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) return false;
    delay(200);
  }
  return true;
}

bool checkAndUpdate() {
  Serial.printf("[OTA] current version %s\n", FW_VERSION);

  String remote;
  if (!fetchRemoteVersion(remote)) {
    Serial.println("[OTA] version check failed");
    return false;
  }
  Serial.printf("[OTA] latest version %s\n", remote.c_str());

  if (!isNewer(remote, FW_VERSION)) return false;

  Serial.println("[OTA] updating");
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(1024, 512);

  ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return result = ESPhttpUpdate.update(client, assetUrl(OTA_FIRMWARE_ASSET));

  if (result == HTTP_UPDATE_FAILED) {
    Serial.printf("[OTA] failed: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
  }
  return result == HTTP_UPDATE_OK;
}

void run(const char* ssid, const char* password) {
  if (!connectWifi(ssid, password)) {
    Serial.println("[OTA] no wifi, skipping");
    return;
  }
  checkAndUpdate();
}

}
