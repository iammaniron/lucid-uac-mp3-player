# Lucid SD player — UAC1 and UAC2

Open **LucidUacMp3Player.ino** in Arduino IDE. Keep the whole sketch folder, including `src/`, together. This sketch plays music from SD through a UAC1 or UAC2 USB DAC.

The player automatically chooses the UAC1 or UAC2 host driver when a DAC connects. It plays MP3/PCM16 WAV files, including subfolders, in directory order, then repeats the whole playlist indefinitely. One song loops by itself. The LCD stays at **full brightness** and shows the filename plus a footer such as `UAC2 48K 24b V50 E0`.

**Earphone buttons and button logging are disabled in this music build.** It does not claim HID interfaces or create/append button logs. The separate `LucidUsbButtonTest` diagnostic sketch is retained for later.

## Arduino upload

1. Use **esp32 by Espressif Systems 3.3.10** in Boards Manager. Keep this exact version: the bundled USB enumeration fix uses its ESP-IDF 5.5.4 interfaces.
2. Libraries: **Adafruit ST7735 and ST7789 Library 1.11.0**, **Adafruit GFX Library 1.12.6**, **Adafruit BusIO 1.17.4**, and **libhelix 0.9.4**. SD/SPI come with ESP32. Both USB audio drivers are included in `src/`; no AudioTools library installation is needed.
3. Select **ESP32S3 Dev Module**:

| Tools setting | Value |
| --- | --- |
| USB Mode | USB-OTG (TinyUSB) |
| USB CDC On Boot | Disabled |
| USB MSC / DFU On Boot | Disabled |
| Upload Mode | UART0 / Hardware CDC |
| Flash Size | 4MB (32Mb) |
| PSRAM | Disabled |

4. Disconnect the DAC and external host supply, connect Glyph to the PC, hold BOOT, tap RESET, release BOOT, select Port, and upload.
5. Insert the music card before boot. Disconnect the PC, restore the **powered 5 V USB host/DAC connection** described in [USB power notes](../../docs/USB_HOST_CONNECTION.md), then reset Glyph.

Install libhelix v0.9.4 using **Sketch → Include Library → Add .ZIP Library** with the [pinned upstream ZIP](https://github.com/pschatzmann/arduino-libhelix/archive/6183305de92c6c8e445aa95116319b64963a6ad3.zip). The Adafruit libraries are available through Library Manager.

No IDF commands, board-package edits, or USB driver downloads are required. The normal Arduino build compiles the included sources.

## Supported playback

| Connection | Formats selected automatically |
| --- | --- |
| UAC1 DAC | PCM16 in 2-byte slots, mono/stereo, 48 or 44.1 kHz |
| UAC2 DAC | PCM16/24/32 in 2/3/4-byte slots, mono/stereo, 48 or 44.1 kHz |

48 kHz is preferred, then 44.1 kHz; stereo is preferred over mono. UAC2 chooses the exact alternate setting, queries clock frequency ranges, and requests the selected rate (with a current-rate check if SET fails). If RANGE is unavailable, only the confirmed current clock rate is considered. It prefers 16-bit transport where available; a 24-bit-only or 32-bit-only PCM DAC can also be used. Decoding remains 16-bit; wider USB samples are left justified with zero low bits, not extra source detail. UAC2 feedback is honored at both rates, and fractional packet scheduling preserves 44.1 kHz timing.

This is an **ESP32-S3 USB full-speed (12 Mbps) host**, sending audio to an external DAC. UAC2 is an audio class version; it does not make the S3 a USB high-speed host. Supported UAC2 devices need a single clock source, no clock multipliers, a usable Type I PCM output with 1 ms data packets, and a format within the S3 FIFO limit. Asynchronous outputs need an explicit 3- or 4-byte feedback endpoint. High-speed-only DACs, implicit-feedback-only modes, multiple clock domains, multichannel output, rates above 48 kHz, capture/duplex, DSD and encoded/float USB formats are outside this build's scope. A DAC advertised as UAC2 is not necessarily compatible with all these constraints.

USB configuration descriptors up to **2048 bytes** are supported by the sketch-local enumeration override. The stock Arduino package's 256-byte limit can reject composite UAC2 devices before an audio driver sees them. The override is derived from ESP-IDF 5.5.4, includes a version guard, and does not modify the installed Arduino core. See `THIRD_PARTY.md` for sources and local changes.

## SD music and display

- Use the FAT-formatted card from the tested Lucid setup. Music files are read only; this build writes no diagnostic files and never formats the card.
- `.mp3`: ordinary mono/stereo CBR/VBR. `.wav`: uncompressed little-endian PCM16 mono/stereo. Source rates 8–48 kHz are resampled to the negotiated USB rate.
- AAC/M4A, FLAC, OGG, float/24-bit/compressed WAV are not implemented. Unsupported extensions, hidden/system files and invalid tracks are skipped.
- No fixed track-count limit or whole-track RAM buffer. Paths may contain up to 255 characters and eight nested folder levels. Up to 64 invalid paths are cached; further bad files are retried/skipped on later passes.
- Track transitions are not guaranteed gapless. Keep the card inserted during playback; reset after changing its contents. DAC reconnection restarts the playlist.
- `LUCID_MUSIC_VOLUME` in `LucidPins.h` sets software volume, default **50%**. Optional DAC hardware controls request -6 dB and unmute where supported. Buttons currently do nothing.
- BLK GPIO16 stays HIGH from startup: **100% backlight duty**, no dimming/PWM. The footer shows USB class, sample rate, valid PCM bits, software volume and transfer-error count. A successful transfer confirms bus delivery, not audible sound.

## Soldered wiring

| Lucid signal | GPIO |
| --- | ---: |
| CLK / SCK | 35 |
| MO / MOSI | 36 |
| MI / MISO | 37 |
| CS1 / SD | 44 |
| CS2 / LCD | 43 |
| DC | 18 |
| RST | 17 |
| BLK | 16 |

Both peripherals are deselected before SD initialization. LCD initialization follows SD, with the tested `INITR_MINI160x80` / rotation 3 profile. SD reads, decoding and LCD drawing stay on the Arduino application task; USB event processing runs in background tasks.

## Troubleshooting and physical checks

| Screen/symptom | Action |
| --- | --- |
| `SD NOT READY` / `NO MUSIC READY` | Check card and FAT filesystem; insert before reset. |
| `NO MUSIC ON SD` / `NO PLAYABLE TRACKS` | Check supported MP3/PCM16 WAV files. |
| `NO USB ENUMERATION` | Check the working 5 V power setup, cable, and DAC full-speed support. |
| `UAC1 MODE UNSUPPORTED` | Needs PCM16 mono/stereo at 44.1/48 kHz. |
| `UAC2 MODE UNSUPPORTED` | No usable format/rate/clock combination within the limits above. |
| `DAC START FAILED` | Format was found but clock, interface activation or transfer setup failed. |
| `USB STREAM FAILED` / `NO AUDIO TRANSFERS` / `AUDIO WRITE FAILED` | Reconnect DAC; note the displayed error and DAC model. |
| `PLAYING`, correct UAC footer, E0 | Transfers are progressing; confirm sound by listening. |

Native Serial Monitor is unavailable while the same USB port operates as host. The screen provides status without another adapter. Optional logs use UART TX GPIO13, RX GPIO14 at 115200 baud; GPIO43/44 remain assigned to Lucid. Use BOOT + RESET to return to USB upload mode.

Validation: Arduino compilation, actual MP3/WAV decoder/playlist tests, descriptor tests using Avery's captured miniDSP configuration, PCM sample packing, and nominal/feedback packet timing tests. The original UAC1 beep setup is hardware-confirmed; **this combined UAC1/UAC2 revision has not been tested on the physical board**.

After uploading, test the existing UAC1 earphones first, then the UAC2 DAC. Check the footer selects UAC1/UAC2 correctly, listen across a track change and playlist wrap, and unplug/reconnect each DAC to verify recovery. Use a correctly powered external 5 V host setup.
