# Troubleshooting

| Symptom | Check |
|---|---|
| Upload cannot find the board | COM/UART connector, data cable, CH343 driver, serial permissions, selected port, other open monitors |
| No serial text | CH343 COM at 115200; tap RESET after opening Monitor |
| USB keyboard not found | Correct OTG port, host adapter, VBUS power, board-specific jumper; try reconnecting after reset |
| BLE keyboard absent | Confirm BLE HID rather than Classic; enter pairing mode before the startup window |
| BLE asks for code every time | Use the same preset with a short press; avoid clearing bonds or erasing flash on routine updates |
| No Caps/Num LED | Some keyboards have none; check typing/state, not only lamps |
| No setup AP immediately | Wi-Fi waits until after BLE startup; a saved network may connect without starting the AP |
| Setup window does not pop up | Join the AP and manually open `http://192.168.4.1/`; captive behavior varies by OS |
| `.local` address fails | Use the serial-reported IP; verify same LAN, no client isolation/VPN/mDNS blocking |
| Text validation fails | Check reported character/line/column; typographic punctuation may not have a BASIC mapping |
| Bally misses characters | Confirm BASIC input state and matrix wiring; increase key/line timing and test a short listing |
| OTA rejected | Use this project's application firmware.bin, correct target/size, admin key, and idle transfer state |
| First build fails downloading | Check internet/proxy access to PlatformIO and Espressif registries; do not manually patch libraries |
| Compiler reports an internal error or crashes | Retry with fewer compiler jobs: `pio run -j 2 -e YOUR-ENVIRONMENT`; preserve the log if it repeats |

To report a problem, include board/module markings, firmware commit, keyboard
model/transport, build flags changed, steps, and a short relevant log. Redact
Wi-Fi credentials, passkeys, admin keys, identifying SSIDs, and MAC addresses.
Never upload a complete flash dump.

No Wi-Fi, no BLE keyboard, or no USB keyboard should not prevent the other
input methods from working. A missing BLE device may produce the startup red
LED indication; this does not mean USB or browser input has failed.
