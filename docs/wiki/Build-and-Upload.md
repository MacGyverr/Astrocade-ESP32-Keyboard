# Build and Upload

## Requirements

- ESP32-S3-WROOM-1-N8R2 (8 MB flash, 2 MB PSRAM), or classic ESP32 4 MB.
- VS Code with the PlatformIO IDE extension.
- USB data cable, internet for the first build, and access to the board's serial port.
- On Windows, a CH343 driver may be necessary. On Linux, grant the normal serial
  port/udev permissions rather than running the editor as root.

## First upload

1. Clone the repository, or download and extract its ZIP.
2. Open the folder containing `platformio.ini` in VS Code. Do not open only `src`.
3. Let PlatformIO initialize. No separate Arduino libraries or driver patches
   need to be installed.
4. Connect the **COM/UART/CH343** USB connector to the computer. The other
   connector is for the USB keyboard in host mode.
5. In PlatformIO Project Tasks, choose environment
   `Astrocade-Keyboard-esp32-s3-usb-ble-wifi`, then **General > Upload**.
6. Open Monitor at **115200 baud**, then tap RESET to see all startup messages.

Upload automatically builds first. The first build downloads the compiler,
framework, and components and can take several minutes. Later builds reuse them.
If upload cannot connect, close other serial monitors. Check the cable and port
before trying the board's BOOT/RESET download sequence.

## Firmware version

Edit the existing flag in `[env]`, keeping the quotes:

```ini
'-DASTROCADE_FIRMWARE_VERSION="1.0.0"'
```

`tools/build_version.py` sets the ESP-IDF application version from this flag,
so serial output, OTA metadata, and the web page agree. Use a semantic version
such as `1.0.1` or `1.1.0-beta.1`. Do not define the flag twice.

## Command line

Three environments share the source. Select the one matching your hardware:

| Environment | Inputs |
|---|---|
| `Astrocade-Keyboard-esp32-s3-usb-ble-wifi` | USB + BLE + Wi-Fi (default) |
| `Astrocade-Keyboard-esp32-s3-ble-wifi` | S3 N8R2 BLE + Wi-Fi; USB host excluded |
| `Astrocade-Keyboard-esp32-4mb-ble-wifi` | Classic ESP32 4 MB BLE + Wi-Fi; no USB host or RGB |

The two S3 profiles target **N8R2 (8 MB flash, 2 MB Quad PSRAM)** and share
GPIO wiring. Other S3 memory variants require matching memory configuration;
do not select N8R2 just because the chip is an S3. The classic profile targets
ESP32-WROOM-32-family 4 MB boards without PSRAM and has a separate GPIO map.
See [hardware](Hardware.md) for photos of the four user-tested board models.

RGB is disabled on non-N8R2 builds. Classic text submissions are limited to
8 KiB to fit internal RAM; N8R2 permits 256 KiB using PSRAM. The app has two
OTA slots: 0x280000 bytes each on S3, 0x1D0000 bytes each on classic ESP32.

The 4 MB classic profile can also use a compatible classic board with larger
flash, but uses only the first 4 MB. The web page shows physical flash separately
from the selected build and OTA capacity; it does not automatically enlarge partitions.

Select the BLE/Wi-Fi environment under PlatformIO Project Tasks when the board
has no exposed OTG connector. Upload still uses the COM/UART connector. No OTG
keyboard-power bridge is required when you are not using a USB keyboard.

Run these in a PlatformIO terminal at the project root:

```sh
pio run
pio run -t upload
pio device monitor -b 115200
```

To upload the BLE + Wi-Fi version explicitly:

```sh
pio run -e Astrocade-Keyboard-esp32-s3-ble-wifi -t upload
```

For classic ESP32 use `pio run -e Astrocade-Keyboard-esp32-4mb-ble-wifi -t upload`.
Its OTA image is `.pio/build/Astrocade-Keyboard-esp32-4mb-ble-wifi/firmware.bin`.

An unqualified `pio run` or Upload uses the full build. Change
`default_envs` in `platformio.ini` to make BLE/Wi-Fi your personal default.
Its OTA image is `.pio/build/Astrocade-Keyboard-esp32-s3-ble-wifi/firmware.bin`.

Use `pio run -t upload --upload-port COM3` only if COM3 is your board's actual
port; Linux/macOS names differ. The project intentionally does not hard-code it.

Do not erase flash during routine updates: saved Wi-Fi credentials and BLE
bonds normally survive uploading. Do not change the partition table casually.

The generated application is
`.pio/build/Astrocade-Keyboard-esp32-s3-usb-ble-wifi/firmware.bin`.
Use it for OTA after the initial USB upload, not for a blank board by itself.

![terminal example](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/terminal-example.png)
