# Astrocade USB + BLE BASIC Keyboard Adapter (untested WIP)

This PlatformIO / ESP-IDF project supports ESP32-S3-WROOM-1-N8R2 boards and
classic ESP32-WROOM-32-family 4 MB boards. It drives a CD74HCT22106E
crosspoint switch to emulate the Bally
Astrocade 24-key keypad while accepting:

- a wired USB HID Boot keyboard (full S3 build only);
- a Bluetooth Low Energy HID keyboard;
- BASIC text pasted into the adapter's web page.

This was almostly completely written in a day mostly using ChatGPT Astra.

Includes Wi-Fi setup, BASIC text validation and paced transfer, and wireless
firmware updates. BASIC is the current overlay; more overlays are planned.

**Start here:** [Community wiki](docs/wiki/Home.md) |
[Build and upload](docs/wiki/Build-and-Upload.md) |
[First use](docs/wiki/First-Use.md) |
[Hardware wiring](WIRING.md) |
[Troubleshooting](docs/wiki/Troubleshooting.md)

For maintainers: [Source guide](src/SOURCE-GUIDE.txt),
[dependencies](docs/wiki/Dependencies.md),
[publishing checklist](docs/PUBLISHING.md), and
[image catalog](docs/images/README.md).

![ESP32-Crosspoint Wiring](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/wiring-diagram.png)
![web example](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/web-example.png)

Bluetooth Classic keyboards are not supported by ESP32-S3 hardware. A keyboard
that advertises to a phone as "Bluetooth 3.0 Keyboard" is almost certainly a
Classic HID keyboard, not BLE HID, and will not appear in this firmware's BLE
scan.

The original Astrocade keypad remains connected in parallel.

## Build from a fresh clone

Open this folder in VS Code with the PlatformIO extension and run Build.

`ASTROCADE_SERIAL_DEBUG_ENABLED=0` silences per-key and diagnostic logs, but
boot, pairing-code, keyboard-ready, AP credentials, admin-key, and joined
network/IP messages remain visible on the CH343 COM port at 115200 baud.

USB, BLE, and web input do not require one another. USB/BLE work without a
Wi-Fi connection; web input works without either keyboard, including directly
through the setup AP without a router. All may be connected at once. During a
web text transfer, physical keys are intentionally ignored except Escape to
cancel, keeping pasted programs from being mixed with keyboard input.

## Hardware GPIOs

The full build is the default. For BLE + Wi-Fi without a USB host, select
`Astrocade-Keyboard-esp32-s3-ble-wifi` in PlatformIO Project Tasks. Both use the
same S3 N8R2 memory configuration and default GPIOs; no source fork is required.
The eight `ASTROCADE_CROSSPOINT_*_GPIO` flags in `[s3]` change the ESP32 side
for both S3 profiles. `Astrocade-Keyboard-esp32-4mb-ble-wifi` supports classic
ESP32 4 MB boards with its own safe GPIO map, no USB host, and RGB disabled.
**Do not use the S3 wiring on classic ESP32: GPIO6/7 are flash connections.**
See [configuration](docs/wiki/Configuration.md) for checks and restrictions.

The following table describes the **default** wiring:

| ESP32-S3 | CD74HCT22106E signal | 28-pin PDIP pin |
|---|---|---|
| GPIO4 | A4 | 28 |
| GPIO5 | A3 | 27 |
| GPIO6 | A2 | 26 |
| GPIO7 | A1 | 25 |
| GPIO15 | A0 | 24 |
| GPIO16 | A5 | 1 |
| GPIO17 | DATA | 4 |
| GPIO18 | STROBE | 2 |
| GPIO19 | USB D- |
| GPIO20 | USB D+ |
| GPIO48 | on-board RGB LED status |

GPIO19 and GPIO20 are never used as general-purpose GPIOs; they are kept for
the board's native USB-C / OTG connector. GPIO48 is also kept off the
crosspoint bus because the pictured board maps its built-in RGB LED there.

Package note: the CD74HCT22106E datasheet lists the `E` package as 28-lead
PDIP. See [WIRING.md](WIRING.md) for the full prototype wiring outline,
including power, reset, pull resistors, and the Bally matrix lines.

## BLE enrollment and reconnect

The BLE transport uses the stock ESP-IDF NimBLE central APIs proven in the
separate BLE-pairing test. No framework patches or private HID-driver APIs are
required. Security completes before HID discovery and notification subscription.
The stock HID report-map parser identifies keyboard reports; vendor and media
reports are not sent to the BASIC overlay.

1. Put the keyboard into pairing mode and reset the adapter.
2. During the 10-second startup window, a saved bond takes priority; otherwise
   a keyboard candidate may enroll.
3. If prompted in the serial monitor, type `123456` and Enter on the keyboard.
4. Afterwards, use the same keyboard preset with a short press. Do not hold it
   into pairing mode. Saved NimBLE bonds reconnect after keyboard power-off,
   sleep, or ESP32 reset without entering the passkey again.

After startup, only bonded peers can reconnect, including unnamed directed
advertisements. An unpaired adapter stops scanning until reboot. USB remains
available throughout. To replace a keyboard or clear stale bonds, hold BOOT
(GPIO0) for three seconds while firmware is running; the adapter clears only
BLE bonds/preferred-keyboard metadata and restarts for enrollment.

When upgrading from the old Bluedroid firmware, pairing may be required once
because Bluedroid and NimBLE use different bond stores. A bond made by the
BLE-pairing project on this ESP32 should remain usable when uploading normally;
do not erase flash or clear BOOT unless reconnect actually fails.

Connection, pairing, and key/crosspoint logs remain available. Advertisement,
GATT-discovery, and report-detail logs are quiet by default; enable
`ASTROCADE_BLE_VERBOSE_DEBUG_ENABLED=1` only for troubleshooting.
`ASTROCADE_SERIAL_DEBUG_ENABLED=0` disables application serial debug.
Setup and connection notices still print with that flag off.
Caps Lock, Num Lock, purple BLE activity, and BASIC mappings use the same
router as USB. Supported input is a conventional 8-byte keyboard report or
the K380s-style 7-byte report without the reserved byte; boot input is the
fallback. NKRO-only/proprietary layouts are not decoded.

## USB behavior

USB is initialized only in the full S3 USB + BLE + Wi-Fi profile.

The USB implementation uses Espressif's `usb_host_hid` component and Boot
Protocol keyboards. The native ESP32-S3 USB pins remain:

- GPIO19 = D-
- GPIO20 = D+

USB CDC and USB Serial/JTAG console output stay disabled so they do not share
the native USB OTG port. Debug output uses the board's CH343 USB-to-serial COM
port at 115200 baud.

Native USB host mode also requires the OTG connector/cable/adapter to provide
host-role wiring and 5 V VBUS power to the keyboard. If the keyboard's lock
LEDs never light and the monitor repeatedly reports no HID attach event, the
keyboard is not being powered/enumerated at the USB layer yet.

## Status LED and debug flags

The on-board RGB LED on GPIO48 is enabled only for the explicitly identified
N8R2 profiles. All other builds disable it. The N8R2 status patterns are:

- slow pulsing blue during the startup enrollment scan when no preferred BLE
  keyboard is saved;
- fast pulsing blue while loading a saved BLE keyboard or after a new BLE
  candidate is detected;
- four fast red pulses if the startup scan finds no keyboard;
- orange on USB keypress detection;
- purple on BLE keypress detection.

The LED work runs in a low-priority task. Input and crosspoint timing paths only
queue status events.

CD74HCT22106 STROBE mirror blinking is disabled by default because it adds
visual noise during key timing tests. Enable/disable LED activity, key debug
prints, or strobe blinking from `platformio.ini`:

    -DASTROCADE_STATUS_LED_ENABLED=0
    -DASTROCADE_SERIAL_DEBUG_ENABLED=0
    -DASTROCADE_KEY_DEBUG_ENABLED=0
    -DASTROCADE_CROSSPOINT_DEBUG_ENABLED=0
    -DASTROCADE_CROSSPOINT_STROBE_LED_ENABLED=1
    -DASTROCADE_CROSSPOINT_SETUP_DELAY_US=1
    -DASTROCADE_CROSSPOINT_STROBE_LOW_US=1
    -DASTROCADE_CROSSPOINT_RECOVERY_DELAY_US=1
    -DASTROCADE_KEYPRESS_DOWN_MS=15
    -DASTROCADE_KEYPRESS_GAP_MS=5
    -DASTROCADE_BACKSPACE_LEFT_ERASE_ENABLED=0
    -DASTROCADE_USB_CAPS_LOCK_LED_DEFAULT_ON=0
    -DASTROCADE_USB_NUM_LOCK_LED_DEFAULT_ON=0
    -DASTROCADE_USB_NUM_LOCK_TOGGLE_ENABLED=0
    -DASTROCADE_USB_CAPS_LOCK_TOGGLE_ENABLED=0

USB keyboards are initialized with Caps Lock and Num Lock LEDs on by default.
Caps Lock keeps letter entry in the natural uppercase BASIC style. Num Lock
nudges full-size keyboards to report keypad digits/operators instead of keypad
navigation. The physical Caps Lock and Num Lock keys toggle those LED states at
runtime. In BASIC overlay mode, letters are still sent through the Bally's
uppercase BASIC letter mapping.

## BASIC overlay architecture

The firmware is split into layers:

    USB HID --------\
                     \
                      keyboard_router
                     /       |
    BLE HID --------/        v
                         overlay_basic
                              |
                              v
                        crosspoint
                              |
                              v
                         Astrocade

This separation is intentional.

A future overlay implementation can be added beside `overlay_basic.cpp` and
selected from the persistent `OverlayMode` setting without rewriting the USB,
BLE, or CD74HCT22106 layers.

## Wi-Fi, text transfer, and OTA

With `ASTROCADE_WEB_ENABLED=1`, Wi-Fi starts after BLE startup enrollment.
When no saved network is available, join `AstrocadeKeyboard-XXXX` using
Wi-Fi password `12345678`, then open `http://192.168.4.1/`.
The separate device administration key is `123456`. These are intentionally
simple development credentials; do not expose this device to untrusted networks.
They are defined in `src/web_portal.cpp` and replace the older random device key
without erasing saved Wi-Fi credentials or Bluetooth bonds.

The Network page scans nearby networks into a dropdown, with a manual option
for hidden networks. Once connected, use `http://astrocadekeyboard.local/`
or the IP printed in the monitor. The setup AP shuts down after 30 seconds
connected, and returns if the saved network remains unavailable.

Joining the setup AP supports the phone/computer's captive-network setup
prompt through DNS/HTTP redirection and DHCP portal discovery. Whether it
opens automatically is controlled by the client OS. If no prompt appears,
open `http://192.168.4.1/` manually; no serial monitor is required.

The Send page validates the whole text against the active overlay before
starting. CR/LF line endings become Enter; a final Enter is added when needed.
Bally BASIC is currently the only overlay. Sending uses the dedicated keypad
worker and the configured keypress timings; `ASTROCADE_TEXT_LINE_GAP_MS`
adds a pause after each line. Pause/cancel finish the current character.

With serial and crosspoint debug enabled, both keyboard and pasted-text output
produce matrix write records in the monitor. A bounded background logging queue
keeps serial output out of the timed keypress path; if it fills, debug records
are dropped (with a warning), not keypad operations.

Firmware uploads use the Firmware page and the device administration key.
Upload the application's `firmware.bin`, not a merged flash image.
OTA and network scans are blocked while a text transfer is active.
The 8 MB partition table includes two 2.5 MB application slots for OTA.

## Flash requirement

The S3 profiles use `partitions_8mb.csv` and `sdkconfig.defaults.esp32s3`:
8 MB Quad flash and 2 MB Quad PSRAM. The classic ESP32 profile uses
`partitions_4mb.csv` and `sdkconfig.defaults.esp32`: 4 MB flash, no PSRAM.
Common SDK settings are in `sdkconfig.defaults`.

If the development board has a different flash size, change the PlatformIO
flash-size setting and partition table before flashing.


## Build

Open this directory in VS Code with PlatformIO installed, then:

    PlatformIO: Build

The serial monitor is useful for USB/BLE diagnostics:

## Current BLE keyboard compatibility

The BLE input path requires a Bluetooth Low Energy HID keyboard. Bluetooth
Classic HID keyboards, including devices shown by phones as Bluetooth 2.x or
3.0 keyboards, require different hardware because ESP32-S3 does not include
Bluetooth Classic/BR-EDR.

For compatible BLE HID keyboards, the input path expects a keyboard report
compatible with the normal
8-byte boot-style HID keyboard report:

- modifier byte
- reserved byte
- six HID key usage bytes

The K380s seven-byte report is also accepted by inserting the reserved byte.
The stock HID descriptor parser identifies the keyboard report so media and
vendor reports are ignored. Proprietary/NKRO-only layouts still require a
separate decoder.

Compile-time report conversion checks (from a PlatformIO terminal):

```powershell
& "$env:USERPROFILE\.platformio\packages\toolchain-xtensa-esp-elf\bin\xtensa-esp32s3-elf-g++.exe" -std=c++17 -Wall -Wextra -Werror -Iinclude -fsyntax-only test/ble_report_test.cpp
```

## Safety behavior

On USB disconnect, BLE disconnect, malformed rollover reports, or HID transfer
errors, the affected keyboard state is discarded. Pending synthetic BASIC
keypresses are cleared on source disconnect.

The crosspoint layer is reference-counted so one input source cannot release a
matrix position still held by another source.

The external CD74HCT22106 MR/reset hardware from the original design remains
the hardware-level protection against an ESP reset leaving a keypad crosspoint
closed.

## Community

This is an independent community project, not an official Bally product.
Please read [CONTRIBUTING.md](CONTRIBUTING.md) before proposing changes and
[SECURITY.md](SECURITY.md) before placing the adapter on a network.
