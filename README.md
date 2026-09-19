# esp-ota-updater

Kontrola a instalace nové verze firmwaru z GitHub Releases při startu ESP8266.

## Použití v projektu

`platformio.ini`:

```ini
[env:nodemcuv2]
custom_version = 0.0.1
custom_ota_repo = owner/repo
build_flags =
	-DFW_VERSION=\"${this.custom_version}\"
	-DOTA_REPO=\"${this.custom_ota_repo}\"
lib_deps =
	https://github.com/davidvancl/esp-ota-updater.git#v1.0.0
```

Kód:

```cpp
#include <OtaUpdater.h>

void setup() {
  Serial.begin(115200);
  OtaUpdater::run();
}
```

`run()` bez argumentů použije WiFi údaje uložené ve flash. Pro první uložení
zavolejte `OtaUpdater::run(ssid, pass)` v lokálním buildu.

`.github/workflows/release.yml` v projektu:

```yaml
name: Release
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

Nová verze se vydá zvýšením `custom_version` a pushem do `main`.

## Nepovinné build flagy

- `OTA_VERSION_ASSET` (výchozí `version.txt`)
- `OTA_FIRMWARE_ASSET` (výchozí `firmware.bin`)

## Omezení

- Repozitář projektu musí být veřejný.
- TLS bez ověření certifikátu (`setInsecure()`).
