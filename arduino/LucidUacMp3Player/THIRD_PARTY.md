# Bundled USB audio driver

`src/uac/` contains the C implementation and headers of Espressif **usb_host_uac 1.5.0**, from source commit `6d24137e14a4f6c8138662a7a074be16a899c2c6`, path `host/class/uac/usb_host_uac`.

Source: https://components.espressif.com/components/espressif/usb_host_uac/versions/1.5.0

Copyright notices remain in the source files. The Apache-2.0 license is included at `src/uac/LICENSE`.

Arduino packaging changes:

- The two C files include the adjacent `uac_host.h` instead of `usb/uac_host.h`.
- `uac.h` includes `uac_config.h`, which loads `sdkconfig.h` and supplies the upstream UAC Kconfig defaults and package version macros that would otherwise come from CMake.
- USB host implementation and public USB headers come from the ESP32 Arduino core, with the local enumeration override documented below. The standalone ESP-IDF project's USB component is not used by this sketch.

Lucid v2 changes to the bundled driver (the standalone ESP-IDF copy is unchanged):

- Added `UAC_HOST_DEVICE_EVENT_TX_TRANSFER_DONE`, emitted only after a real successful isochronous transfer with payload. The upstream `TX_DONE` event reports buffer demand and is not proof that audio reached the USB bus.
- Check packet-level completion errors and report rejected USB transfer submissions. Rejected submissions return the transfer to the free list instead of silently losing it.
- Release a failed-open ring buffer before freeing its owning interface, avoiding a null/freed-pointer access in that cleanup path.

The v2 application player is rewritten as an Arduino begin/poll state machine with a separate USB observer, following [Espressif's host example API sequence](https://github.com/espressif/esp-usb/tree/master/host/class/uac/usb_host_uac/examples/audio_player). The beep generator is retained from this project's tested PCM implementation. The LCD uses the installed Adafruit libraries. No Arduino libraries need to be installed manually from ZIP files for USB audio.

The player also provides the enumeration callback when enabled by the core, as required by [Espressif's Arduino USB documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb.html#usb-as-host) for 3.3.11 and later. The tested board-package version remains 3.3.10.

## Music decoding

SD music playback links the installed **libhelix 0.9.4** Arduino library by Phil Schatzmann, using the underlying Helix `MP3InitDecoder` / `MP3Decode` C API. Source: [arduino-libhelix / codec-helix](https://github.com/pschatzmann/arduino-libhelix). The library is an external dependency, not copied into this sketch. Its Arduino wrapper is GPLv3; the underlying RealNetworks decoder carries RPSL/RCSL notices, included in the installed library's `src/libhelix-mp3/` directory. Retain the applicable upstream notices when redistributing dependencies.

`track_decoder.cpp` implements file/tag handling, WAV PCM reading, streaming sample-rate conversion and channel conversion. `music.cpp` walks the SD card incrementally and repeats the tracks. The existing USB transport is retained; the original beep generator is no longer used for playback.


## UAC2 host driver

`src/uac2/` vendors [Avery Levitt's esp-uac2-host](https://github.com/Averyy/esp-uac2-host), commit **a072adb6a2410f9f93474637a483724c93d5fc6f**, component version 0.1.2. Original MIT notices and `src/uac2/LICENSE` are retained. Descriptor definitions retain the upstream attribution to USBX and CherryUSB.

Local changes:

- Adjacent header includes for Arduino's recursive sketch compilation; AS alternate capacity increased from 4 to 16.
- Preserve AS `bmFormats` for PCM validation; filter UAC2 interface protocol; reject malformed descriptor chains; preserve invalid slot sizes for rejection rather than silently clamping them; fix short format-descriptor bounds checking. Include `inttypes.h` explicitly and remove an unused counter for native builds.
- Add exact alternate selection to stream configuration, avoiding ambiguity between equal bit-depth/channel modes with different subslot layouts.
- Add a successful USB TX completion event and surface packet-level errors/rejected submissions. Buffer-demand events alone are not counted as audio delivery.
- Use shared `usb_pcm.c` packet scheduling: exact nominal fractional rates and feedback at integer as well as fractional rates, with plausible-feedback checks and whole-frame packet bounds. Remove the upstream restriction on fractional playback without feedback for adaptive/synchronous sinks.
- Respect the feedback endpoint's advertised 3/4-byte size and packet status. Feedback allocation/start failures fail startup; resubmit failures notify the application to stop.

The application limits UAC2 to a single clock source without multipliers, PCM mono/stereo at 44.1/48 kHz, 1 ms data endpoints, and S3 FIFO-compatible packets. Decoded PCM16 is widened into the negotiated USB container. Original UAC1 selection remains separate. HID handling/logging is inactive in the music application.

`tests/fixtures/minidsp_2x4hd_descriptors.h` is Avery's captured hardware configuration from the same commit, retained under MIT with `tests/fixtures/AVERY_LICENSE`. These descriptors exercise the actual bundled parser and mode validation; they are not evidence of a physical test of this firmware.

The user's [AudioTools USB Audio Class 2.0 wiki reference](https://github.com/pschatzmann/arduino-audio-tools/wiki/USB-Audio-Class-2.0) was reviewed for the Arduino host architecture. Its host implementation is not vendored or linked; the existing Helix dependency is unchanged.

## Larger USB configuration descriptors in Arduino

`src/usb_enum/enum.c` and its four private headers are from [Espressif ESP-IDF v5.5.4](https://github.com/espressif/esp-idf/tree/v5.5.4/components/usb), commit **735507283d5b2f9fb363a1901172dbd9e847945d**. Apache-2.0 notices and `src/usb_enum/LICENSE` are retained.

Local changes: set the enumerator control-data capacity to **2048 bytes** instead of Arduino's compiled 256-byte setting, and add a compile-time ESP-IDF version guard. The sketch object supplies `enum_*` symbols before the precompiled `libusb.a`; the linker map was checked to confirm selection of the local enumerator. The remaining USB core is supplied by Arduino. This avoids modifying global packages or pretending a sketch `#define` can reconfigure a prebuilt library. It is pinned to ESP-IDF 5.5.4 / Arduino ESP32 3.3.10; other core releases require reviewing the private ABI before updating it.
