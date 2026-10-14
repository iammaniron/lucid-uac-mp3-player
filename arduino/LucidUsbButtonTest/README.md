# Earphone button capture to SD: `/file.txt`

Open **LucidUsbButtonTest.ino** in Arduino IDE. The test now writes a readable **`file.txt` in the SD card's root directory**. No USB Serial Monitor or UART adapter is needed. Existing file contents are preserved; each test appends another session.

The firmware also tries to recover the **previous version's saved flash capture** into this file before starting a new test. Recovered groups have unknown button names because the previous test did not establish them. The old flash record is removed only after the SD export has been closed and verified by reading it back. If no valid previous capture exists, the sketch simply starts a new test.

## Upload and test

1. Keep your SD card inserted. Use the existing Arduino settings: **ESP32S3 Dev Module**, esp32 package **3.3.10**, **USB-OTG (TinyUSB)**, **USB CDC/MSC/DFU On Boot disabled**, **PSRAM disabled**, **4 MB flash**, **UART0 / Hardware CDC** upload. Keep **Erase All Flash Before Sketch Upload: Disabled** to preserve the old diagnostic capture for recovery.
2. Disconnect the earphones/external host supply, connect the PC and upload using BOOT + RESET as before.
3. Disconnect the PC. Restore the working 5 V supply and earphone connection; tap **RESET**. If an old flash log is found, the LCD briefly says `OLD LOG COPIED TO SD`.
4. Wait for the **slow GPIO21 LED blink** and `READY: CLICK 1 OF 3`. **Short-press and release centre/pause**. The LCD names the expected button.
5. After the **one-flash saved acknowledgement**, wait for the next slow blink. **Short-press and release volume +**.
6. After the **two-flash saved acknowledgement**, wait for the next slow blink. **Short-press and release volume −**.
7. After the third capture, the file is flushed, closed and read back to check it; the SD filesystem is then unmounted. The LED **flashes three times, stays lit for two seconds, then turns off** along with the LCD backlight as the ESP32 sleeps.
8. **Power off the board before removing the SD card.** Read the card on your computer and send **`file.txt`**, or paste its contents here. No Glyph serial connection is involved.

The button names are **expected test labels, not decoded identities**. We still need the raw reports to determine the real mapping. If you press different buttons or use a different order, tell us what you pressed for groups 1, 2 and 3. If your earphones only have one button, press it once for each group and mention that.

Wait for READY between short presses: each group includes the press and release, with a minimum 1.5-second capture window and one second without changed data before completion. This counts raw-report activity bursts; long holds, rapid multi-clicks or changing background reports may not correspond one-to-one with physical buttons.

## File contents

The log includes device/configuration/HID report descriptors, USB setup/error messages, every received interrupt report, and save checkpoints. It has no previous eight-report-per-click storage cap. File growth is limited by the card's free space; the capture ends after three groups or the timeout.

Example format only (the hex bytes are illustrative, not your earphones' known mapping):

```text
=== NEW SD BUTTON CAPTURE v4 ===
Expected order: 1=CENTRE/PAUSE, 2=VOLUME +, 3=VOLUME -.
...
RAW t_ms=12034 group=1 expected_button="CENTRE/PAUSE" addr=1 if=3 class=03 ep=83 len=2 : 01 04
RAW t_ms=12200 group=1 expected_button="CENTRE/PAUSE" addr=1 if=3 class=03 ep=83 len=2 : 01 00
CHECKPOINT t_ms=13534 groups=1/3 finished=0 reason=Click saved; capture in progress
```

`t_ms` is the time the report was processed, in **milliseconds since that boot**, not the calendar time. Each new boot appends a separate session header. Group 0 is startup/between-click traffic. An older recovered capture has its own section and timestamps from its original boot.

## Errors and another capture

- `SD NOT READY` or rapid continuous LED blinking: check the card/filesystem, insert it before boot, then reset. The test never formats the card.
- `SD WRITE/VERIFY ERROR` / `SD WRITE FAILED`: the card could not save or return the expected data. The firmware stops capturing and does not give a successful-save acknowledgement.
- If no buttons register, **hold BOOT for two seconds while the sketch is already running** to finish a partial log. Don't hold it during reset, which enters the uploader.
- After a USB device enumerates, a **90-second timeout** also finishes a partial log. If no device enumerates, hold BOOT to finish the available diagnostics.
- The file is synchronized periodically (about every two seconds) as well as after each completed group. Still wait for the completion indication and power off before removing the card; unplugging during an SD write can damage a FAT filesystem.
- To run another test, put the card back and reset. It appends another session automatically. There is no ARM command or flash-replay mode now.

## Implementation and checks

The soldered pins stay unchanged: SCK35, MOSI36, MISO37, SD CS44, LCD CS43, DC18, RST17, BLK16; GPIO21 is the indicator. SD initialization still precedes LCD initialization. All file operations and LCD drawing run on the same application task to serialize use of SPI.

This is a silent test: it does not play or modify music files. It captures raw interrupt-IN traffic without the music player's button decoder. Capture supports up to eight interfaces, two interrupt-IN endpoints per interface, 64-byte packets, alternate zero, and HID report-descriptor fetches up to 2048 bytes. Some earphones may require active audio before reporting buttons; partial descriptor logs still help diagnose that.

Build and software checks:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,PSRAM=disabled,FlashSize=4M --build-path /tmp/lucid-buttons-build arduino/LucidUsbButtonTest
python3 -m unittest discover -s tests -p 'test_button*.py' -v
```

Compilation and host tests cover append preservation, buffering, failed writes, readback corruption, log formatting, old-capture recovery formatting and click grouping. SD capture still needs the physical board/card test. Upload `../LucidUacMp3Player/LucidUacMp3Player.ino` again when you want to restore music playback.
