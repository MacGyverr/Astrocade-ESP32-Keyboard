# Astrocade Keyboard Wiki

An ESP32/S3 adapter for entering Bally Astrocade BASIC using BLE or a browser,
plus USB keyboards on the full S3 build. The crosspoint emulates keypad contacts; no console firmware changes
are required. Read the wiring guide before connecting vintage hardware.

## Guides

1. [Hardware and safety](Hardware.md)
2. [Build and upload](Build-and-Upload.md)
3. [First use: USB, Bluetooth, and Wi-Fi](First-Use.md)
4. [Web text transfer and OTA](Web-and-OTA.md)
5. [Configuration](Configuration.md)
6. [Troubleshooting](Troubleshooting.md)
7. [Dependencies and maintenance](Dependencies.md)

The wiki lives with the source so it is included in every clone/download.
It does not depend on GitHub's separate Wiki service. Wiring and BASIC mappings
are in [WIRING.md](WIRING.md) and [BASIC_MAPPING.md](BASIC_MAPPING.md).

Only the BASIC overlay is implemented. Other overlays and a Bluetooth
administration page are future work, not current features.
