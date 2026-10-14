# Lucid UAC MP3 Player

An Arduino music player for the **PCB Cupid Glyph ESP32-S3** and **Lucid LCD + microSD board**. Read songs from an SD card and send audio through USB to a DAC or digital earphones, with automatic UAC1/UAC2 selection.

```text
microSD (MP3 / WAV) → ESP32-S3 USB host → USB DAC → headphones / speakers
                          │
                    Lucid 160×80 LCD
```

## Features

- MP3 and PCM16 WAV playback, including subfolders.
- Automatic playlist repeat; no fixed track-count limit.
- UAC1 PCM16 and UAC2 PCM16/24/32 transport at 44.1 or 48 kHz, mono or stereo.
- Streaming decoding and resampling without loading entire tracks into RAM.
- Full LCD brightness, song filename, USB class, sample rate, bit depth and transfer-error status.
- USB reconnect handling and a separate button diagnostic sketch.

**Status:** development firmware. The original UAC1 beep setup was confirmed working with external 5 V power. The combined UAC1/UAC2 music revision compiles and passes 46 native tests, but still needs physical validation. **Earphone buttons and button logging are disabled in the music player.**

## Hardware and wiring

- [PCB Cupid Glyph ESP32-S3](https://learn.pcbcupid.com/documentation/modules/glyph/glyph-esp32s3/glyph-esp32-s3-pinouts).
- Lucid board with a 160×80 ST7735 LCD and SPI microSD slot.
- FAT-formatted microSD card containing supported music.
- Compatible USB DAC or digital USB earphones, with a powered USB host connection.

| Lucid signal | ESP32-S3 GPIO |
| --- | ---: |
| BLK / backlight | 16 |
| RST | 17 |
| DC | 18 |
| CLK / SCK | 35 |
| MO / MOSI | 36 |
| MI / MISO | 37 |
| CS1 / SD | 44 |
| CS2 / LCD | 43 |

Pins are configured in [LucidPins.h](arduino/LucidUacMp3Player/LucidPins.h). LCD and SD share SPI, with SD initialized first. Native USB uses GPIO19/20. Optional runtime UART logs use TX13/RX14, leaving GPIO43/44 available for Lucid.

**USB power matters:** battery power alone did not power the earphones in the tested setup. Provide a regulated 5 V host power path and suitable USB-C source signaling. Disconnect the computer before connecting the external host supply. See [USB power and adapter notes](docs/USB_HOST_CONNECTION.md).

## Quick start — Arduino IDE

1. Install **esp32 by Espressif Systems 3.3.10** in Boards Manager.
2. Install these Library Manager versions: **Adafruit ST7735 and ST7789 Library 1.11.0**, **Adafruit GFX Library 1.12.6**, **Adafruit BusIO 1.17.4**. Install **libhelix v0.9.4** from its [pinned ZIP](https://github.com/pschatzmann/arduino-libhelix/archive/6183305de92c6c8e445aa95116319b64963a6ad3.zip) through **Sketch → Include Library → Add .ZIP Library**. Both USB host drivers are bundled.
3. Open **[LucidUacMp3Player.ino](arduino/LucidUacMp3Player/LucidUacMp3Player.ino)**. Keep its complete folder, including `src/`, together.
4. Select **ESP32S3 Dev Module** and the following settings:

| Tools setting | Value |
| --- | --- |
| USB Mode | USB-OTG (TinyUSB) |
| USB CDC / MSC / DFU On Boot | Disabled |
| Upload Mode | UART0 / Hardware CDC |
| Flash Size | 4MB (32Mb) |
| PSRAM | Disabled |

5. Disconnect the DAC/external host supply and connect Glyph to the computer. Hold **BOOT**, tap **RESET**, release BOOT, select the port, and upload.
6. Insert the music card, disconnect the computer, restore the powered DAC connection, and reset Glyph.

The display shows **PLAYING**, the filename, and a footer such as `UAC2 48K 24b V50 E0`. Songs play in directory order and repeat after the last track. Software volume defaults to 50%; edit `LUCID_MUSIC_VOLUME` to change it.

Keep core **3.3.10**: the bundled fix for larger USB configuration descriptors is tied to its ESP-IDF 5.5.4 interfaces. Native USB Serial Monitor is unavailable while that port is operating as host.

## Compatibility

| Item | Supported |
| --- | --- |
| Input files | Mono/stereo MP3 (CBR/VBR), uncompressed PCM16 WAV; source rates 8–48 kHz |
| UAC1 output | PCM16 in 2-byte slots; mono/stereo; 44.1/48 kHz |
| UAC2 output | Type I PCM16/24/32 in 2/3/4-byte slots; mono/stereo; 44.1/48 kHz |
| UAC2 clocks | Single clock source, no multipliers; explicit feedback for asynchronous outputs |
| USB bus | Full-speed, 12 Mbps; 1 ms audio data packets within S3 FIFO capacity |
| SD traversal | Eight nested folder levels; paths up to 255 characters |

UAC2 is an audio class version, not a promise of compatibility with every DAC. High-speed-only DACs, multiple clock domains, implicit-feedback-only modes, multichannel/high-rate output, capture/duplex, DSD, and float/encoded USB audio are outside this build's scope. Wider USB samples preserve the decoded 16-bit content; they do not add source resolution.

AAC/M4A, FLAC, OGG, compressed/float/24-bit WAV and gapless MP3 trimming are not implemented. Invalid files are skipped. Reset after changing the card; do not remove it during playback.

See the [full Arduino guide](arduino/LucidUacMp3Player/README.md) for negotiation details, display messages, and physical testing steps.

## Build and test

For command-line development, install Arduino CLI **1.5.1**, Git, Python 3, a C/C++ compiler, and ffmpeg:

```bash
bash tools/setup-arduino.sh
bash tools/check.sh
```

The setup script installs pinned Arduino dependencies. The check script compiles the music sketch and runs the native suite. Native tests cover real MP3 decoding, WAV playback, resampling, playlist wrapping, USB descriptor parsing, PCM packing, packet scheduling, and retained button diagnostics. They do not replace a listening test.

The GitHub Actions workflow performs the same checks and compiles the standalone button diagnostic. See [CONTRIBUTING.md](CONTRIBUTING.md) for development and hardware-report guidance.

## Repository layout

| Path | Purpose |
| --- | --- |
| `arduino/LucidUacMp3Player/` | Current Arduino SD music player and bundled USB drivers |
| `arduino/LucidUsbButtonTest/` | Standalone SD/flash button capture diagnostic |
| `tests/` | Native regression tests and attributed descriptor fixtures |
| `tools/` | Dependency setup, build/test and legacy IDF environment helpers |
| `docs/` | USB power notes and legacy ESP-IDF instructions |
| `main/`, `components/` | Retained ESP-IDF UAC1 beep example and its dependencies |

The root ESP-IDF project remains a beep example. Use the Arduino sketch for SD music and UAC2.

## License and credits

Original project code is **MIT licensed**; see [LICENSE](LICENSE). Vendored and external dependencies keep their own licenses, documented in [THIRD_PARTY.md](THIRD_PARTY.md) and the [Arduino dependency notes](arduino/LucidUacMp3Player/THIRD_PARTY.md). In particular, libhelix is an external dependency with GPLv3 wrapper and RealNetworks decoder notices; the root MIT license does not relicense those components.

USB audio builds on [Espressif's UAC1 host driver](https://github.com/espressif/esp-usb/tree/master/host/class/uac/usb_host_uac) and [Avery Levitt's UAC2 host driver](https://github.com/Averyy/esp-uac2-host). MP3 decoding uses [Phil Schatzmann's libhelix](https://github.com/pschatzmann/arduino-libhelix), and the display uses [Adafruit's ST7735 library](https://github.com/adafruit/Adafruit-ST7735-Library). The [AudioTools UAC2 reference](https://github.com/pschatzmann/arduino-audio-tools/wiki/USB-Audio-Class-2.0) informed the host approach; AudioTools itself is not a build dependency.
