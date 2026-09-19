#include "OtaUpdater.h"

#include <EEPROM.h>
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

constexpr uint32_t CREDENTIALS_MAGIC = 0x4F544131;  // "OTA1"

struct WifiCredentials {
  uint32_t magic;
  char ssid[33];
  char password[65];
};

bool loadCredentials(WifiCredentials& out) {
  EEPROM.begin(sizeof(WifiCredentials));
  EEPROM.get(0, out);
  EEPROM.end();
  if (out.magic != CREDENTIALS_MAGIC) return false;
  out.ssid[sizeof(out.ssid) - 1] = '\0';
  out.password[sizeof(out.password) - 1] = '\0';
  return out.ssid[0] != '\0';
}

void saveCredentials(const char* ssid, const char* password) {
  WifiCredentials current;
  if (loadCredentials(current) && strcmp(current.ssid, ssid) == 0 &&
      strcmp(current.password, password) == 0) {
    return;
  }

  WifiCredentials creds = {};
  creds.magic = CREDENTIALS_MAGIC;
  strncpy(creds.ssid, ssid, sizeof(creds.ssid) - 1);
  strncpy(creds.password, password, sizeof(creds.password) - 1);

  EEPROM.begin(sizeof(WifiCredentials));
  EEPROM.put(0, creds);
  EEPROM.commit();
  EEPROM.end();
}

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

  WifiCredentials saved;
  bool useSaved = false;
  if (ssid) {
    WiFi.begin(ssid, password ? password : "");
  } else if (loadCredentials(saved)) {
    useSaved = true;
    WiFi.begin(saved.ssid, saved.password);
  } else {
    WiFi.begin();
  }

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) return false;
    delay(200);
  }

  if (ssid && !useSaved) saveCredentials(ssid, password ? password : "");
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

  Serial.printf("[OTA] updating (free heap %u)\n", ESP.getFreeHeap());
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  // Stahování jde přes CDN GitHubu, který neumí zmenšené TLS záznamy;
  // s malým vstupním bufferem se spojení přeruší.
  client.setBufferSizes(16384, 512);

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
