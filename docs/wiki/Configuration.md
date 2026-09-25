# Configuration

Edit existing entries in `platformio.ini`, then build and upload again.
Do not add duplicate definitions of the same flag. Defaults and checks live in
`include/app_config.h`; the checked-in INI is the current build configuration.

Shared flags are in `[env]`. The full environment sets
`ASTROCADE_USB_ENABLED=1`; the BLE/Wi-Fi environment sets it to `0`. This removes
USB keyboard host initialization and implementation from the firmware, not BLE,
Wi-Fi, OTA, or the CH343 serial console. No separate source fork is needed.

## Crosspoint GPIO assignment

Both S3 profiles inherit these defaults from `[s3]`:

```ini
-DASTROCADE_CROSSPOINT_A0_GPIO=8
-DASTROCADE_CROSSPOINT_A1_GPIO=18
-DASTROCADE_CROSSPOINT_A2_GPIO=17
-DASTROCADE_CROSSPOINT_A3_GPIO=16
-DASTROCADE_CROSSPOINT_A4_GPIO=15
-DASTROCADE_CROSSPOINT_A5_GPIO=7
-DASTROCADE_CROSSPOINT_DATA_GPIO=5
-DASTROCADE_CROSSPOINT_STROBE_GPIO=4
```

Edit the existing profile values to match your wiring. These are **ESP32 GPIO
numbers**, not header or crosspoint DIP pin numbers. Crosspoint-side pins,
resistors, and Bally matrix connections in WIRING.md do not change. Startup
serial output prints the configured GPIO mapping even with debug off.

The conservative S3 validator accepts GPIO1, 2, 4-18, 21, 38-42 and 47.
It rejects duplicate assignments and conflicts with the enabled BLE reset
button. USB19/20, UART43/44, RGB48, boot straps and memory pins remain reserved
in both profiles. Check your actual board schematic for other attached hardware;
compile-time checks cannot discover board-specific connections. GPIO39-42 are
also used by external JTAG, so do not share them with an active JTAG probe.

The classic ESP32 profile overrides all eight pins with its own safe map in
`platformio.ini`: A0=12, A1=14, A2=27, A3=26, A4=25, A5=33, DATA=32,
STROBE=21. GPIO13 is also suitable for STROBE: change the existing flag and wire
together; never connect both outputs. The validator rejects flash, input-only,
UART, and boot-strap pins except GPIO12 specifically as A0. GPIO12 must remain
low during boot: retain the documented 100 kOhm pull-down and never add a pull-up.
See [the wiring table](WIRING.md); S3 and classic wiring are not interchangeable.

## Build identity and RGB

`ASTROCADE_FIRMWARE_VERSION` defaults to `1.0.0`; see
[build instructions](Build-and-Upload.md) for the quoted flag syntax.
`ASTROCADE_CLASSIC_ESP32=1` selects the classic chip profile.
`ASTROCADE_BOARD_N8R2=1` explicitly identifies the tested S3 N8R2 board.
The web build/board labels reflect these flags; flash size is measured at runtime.

RGB defaults off unless the build explicitly identifies N8R2. Enabling
`ASTROCADE_STATUS_LED_ENABLED=1` without that identification is a compile error,
preventing accidental GPIO48 use. The classic profile explicitly disables RGB.
Only identify N8R2 when the actual memory and RGB wiring match that board.

## Other build flags

| Flag | Current setting | Purpose |
|---|---:|---|
| `ASTROCADE_SERIAL_DEBUG_ENABLED` | 1 | Per-key/diagnostic logs; setup notices remain on |
| `ASTROCADE_CROSSPOINT_DEBUG_ENABLED` | 1 | Matrix logs when serial debug is also on |
| `ASTROCADE_STATUS_LED_ENABLED` | 1 N8R2 / 0 classic | Onboard RGB activity |
| `ASTROCADE_WEB_ENABLED` | 1 | Wi-Fi and web interface |
| `ASTROCADE_KEYPRESS_DOWN_MS` | 15 | Key closure duration in milliseconds |
| `ASTROCADE_KEYPRESS_GAP_MS` | 5 | Gap between synthetic keypresses |
| `ASTROCADE_TEXT_LINE_GAP_MS` | 100 | Additional pause after Enter in pasted text |
| `ASTROCADE_TEXT_MAX_BYTES` | 262144 N8R2 / 8192 classic | Maximum submitted text bytes |
| `ASTROCADE_BACKSPACE_LEFT_ERASE_ENABLED` | 1 | Backspace sends left then ERASE |
| `ASTROCADE_BLE_ENROLLMENT_WINDOW_SECONDS` | 10 | Startup new-keyboard enrollment |

Crosspoint setup, strobe-low, and recovery timings are separate microsecond
flags documented in `WIRING.md`. They are not the millisecond keypress delays.

USB and BLE each have `CAPS_LOCK_LED_DEFAULT_ON`, `NUM_LOCK_LED_DEFAULT_ON`,
`CAPS_LOCK_TOGGLE_ENABLED`, and `NUM_LOCK_TOGGLE_ENABLED` options, prefixed with
`ASTROCADE_USB_` or `ASTROCADE_BLE_`. Actual keyboard LEDs depend on the hardware.

For BLE diagnostics enable serial debug and, if needed,
`ASTROCADE_BLE_VERBOSE_DEBUG_ENABLED=1`. Turn verbose logging off after testing.

The setup AP password and web admin key are presently constants in
`src/web_portal.cpp` (`apPassword` and `adminKey`), not runtime settings.
The web key field initially suggests the development default; enter your changed
key if you customize it. BLE passkey configuration is in `include/app_config.h`.
Never commit personal Wi-Fi passwords or exported device flash/NVS contents.
