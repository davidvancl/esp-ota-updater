# esp-ota-updater

Library for ESP8266 and ESP32 (PlatformIO, Arduino). On startup it checks GitHub Releases for the latest version and updates itself if a newer one is available.

## Adding it to a project

### 1. `platformio.ini`

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200
custom_version = 0.0.1
custom_ota_repo = user/repo-name
build_flags =
	-DFW_VERSION=\"${this.custom_version}\"
	-DOTA_REPO=\"${this.custom_ota_repo}\"
lib_deps =
	https://github.com/davidvancl/esp-ota-updater.git#v1.1.0
```

- `custom_version` is the version of your firmware.
- `custom_ota_repo` is your project (`user/repo`) where the releases are published.

### 2. `src/main.cpp`

```cpp
#include <Arduino.h>
#include <OtaUpdater.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define WIFI_CREDENTIALS SECRET_SSID, SECRET_PASS
#else
#define WIFI_CREDENTIALS nullptr, nullptr
#endif

void setup() {
  Serial.begin(115200);
  Serial.print("Firmware version: ");
  Serial.println(FW_VERSION);

  OtaUpdater::run(WIFI_CREDENTIALS);
}

void loop() {
}
```

Call `OtaUpdater::run()` as early as possible in `setup()`.

### 3. `src/secrets.h` (WiFi credentials)

```cpp
#define SECRET_SSID "wifi-name"
#define SECRET_PASS "wifi-password"
```

Add it to `.gitignore`:

```
src/secrets.h
```

The credentials are saved to EEPROM after the first successful connection. Later builds without `secrets.h` (e.g. the ones built by CI) reuse the saved credentials.

#### Multiple networks

Pass a list of networks instead of a single SSID (up to 4 are saved to EEPROM). Networks found by a scan are tried first, in the given order.

```cpp
static const OtaUpdater::WifiNetwork WIFI_NETWORKS[] = {
  {SECRET_SSID, SECRET_PASS},
  {SECRET_SSID_ALT, SECRET_PASS_ALT},
};

OtaUpdater::run(WIFI_NETWORKS, sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]));
```

#### Tokens and other secrets

Other secrets (API tokens etc.) can be stored the same way: save them when `secrets.h` exists, read them in every build.

```cpp
#ifdef HAS_SECRETS
  OtaUpdater::saveSecret("gitlab", SECRET_GITLAB_TOKEN);
#endif

String token = OtaUpdater::loadSecret("gitlab");  // "" if never saved
```

`saveSecret()` writes to EEPROM only when the value changed. All keys and values share about 890 bytes.

#### EEPROM layout

- Bytes `0–127`: the first network (same as v1.0.x).
- Bytes `128–1023`: secrets.
- Bytes `1024–1323`: the additional networks, only when more than one is configured.

The offsets can be changed with `-DOTA_EEPROM_SECRETS_OFFSET=...` and `-DOTA_EEPROM_EXTRA_OFFSET=...`. If your app also uses `EEPROM` directly, keep its data after byte `1324` and always call `EEPROM.begin()` with a size covering the library's area too, otherwise `commit()` erases it.

### 4. `.github/workflows/release.yml`

```yaml
name: Release firmware

on:
  push:
    branches: [main]
    paths: [platformio.ini]

jobs:
  release:
    uses: davidvancl/esp-ota-updater/.github/workflows/release.yml@v1
    permissions:
      contents: write
```

### 5. First upload

Flash the firmware over USB (`pio run -t upload`). After that the device updates itself.

## Releasing a new version

1. Bump `custom_version` in `platformio.ini` (e.g. `0.0.1` → `0.0.2`).
2. Commit and push to `main`.
3. The workflow creates a release with the `firmware.bin` and `version.txt` files.
4. After the device restarts, the new version gets installed.
