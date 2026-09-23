# Hardware and Safety

The full build targets the dual-USB ESP32-S3-WROOM-1-N8R2 board with CH343
serial and GPIO48 RGB. A BLE + Wi-Fi profile also supports classic ESP32
4 MB boards, with RGB disabled and a different crosspoint GPIO map. All use a
**28-pin CD74HCT22106E**. The owner reports successful tests on the four board
models pictured below (classic 4 MB and S3 8 MB). Photos identify physical
boards, not an automatic firmware detection mechanism.

Other boards may have different power routing, jumpers, and header layouts.
GPIO6/7 are used by flash on classic ESP32: do not copy the S3 wiring there.

Use the repository's `WIRING.md` as the single authoritative connection table.
It includes every ESP32 control line, series/pull resistor, crosspoint power and
reset connection, Bally row/column connection, and a disconnected matrix test.
`BASIC_MAPPING.md` describes the overlay mappings. Do not duplicate those tables
in a second wiring document that could become inconsistent.

## Before attaching the Bally

1. Confirm the module, IC markings, package orientation, and physical pin numbers.
2. Build and power the adapter with the Bally disconnected.
3. Check common ground, regulated supplies, decoupling, reset, and control levels.
4. Run the matrix sanity test in `WIRING.md`; use the logic analyzer only within
   its input-voltage rating. Remove the temporary test pull resistors afterwards.
5. Power down before attaching the Bally matrix, then follow the power guidance
   in `WIRING.md`. Do not connect independent 5 V supplies together casually.

Native USB uses GPIO19 D- and GPIO20 D+. The CH343 COM port is separate.
A keyboard needs both a USB host data path and 5 V VBUS. This particular tested
board required its USB-OTG solder bridge for keyboard power. Do not assume the
same bridge is safe or required on another board revision. With host VBUS enabled,
do not connect that native USB connector to a PC's powered USB port.

The connector's schematic row/column labels matter more than the order of a
custom ten-wire harness. ESP32 GPIOs must not receive 5 V.

![esp32 model](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/esp32-model.png)
![wiring diagram](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/wiring-diagram.png)
![Bally keypad wiring](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/bally-keypad-wiring.png)
![proto board](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/proto-board.png)
![usb otg jumper](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/usb-otg-jumper.png)
