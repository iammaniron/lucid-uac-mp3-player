# Legacy ESP-IDF UAC1 beep example

For SD music playback with UAC1/UAC2, use the [Arduino player](../arduino/LucidUacMp3Player/README.md). This document describes only the retained beep example.

ESP-IDF firmware for the **PCB Cupid Glyph ESP32-S3** and your **Lucid ST7735 160×80 LCD + microSD board**. The S3 acts as a **USB audio host** and sends generated PCM audio to an external USB DAC. No audio file is needed yet.

On a supported DAC it repeats **880 Hz for 250 ms, then 750 ms silence**. Both stereo channels carry the same tone, with 5 ms fades and an 8% peak software level. It tries 48 kHz then 44.1 kHz, preferring stereo over mono at each rate. It requires **UAC 1.0, signed 16-bit PCM in 2-byte subframes**. UAC2-only and passive analog USB-C adapters are not supported by this build. The original UAC1 beep connection was confirmed working after providing external 5 V power.

## Your wiring

These are ESP32 GPIO numbers, matching the wiring you supplied:

| Lucid signal | Glyph S3 GPIO |
| --- | ---: |
| CLK / SCK | 35 |
| MO / MOSI | 36 |
| MI / MISO | 37 |
| CS1 / SD CS | 44 |
| CS2 / LCD CS | 43 |
| CD / DC | 18 |
| RST | 17 |
| BLK | 16 |

Keep Lucid's tested power wiring and a common ground; GPIO signals are 3.3 V. Change pins under `idf.py menuconfig` → **Lucid USB beep**. GPIO19 (USB D−) and GPIO20 (USB D+) belong to the native USB port and cannot be moved to arbitrary GPIOs. SPI is on SPI2, and PSRAM is disabled.

Both chip selects are inactive before SPI starts. SD initialization precedes LCD initialization, as in the original tested Lucid sketch. The LCD profile matches `INITR_MINI160x80`, rotation 3. A mountable FAT card appears at `/sdcard`; the firmware never formats it. Missing/unmountable cards do not prevent the beep test. Insert a card before boot; SD hot insertion is not implemented.

## USB power and connection

The Glyph schematic linked by PCB Cupid shows native USB data, 5.1 kΩ CC pull-downs, and **no battery-to-5 V boost/host power switch**. Therefore host firmware alone does not make this connector a complete powered USB-C host. A battery and an ordinary passive adapter are insufficient to power a USB DAC. Direct USB-C-to-USB-C connection can also fail because the board does not advertise a Type-C source role.

For the first bench test, use a **powered USB 2.0 host breakout/OTG arrangement** with:

1. Glyph native USB D+/D− connected to the DAC's USB data pair through a short cable.
2. A single regulated **5 V** source supplying both the Glyph **USB/VBUS** rail and the DAC's VBUS; common ground throughout. Use appropriate current limiting for your hardware. Never connect 5 V to 3.3 V, BAT, or a GPIO.
3. A USB-A downstream socket for a USB-A DAC, or a proper downstream Type-C source adapter for a USB-C DAC. The Type-C adapter must provide the required source CC termination; firmware cannot add that resistor arrangement.

A powered USB 2.0 hub is supported by the firmware, but the upstream Glyph side still needs valid VBUS/power routing. A hub's power brick alone does not guarantee that. Check the adapter/breakout wiring before use. **Disconnect the PC's USB cable before applying the external host-side 5 V**, so two USB power sources cannot backfeed each other. The native port is shared between programming and DAC use.

The exact cable/adapter arrangement depends on your DAC connector and power needs. Start with low amplifier/headphone volume; actual loudness depends on the DAC. Hardware volume is requested at 75% where supported, and software attenuation always applies.

## Build and flash

This is an **ESP-IDF project**, not an Arduino `.ino`. It builds with the ESP-IDF **5.5.5** toolchain. USB dependencies are vendored, so no component download is needed. See `THIRD_PARTY.md` for versions and licenses.

```bash
cd lucid-uac-mp3-player
export IDF_PATH=/path/to/esp-idf
source tools/idf-env.sh
idf.py set-target esp32s3
idf.py build
```

Alternatively, source your ESP-IDF 5.5.5 `export.sh` directly. This configuration uses a conservative 4 MB flash layout with no PSRAM requirement.

To flash through native USB:

1. Disconnect the DAC and external host power, then connect Glyph to the PC.
2. Hold **BOOT**, press and release **RESET**, then release BOOT to enter ROM download mode.
3. Find the board's serial port and run, replacing the example port if necessary:

```bash
idf.py -p /dev/ttyACM0 flash
```

4. Disconnect the PC. Connect the powered DAC setup described above and reset Glyph.

The display should show `CONNECT USB DAC`, then `BEEP TEST` and the selected sample rate. Disconnecting the DAC returns to the waiting screen; reconnecting a supported DAC restarts the tone. There are no playback buttons or MP3 decoding yet.

**USB serial disappearing when this application starts is expected:** the USB peripheral is now a host. Enter BOOT/RESET download mode again to reflash.

## Optional serial logs

Your LCD and SD use GPIO43/44, which are also the default UART0 pins. The application and second-stage bootloader console are remapped to **TX GPIO13 / RX GPIO14, 115200 baud**. Use a separate **3.3 V logic USB-to-UART adapter**:

| Adapter | Glyph |
| --- | --- |
| RX | GPIO13 |
| TX (optional) | GPIO14 |
| GND | GND |

Leave the adapter's power pin disconnected. For logs only, RX + GND is enough. Monitor its port with `idf.py -p /dev/ttyUSB0 monitor` (exit Ctrl+]). These remapped pins are for runtime logging, not ROM UART flashing. ROM may briefly print on GPIO43 at reset before firmware takes control; the LCD is subsequently reset and initialized.

## Troubleshooting and validation

| Symptom | Check |
| --- | --- |
| Stays on `CONNECT USB DAC` | DAC power, USB data cable, CC/source adapter, UAC1 support; inspect UART enumeration logs. UAC2 devices may never reach the application callback. |
| `FORMAT UNSUPPORTED` | DAC must advertise 16-bit mono/stereo at 48 or 44.1 kHz; UART prints its formats. |
| `DAC START FAILED` / `AUDIO STOPPED` | Inspect UART error; reconnect DAC. Check power and cable first. |
| `SD NOT READY` | Optional for beeps; check card/FAT filesystem/CS1. No auto-format is performed. |
| Blank or shifted LCD | Check CS2/DC/RST/BLK and mini 160×80 panel variant. |
| No tone despite `BEEP TEST` | Confirm the DAC's output, amplifier/headphone level and mute; inspect transfer errors. The screen shows configured playback, not proof of acoustic output. |

Disable **Enable Lucid LCD and SD initialization** in menuconfig for a USB-only test. The audio path is independent of SPI.

Host PCM tests:

```bash
python3 -m unittest discover -s tests -v
```

They compile the actual C generator and check both sample rates/channel counts, tone frequency, silence duration, level, stereo equality, and continuity across arbitrary buffer boundaries. A successful firmware build does not establish physical DAC/LCD compatibility. The hardware acceptance check is: boot with/without SD, hear repeated beeps, unplug/replug the DAC, and verify recovery on screen and UART.

SD-file decoding and UAC2 support are implemented in the separate Arduino player. They are not part of this legacy example.

## References

- [PCB Cupid Glyph S3 pinout](https://learn.pcbcupid.com/documentation/modules/glyph/glyph-esp32s3/glyph-esp32-s3-pinouts)
- [Glyph S3 schematic linked by PCB Cupid](https://files.pcbcupid.com/Documentation/Boards/glyph-S3/S3%20schematic.pdf)
- [Espressif UAC host 1.5.0](https://components.espressif.com/components/espressif/usb_host_uac/versions/1.5.0/readme)
