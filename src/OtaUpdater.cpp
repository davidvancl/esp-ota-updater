#include "OtaUpdater.h"

#include <EEPROM.h>
#include <memory>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#else
#error "OtaUpdater supports only ESP8266 and ESP32"
#endif

#ifndef OTA_VERSION_ASSET
#define OTA_VERSION_ASSET "version.txt"
#endif

#ifndef OTA_FIRMWARE_ASSET
#define OTA_FIRMWARE_ASSET "firmware.bin"
#endif

#ifndef OTA_EEPROM_SECRETS_OFFSET
#define OTA_EEPROM_SECRETS_OFFSET 128
#endif

#ifndef OTA_EEPROM_EXTRA_OFFSET
#define OTA_EEPROM_EXTRA_OFFSET 1024
#endif

namespace {

using OtaUpdater::WifiNetwork;

#if defined(ESP8266)
using SecureClient = BearSSL::WiFiClientSecure;
#define OTA_HTTP_UPDATE ESPhttpUpdate
#else
using SecureClient = WiFiClientSecure;
#define OTA_HTTP_UPDATE httpUpdate
#endif

constexpr uint32_t PRIMARY_MAGIC = 0x4F544131;
constexpr uint32_t EXTRA_MAGIC = 0x4F544158;
constexpr size_t MAX_NETWORKS = 4;

struct StoredNetwork {
  char ssid[33];
  char password[65];
};

struct PrimaryRecord {
  uint32_t magic;
  StoredNetwork network;
};

struct ExtraRecord {
  uint32_t magic;
  uint8_t count;
  StoredNetwork networks[MAX_NETWORKS - 1];
};

struct StoredCredentials {
  uint8_t count;
  StoredNetwork networks[MAX_NETWORKS];
};

constexpr size_t EEPROM_WINDOW = OTA_EEPROM_EXTRA_OFFSET + sizeof(ExtraRecord);

void terminate(StoredNetwork& network) {
  network.ssid[sizeof(network.ssid) - 1] = '\0';
  network.password[sizeof(network.password) - 1] = '\0';
}

void toStored(const WifiNetwork& in, StoredNetwork& out) {
  strncpy(out.ssid, in.ssid, sizeof(out.ssid) - 1);
  strncpy(out.password, in.password ? in.password : "", sizeof(out.password) - 1);
}

void readRecords(PrimaryRecord& primary, ExtraRecord& extra) {
  EEPROM.begin(EEPROM_WINDOW);
  EEPROM.get(0, primary);
  EEPROM.get(OTA_EEPROM_EXTRA_OFFSET, extra);
  EEPROM.end();
}

bool loadCredentials(StoredCredentials& out) {
  PrimaryRecord primary;
  ExtraRecord extra;
  readRecords(primary, extra);

  if (primary.magic != PRIMARY_MAGIC) return false;
  terminate(primary.network);
  if (primary.network.ssid[0] == '\0') return false;

  out.count = 1;
  out.networks[0] = primary.network;
  if (extra.magic == EXTRA_MAGIC && extra.count < MAX_NETWORKS) {
    for (uint8_t i = 0; i < extra.count; i++) {
      terminate(extra.networks[i]);
      out.networks[out.count++] = extra.networks[i];
    }
  }
  return true;
}

void saveCredentials(const WifiNetwork* networks, size_t count) {
  count = min(count, MAX_NETWORKS);

  PrimaryRecord nextPrimary;
  memset(&nextPrimary, 0, sizeof(nextPrimary));
  nextPrimary.magic = PRIMARY_MAGIC;
  toStored(networks[0], nextPrimary.network);

  ExtraRecord nextExtra;
  memset(&nextExtra, 0, sizeof(nextExtra));
  nextExtra.magic = EXTRA_MAGIC;
  nextExtra.count = count - 1;
  for (uint8_t i = 0; i < nextExtra.count; i++) toStored(networks[i + 1], nextExtra.networks[i]);

  PrimaryRecord primary;
  ExtraRecord extra;
  readRecords(primary, extra);
  bool primarySame = memcmp(&primary, &nextPrimary, sizeof(primary)) == 0;
  bool extraSame = (count == 1 && extra.magic != EXTRA_MAGIC) ||
                   memcmp(&extra, &nextExtra, sizeof(extra)) == 0;
  if (primarySame && extraSame) return;

  EEPROM.begin(EEPROM_WINDOW);
  EEPROM.put(0, nextPrimary);
  if (!extraSame) EEPROM.put(OTA_EEPROM_EXTRA_OFFSET, nextExtra);
  EEPROM.commit();
  EEPROM.end();
}

constexpr uint32_t SECRETS_MAGIC = 0x4F544153;
constexpr size_t SECRETS_DATA_OFFSET = OTA_EEPROM_SECRETS_OFFSET + sizeof(uint32_t);
constexpr size_t SECRETS_DATA_SIZE = OTA_EEPROM_EXTRA_OFFSET - SECRETS_DATA_OFFSET;

static_assert(OTA_EEPROM_SECRETS_OFFSET >= sizeof(PrimaryRecord), "secrets overlap WiFi credentials");
static_assert(OTA_EEPROM_EXTRA_OFFSET > SECRETS_DATA_OFFSET + 2, "secrets area is too small");

std::unique_ptr<char[]> readSecrets() {
  std::unique_ptr<char[]> data(new char[SECRETS_DATA_SIZE]());
  EEPROM.begin(EEPROM_WINDOW);
  uint32_t magic;
  EEPROM.get(OTA_EEPROM_SECRETS_OFFSET, magic);
  if (magic == SECRETS_MAGIC) {
    for (size_t i = 0; i < SECRETS_DATA_SIZE; i++) data[i] = EEPROM.read(SECRETS_DATA_OFFSET + i);
    data[SECRETS_DATA_SIZE - 1] = '\0';
  }
  EEPROM.end();
  return data;
}

template <typename Fn>
void forEachSecret(const char* data, Fn fn) {
  const char* end = data + SECRETS_DATA_SIZE;
  const char* p = data;
  while (p < end && *p) {
    const char* key = p;
    p += strnlen(p, end - p) + 1;
    if (p >= end) return;
    const char* value = p;
    p += strnlen(p, end - p) + 1;
    if (!fn(key, value)) return;
  }
}

bool waitForConnection(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) return false;
    delay(200);
  }
  return true;
}

bool tryConnect(const WifiNetwork& network, uint32_t timeoutMs) {
  Serial.printf("[OTA] connecting to %s\n", network.ssid);
  WiFi.begin(network.ssid, network.password ? network.password : "");
  if (waitForConnection(timeoutMs)) return true;
#if defined(ESP32)
  WiFi.disconnect();
#endif
  return false;
}

bool isVisible(const char* ssid, int found) {
  for (int i = 0; i < found; i++) {
    if (WiFi.SSID(i) == ssid) return true;
  }
  return false;
}

bool connectAny(const WifiNetwork* networks, size_t count, uint32_t timeoutMs) {
  if (count == 1) return tryConnect(networks[0], timeoutMs);

  int found = WiFi.scanNetworks();
  if (found < 0) found = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i < count; i++) {
      if (isVisible(networks[i].ssid, found) != (pass == 0)) continue;
      if (tryConnect(networks[i], timeoutMs)) {
        WiFi.scanDelete();
        return true;
      }
    }
  }
  WiFi.scanDelete();
  return false;
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
  SecureClient client;
  client.setInsecure();
#if defined(ESP8266)
  client.setBufferSizes(1024, 512);
#endif

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
  if (!ssid) return connectWifi(static_cast<const WifiNetwork*>(nullptr), 0, timeoutMs);
  WifiNetwork network = {ssid, password};
  return connectWifi(&network, 1, timeoutMs);
}

bool connectWifi(const WifiNetwork* networks, size_t count, uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);

  if (networks && count > 0) {
    if (!connectAny(networks, count, timeoutMs)) return false;
    saveCredentials(networks, count);
    return true;
  }

  StoredCredentials saved;
  if (loadCredentials(saved)) {
    WifiNetwork list[MAX_NETWORKS];
    for (uint8_t i = 0; i < saved.count; i++) {
      list[i] = {saved.networks[i].ssid, saved.networks[i].password};
    }
    return connectAny(list, saved.count, timeoutMs);
  }

  WiFi.begin();
  return waitForConnection(timeoutMs);
}

String loadSecret(const char* key) {
  String result;
  std::unique_ptr<char[]> data = readSecrets();
  forEachSecret(data.get(), [&](const char* k, const char* v) {
    if (strcmp(k, key) != 0) return true;
    result = v;
    return false;
  });
  return result;
}

bool saveSecret(const char* key, const char* value) {
  if (!key || !*key) return false;
  if (!value) value = "";

  std::unique_ptr<char[]> current = readSecrets();
  std::unique_ptr<char[]> next(new char[SECRETS_DATA_SIZE]());
  size_t length = 0;
  bool unchanged = false;
  bool fits = true;

  auto append = [&](const char* k, const char* v) {
    size_t needed = strlen(k) + 1 + strlen(v) + 1;
    if (length + needed >= SECRETS_DATA_SIZE) return fits = false;
    memcpy(&next[length], k, strlen(k) + 1);
    memcpy(&next[length + strlen(k) + 1], v, strlen(v) + 1);
    length += needed;
    return true;
  };

  forEachSecret(current.get(), [&](const char* k, const char* v) {
    if (strcmp(k, key) == 0) {
      unchanged = strcmp(v, value) == 0;
      return true;
    }
    return append(k, v);
  });
  if (unchanged) return true;
  if (!append(key, value)) {
    Serial.printf("[OTA] no room for secret %s\n", key);
    return false;
  }

  EEPROM.begin(EEPROM_WINDOW);
  EEPROM.put(OTA_EEPROM_SECRETS_OFFSET, SECRETS_MAGIC);
  for (size_t i = 0; i < SECRETS_DATA_SIZE; i++) EEPROM.write(SECRETS_DATA_OFFSET + i, next[i]);
  EEPROM.commit();
  EEPROM.end();
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

  Serial.printf("[OTA] updating (free heap %u)\n", (unsigned)ESP.getFreeHeap());
  SecureClient client;
  client.setInsecure();
#if defined(ESP8266)
  client.setBufferSizes(16384, 512);
#endif

  OTA_HTTP_UPDATE.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  OTA_HTTP_UPDATE.rebootOnUpdate(true);
  t_httpUpdate_return result = OTA_HTTP_UPDATE.update(client, assetUrl(OTA_FIRMWARE_ASSET));

  if (result == HTTP_UPDATE_FAILED) {
    Serial.printf("[OTA] failed: %s\n", OTA_HTTP_UPDATE.getLastErrorString().c_str());
  }
  return result == HTTP_UPDATE_OK;
}

void run(const char* ssid, const char* password) {
  if (!ssid) {
    run(static_cast<const WifiNetwork*>(nullptr), 0);
    return;
  }
  WifiNetwork network = {ssid, password};
  run(&network, 1);
}

void run(const WifiNetwork* networks, size_t count) {
  if (!connectWifi(networks, count)) {
    Serial.println("[OTA] no wifi, skipping");
    return;
  }
  checkAndUpdate();
}

}
