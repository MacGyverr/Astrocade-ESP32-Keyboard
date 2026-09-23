# Web Text Transfer and OTA

The status strip shows USB and BLE independently: not built, disconnected,
or connected with the keyboard's reported name when available. Build type and
firmware version remain visible on every tab. Firmware details include the
configured board model, detected flash, available PSRAM heap, RGB setting, and
actual next OTA slot capacity. USB remains marked **Not built** on BLE-only builds.

## Send BASIC

1. Put the Astrocade into the appropriate BASIC input state.
2. Open the adapter page and choose **Send text**. BASIC is currently the only overlay.
3. Paste text or load a text file, then choose **Validate**.
4. Resolve unsupported characters using the reported line and column.
5. Choose **Send to Astrocade** and watch progress on both the page and console.

Validation checks whether characters can be entered through the overlay; it
does **not** check BASIC syntax, program memory limits, or whether the Bally
accepted each key. There is no console readback or automatic error correction.
Start with a short program and conservative timings before sending long listings.

CR, LF, and CRLF become Enter; a missing final newline gets a final Enter.
The text limit is 256 KiB on N8R2 or 8 KiB on the classic 4 MB build, not a
guarantee that the Bally can hold that much program text. Unsupported characters are rejected before transfer.

The timed keypad worker, not the HTTP handler, sends characters. Pause and
Cancel finish the current character first. Physical keys are ignored during a
transfer except Escape to cancel. Closing the browser does not cancel the job;
reopen it to inspect status. Avoid restarting or powering off mid-transfer.
Debug can show matrix writes without making the timing task wait for serial.

![web example](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/web-example.png)

![Phone web interface](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/web-phone-example.png)

## Wireless firmware update

1. Build firmware for this board using PlatformIO.
2. Wait until text sending and physical keyboard activity have stopped.
3. Open **Firmware**, enter the admin key, and select the application's
   `firmware.bin` from `.pio/build/<your-selected-environment>/`.
4. Start the update and leave power connected until the adapter restarts.
5. Reopen the page and verify keyboard operation.

Do not upload `bootloader.bin`, a partition table, a merged image, or firmware
for a different board. A partition-layout change requires a planned USB update,
not just replacing the OTA application. Keep the USB cable available for recovery.

The page and server enforce the device's actual OTA slot capacity, not a fixed
S3 limit. The classic 4 MB layout has 0x1D0000-byte slots; S3 uses 0x280000.

OTA is authenticated with the device key but is HTTP, not encrypted HTTPS.
It checks the image format/target, not a cryptographic publisher signature.
Use only firmware from a trusted source on a trusted network.

![ota example](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/ota-example.png)
![ota example](https://raw.githubusercontent.com/MacGyverr/Astrocade-ESP32-Keyboard/main/docs/images/ota-4mb-example.png)
