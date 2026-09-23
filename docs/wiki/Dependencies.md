# Dependencies and Maintenance

This is **PlatformIO + ESP-IDF**, not an Arduino-framework project.
PlatformIO invokes ESP-IDF's build/component manager automatically.

| File/folder | Role |
|---|---|---|
| `platformio.ini` | Board, pinned platform, flags, upload settings | 
| `src/idf_component.yml` | Direct ESP-IDF component requirements | 
| `dependencies.lock`, `dependencies.esp32.lock` | Exact S3/classic versions and registry hashes | 
| `sdkconfig.defaults`, `sdkconfig.defaults.esp32s3`, `sdkconfig.defaults.esp32` | Common and target-specific framework configuration | 


## Stock components

| Component | Locked version | Used for |
|---|---|---|
| `espressif/usb_host_hid` | 1.2.1 | USB HID keyboard host |
| `espressif/usb` | 1.5.0 | USB host dependency pulled by HID |
| `espressif/mdns` | 1.13.1 | `astrocadekeyboard.local` discovery |
| `espressif/cjson` | 1.7.19 | JSON web API |

NimBLE, Wi-Fi, HTTP, NVS, FreeRTOS, OTA, and the HID report-map parser come
from the stock ESP-IDF framework. Project code in `src/ble_transport.c` uses
public NimBLE APIs; it is not a modified factory driver.

The managed component files were compared against a clean build's downloads
and matched. The project was also built from a temporary copy with no
`managed_components`, `.pio`, or generated sdkconfig. No private local patches
are required. First-build downloads need internet access.

All profiles share the dependency manifest. Its target rule includes USB only
on S3; classic ESP32 resolves without USB components. CMake selects a separate
classic lockfile so switching chips does not overwrite the S3 dependency lock.
USB archives may still be built for the S3 BLE/Wi-Fi profile, but USB host/HID
code is not linked into that firmware and creates no USB tasks.

## Why not lib_deps?

These are ESP-IDF components with CMake configuration and transitive component
dependencies. Their dependency declaration is `idf_component.yml`, with
the target lockfile recording the result. `lib_deps` is not a drop-in replacement
for that integration. Keep a single source of dependency truth.


## Updating

Change the platform/component version deliberately, build with freshly generated
configuration, and review the updated lockfile. Test USB, BLE enrollment and
reconnect, text pacing, captive setup, and OTA on hardware before releasing.
Do not upgrade dependencies as an incidental part of a mapping change.

References: [PlatformIO ESP-IDF](https://docs.platformio.org/en/latest/frameworks/espidf.html),
[ESP-IDF component manifest](https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/manifest_file.html).
