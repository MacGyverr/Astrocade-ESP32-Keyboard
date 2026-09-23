# First Use

You can use USB, BLE, or browser input independently, or leave all connected.
No router or internet connection is required for USB/BLE typing. Browser input
can use the setup AP without a router. No keyboard is required to use the web UI.

## USB keyboard

Plug a USB HID Boot keyboard into the native OTG connector using a suitable
host adapter. The serial monitor announces when it is ready. Orange indicates
USB key activity. If it does not enumerate after reset, unplug and reconnect it.

## Bluetooth keyboard

Use **BLE HID**, not Bluetooth Classic/3.0. A Logitech Pebble K380s was tested;
the original K380 is a different product and must not be assumed equivalent.

1. Select a keyboard pairing preset and put it in pairing mode.
2. Reset the ESP32. New-device enrollment lasts 10 seconds by default.
3. When pairing requests a code, type **123456**, then **Enter**, on the keyboard.
4. On later starts, select the same preset with a short press, not a long
   pairing-mode press. Saved bonds reconnect without entering the number again.

Hold the ESP32 BOOT button for three seconds while the firmware runs to clear
BLE bonds and restart enrollment. This is not a normal reconnect step.
Purple indicates BLE input. Some keyboards have no physical Caps/Num Lock LEDs;
the lock state can still work. BASIC letters remain uppercase.

## Wi-Fi and device key

Wi-Fi starts after the initial BLE enrollment period. When no saved network
is available, look for **AstrocadeKeyboard-XXXX**.

| Setting | Default |
|---|---|
| Setup AP Wi-Fi password | `12345678` |
| Web administration key | `123456` |
| BLE first-pairing code | `123456`, then Enter |
| Setup page | `http://192.168.4.1/` |
| On your home Wi-Fi | `http://astrocadekeyboard.local/` |

Join the setup AP and use the captive-network notification if it appears.
Otherwise open the setup address manually. On **Network**, choose an SSID
from the dropdown, enter its password, and save. Hidden networks have a manual
option. After joining, reconnect your phone/computer to the same home network.
The setup AP closes after a successful connection has been stable for 30 seconds.

These deliberately simple defaults are for a trusted local network only.
Do not expose the web server to the internet. Startup serial messages at 115200
show the actual AP name, joined SSID, IP, admin key, and mDNS address even with
debug off. mDNS uses **.local**; the short hostname alone is not guaranteed.

![network example](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/network-example.png)

![Phone setup AP and portal](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/AP-setup.png)

USB keyboard steps apply only to the full S3 build. BLE and web text input work
independently on all profiles. The page's status strip shows each keyboard's
connection and name when available, plus build type and firmware version.
