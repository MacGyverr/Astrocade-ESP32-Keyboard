# Prototype wiring outline

The ESP32 GPIOs below are the default mapping for both full and BLE/Wi-Fi-only
S3 builds. Override the eight `ASTROCADE_CROSSPOINT_*_GPIO` build flags to change
the ESP32 side only. The crosspoint DIP pins, resistors and Bally connections
stay the same. Verify the GPIO map printed at startup matches your wiring.

This outline assumes the crosspoint IC is a 28-pin `CD74HCT22106E` PDIP. Pin
numbers are shown in normal DIP top view, with the notch at the top: pin 1 is
upper-left, pins 1-14 run down the left side, pin 15 is lower-right, and pins
15-28 run up the right side.

The firmware drives the chip with this address convention:

- `A2..A0` select crosspoint `X0..X7`.
- `A5..A3` select crosspoint `Y0..Y7`.
- Astrocade keypad rows are `Y0..Y5`.
- Astrocade keypad columns are `X0..X3`.
- `DATA = 1` closes the selected switch.
- `DATA = 0` opens the selected switch.
- `STROBE` is active low.

## Power and reset

| CD74HCT22106E pin | Signal | Connection |
|---:|---|---|
| 19 | VDD | Adapter regulated +5 V |
| 5 | VSS | Common GND |
| 10 | MR | 0.1 uF ceramic capacitor to GND |
| 3 | /CE | GND |

Place one 0.1 uF ceramic decoupling capacitor between VDD pin 19 and VSS pin 5,
close to the IC. A 4.7 uF to 10 uF bulk capacitor across the local 5 V/GND rail
is also useful on a breadboard.

The datasheet says MR has an internal pull-up and is normally used with the
0.1 uF capacitor. If reset ever proves unreliable on the real build, add an
optional 10 kOhm pull-up from MR pin 10 to +5 V.

Use a common ground between ESP32, crosspoint, USB keyboard supply, and the
Astrocade keypad matrix. Do not tie the adapter +5 V rail to the Astrocade
internal +5 V rail unless you deliberately redesign the power system around
that. The CD74HCT22106E must be powered whenever live Astrocade matrix signals
are connected to its X/Y pins.

## ESP32 to crosspoint control wiring

For each ESP32 control line, the series resistor is optional but useful on a
hand-wired prototype. Put the weak pull resistor on the crosspoint input side of
the series resistor.

| ESP32-S3 pin | Series resistor | CD74HCT22106E pin | Signal | Pull resistor |
|---|---:|---:|---|---|
| GPIO4 | 220 Ohm | 2 | STROBE | 10 kOhm to ESP32 3V3 |
| GPIO5 | 220 Ohm | 4 | DATA | 100 kOhm to GND |
| GPIO7 | 220 Ohm | 1 | A5 | 100 kOhm to GND |
| GPIO15 | 220 Ohm | 28 | A4 | 100 kOhm to GND |
| GPIO16 | 220 Ohm | 27 | A3 | 100 kOhm to GND |
| GPIO17 | 220 Ohm | 26 | A2 | 100 kOhm to GND |
| GPIO18 | 220 Ohm | 25 | A1 | 100 kOhm to GND |
| GPIO8 | 220 Ohm | 24 | A0 | 100 kOhm to GND |

The eight selected S3 GPIOs are all input/output-capable per Table 3-1 of the
[Espressif module datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf).
They avoid boot straps, flash/PSRAM, UART console GPIO43/44, USB19/20 and RGB48.
GPIO6 is deliberately skipped. ADC, touch, UART and crystal labels are alternate
functions; the firmware configures these eight pins as digital outputs.

On the photographed board, viewed from the component side with the antenna up,
the left header begins: 3V3, 3V3, RST, GPIO4, GPIO5, GPIO6, GPIO7, GPIO15,
GPIO16, GPIO17, GPIO18, GPIO8. Do not use a generic S3 pinout's physical order.

Do not pull STROBE up to +5 V, because the ESP32 GPIO is still connected to that node.
Pulling it to ESP32 3V3 is enough for the HCT input high level and avoids
putting 5 V on an ESP32 pin.

On S3, GPIO19/20 stay reserved for native USB OTG, and GPIO48 for the
N8R2 board's RGB LED.

### Classic ESP32 4 MB alternative

The classic BLE + Wi-Fi profile uses this different GPIO map. **Never use S3
GPIO6/7 on classic ESP32; they are connected to flash.** Keep the same per-line
220 Ohm series resistors and crosspoint-side pull resistors shown above.

| Classic ESP32 GPIO | Crosspoint signal | DIP pin | Pull resistor |
|---:|---|---:|---|
| 12 | A0 | 24 | 100 kOhm to GND |
| 14 | A1 | 25 | 100 kOhm to GND |
| 27 | A2 | 26 | 100 kOhm to GND |
| 26 | A3 | 27 | 100 kOhm to GND |
| 25 | A4 | 28 | 100 kOhm to GND |
| 33 | A5 | 1 | 100 kOhm to GND |
| 32 | DATA | 4 | 100 kOhm to GND |
| 21 | STROBE | 2 | 10 kOhm to ESP32 3V3 |

GPIO13 is also suitable for STROBE (left header pin 15 on the pictured 38-pin
board). To use it instead of GPIO21, change the classic environment's existing
`ASTROCADE_CROSSPOINT_STROBE_GPIO=21` flag to `13` and move the wire. Keep the
220 Ohm series resistor and crosspoint-side 10 kOhm pull-up. Connect STROBE to
only one ESP32 output; the drawing's alternatives must not be joined together.

GPIO12/A0 is a flash-voltage boot strap on classic ESP32. Keep its 100 kOhm
pull-down and do not connect any pull-up or external high-driving circuit to
that net during reset. The firmware permits GPIO12 only for this documented A0
connection, not STROBE. This exception is for the pictured 3.3 V-flash WROOM-32D,
not a general recommendation for other ESP32 modules.

### Diagram review

The final classic drawing matches the table above with GPIO21 STROBE; GPIO13
is the optional alternative. GPIO34-39 are input-only and must never drive STROBE.
See [Espressif GPIO restrictions](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/gpio.html)
and [GPIO12 boot behavior](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/sd_pullup_requirements.html).


The revised S3 map above follows the actual board photo and the corrected drawing
provided by the owner: GPIO4/5 are STROBE/DATA; A5..A0 are GPIO7/15/16/17/18/8.

**Image correction pending:** the saved diagram below still shows the old S3
header arrangement and old USB connector labels. Do not wire the S3 from that
image; use the table above until the corrected image replaces it. The classic
half remains applicable. Wire by GPIO labels, not assumed header positions.

![S3 and classic ESP32 crosspoint drawings](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/wiring-diagram.png)

The updated drawings explicitly label Bally GND. All such symbols must share
a physical common ground. The diode from the Bally lightpen 5 V supply still
needs a voltage/current check: the HCT part
requires **4.5-5.5 V at VDD pin 19**, so diode drop must not take it below 4.5 V
under load. Verify that the console supply can power the ESP32 and any USB
keyboard before using it; the diagram alone does not establish that capacity.
Do not connect independent powered 5 V rails together. See the
[manufacturer datasheet](https://www.farnell.com/datasheets/48691.pdf).
This control-wiring review is not certification of the pictured power circuit.

## Crosspoint to Bally matrix wiring

The Bally schematic shows six row lines pulled down to ground through 8.2 kOhm
resistors. The console scans the columns, and a keypress connects one column to
one pulled-down row. The CD74HCT22106E should therefore connect directly in
parallel with the keypad contacts.

The schematic labels the keyboard connector row pins as 1-6 and the column pins
as 13-16. If your physical harness adapts these to a ten-wire connector, follow
the row/column function rather than assuming the ten wires are numerically
ordered.

| Bally keyboard connector pin | Function | CD74HCT22106E pin | Crosspoint signal |
|---:|---|---:|---|
| 1 | Row 0: GO, PAUSE, HALT, DIVIDE | 18 | Y0 |
| 2 | Row 1: 7, 8, 9, MULTIPLY | 17 | Y1 |
| 3 | Row 2: 4, 5, 6, MINUS | 16 | Y2 |
| 4 | Row 3: 1, 2, 3, PLUS | 15 | Y3 |
| 5 | Row 4: SPACE, 0, ERASE, EQUALS | 14 | Y4 |
| 6 | Row 5: GREEN, RED, BLUE, WORDS | 13 | Y5 |
| 13 | Column 3: DIVIDE, MULTIPLY, MINUS, PLUS, EQUALS, WORDS | 22 | X3 |
| 14 | Column 2: HALT, 9, 6, 3, ERASE, BLUE | 7 | X2 |
| 15 | Column 1: PAUSE, 8, 5, 2, 0, RED | 23 | X1 |
| 16 | Column 0: GO, 7, 4, 1, SPACE, GREEN | 6 | X0 |

Do not add series resistors in the X/Y matrix paths for the normal build. Those
pins are the synthetic keypad contacts, and extra resistance makes the contact
less keypad-like. The CD74HCT22106E itself already has on resistance.

Leave unused analog switch pins unconnected:

- X4 pin 8
- X5 pin 21
- X6 pin 9
- X7 pin 20
- Y6 pin 12
- Y7 pin 11

![Bally keypad wiring](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/bally-keypad-wiring.png)

## Matrix sanity test before connecting the Bally

With the Bally disconnected:

1. Power the ESP32 and crosspoint normally.
2. Pull `Y0..Y5` up to ESP32 3V3 with 10 kOhm resistors.
3. Connect only one `X` column at a time to GND through a 1 kOhm resistor.
4. Temporarily set `ASTROCADE_KEYPRESS_DOWN_MS=1000` in `platformio.ini` so a
   meter or logic probe can catch each synthetic key closure.
5. Press a USB keyboard key that maps to the row and column being tested.

Expected examples:

| Keyboard key | BASIC key | Row/column | Test wiring result |
|---|---|---|---|
| Numpad 8, Num Lock on | 8 | Y1/X1 | With X1 grounded, Y1 drops low for the keypress |
| Space | SPACE | Y4/X0 | With X0 grounded, Y4 drops low for the keypress |
| Numpad / | DIVIDE | Y0/X3 | With X3 grounded, Y0 drops low for the keypress |
| F1 | WORDS then FOR position | Y5/X3, then Y1/X0 | WORDS and target positions pulse in sequence |

Move the 1 kOhm ground resistor between X0, X1, X2, and X3 to prove that the
firmware is selecting both the expected row and the expected column. Restore the
normal key timing after the sanity test.

## Timing build flags

These defaults are intentionally conservative:

```ini
-DASTROCADE_CROSSPOINT_SETUP_DELAY_US=1
-DASTROCADE_CROSSPOINT_STROBE_LOW_US=1
-DASTROCADE_CROSSPOINT_RECOVERY_DELAY_US=1
-DASTROCADE_KEYPRESS_DOWN_MS=15
-DASTROCADE_KEYPRESS_GAP_MS=5
```

The crosspoint latch timing is in microseconds and controls only the chip
programming pulse. The keypress timing is in milliseconds and controls how long
the synthetic Bally key remains closed and the gap before the next synthetic
keypress.
