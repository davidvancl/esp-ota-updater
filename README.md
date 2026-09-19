# esp-ota-updater

Knihovna pro ESP8266 (PlatformIO, Arduino). Při startu zkontroluje nejnovější verzi v GitHub Releases a případně se sama aktualizuje.

## Přidání do projektu

### 1. `platformio.ini`

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
monitor_speed = 115200
custom_version = 0.0.1
custom_ota_repo = uzivatel/nazev-repa
build_flags =
	-DFW_VERSION=\"${this.custom_version}\"
	-DOTA_REPO=\"${this.custom_ota_repo}\"
lib_deps =
	https://github.com/davidvancl/esp-ota-updater.git#v1.0.0
```

- `custom_version` je verze tvého firmwaru.
- `custom_ota_repo` je tvůj projekt (`uzivatel/repo`), ve kterém se vydávají releasy.

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
  Serial.print("Verze firmwaru: ");
  Serial.println(FW_VERSION);

  OtaUpdater::run(WIFI_CREDENTIALS);
}

void loop() {
}
```

`OtaUpdater::run()` volej co nejdřív v `setup()`.

### 3. `src/secrets.h` (WiFi údaje)

```cpp
#define SECRET_SSID "nazev-wifi"
#define SECRET_PASS "heslo-wifi"
```

Přidej do `.gitignore`:

```
src/secrets.h
```

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

### 5. První nahrání

Firmware nahraj přes USB (`pio run -t upload`). Dál se zařízení aktualizuje samo.

## Vydání nové verze

1. V `platformio.ini` zvyš `custom_version` (např. `0.0.1` → `0.0.2`).
2. Commitni a pushni do `main`.
3. Workflow vytvoří release se soubory `firmware.bin` a `version.txt`.
4. Po restartu zařízení se nová verze nainstaluje.
