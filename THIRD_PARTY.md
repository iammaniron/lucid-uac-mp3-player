# Licensing and third-party components

The root [MIT license](LICENSE) covers original Lucid application code, tests, documentation, and build scripts unless a file carries another notice. It does not replace licenses on vendored or external dependencies. Upstream copyright/license notices are retained.

The Arduino music build uses external libhelix v0.9.4. Its wrapper carries GPLv3 terms, and the underlying Helix MP3 decoder has RealNetworks RPSL/RCSL notices. It is not vendored here. This repository publishes source, not a blanket MIT license for the linked firmware or its dependencies. See the [Arduino dependency details](arduino/LucidUacMp3Player/THIRD_PARTY.md#music-decoding) before redistributing a compiled build.

The following upstream components are vendored under `components/`, with their original notices and licenses retained. No driver source changes were made. They were included locally to make this ESP-IDF 5.5.5 build independent of registry availability.

The Arduino sketch has its own bundled UAC1/UAC2 drivers and a version-pinned USB enumeration override, with packaging changes documented in [its third-party notice](arduino/LucidUacMp3Player/THIRD_PARTY.md). Its LCD uses the installed Adafruit Arduino libraries.

| Directory | Version | Upstream | License file |
| --- | --- | --- | --- |
| `components/usb_host_uac` | 1.5.0 | [Espressif USB host UAC](https://components.espressif.com/components/espressif/usb_host_uac/versions/1.5.0) | `components/usb_host_uac/LICENSE` |
| `components/usb` | 1.5.0 | [Espressif USB host](https://components.espressif.com/components/espressif/usb/versions/1.5.0) | `components/usb/LICENSE` |
| `components/cmake_utilities` | 0.5.3 | [Espressif CMake utilities](https://components.espressif.com/components/espressif/cmake_utilities/versions/0.5.3) | `components/cmake_utilities/license.txt` |

UAC package source commit: `6d24137e14a4f6c8138662a7a074be16a899c2c6`, path `host/class/uac/usb_host_uac`, as recorded in its manifest. USB and CMake utilities were taken from the installed Espressif component cache; their manifests retain version metadata. Local components take precedence in the build. The generated `dependencies.lock` contains machine-specific local paths and is ignored.

The ST7735R initialization register values and mini 160×80 rotation/offset profile in `main/lucid_board.c` are adapted from [Adafruit_ST7735.cpp](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/Adafruit_ST7735.cpp). The original attribution/notice from the library is retained in `licenses/Adafruit-ST7735-NOTICE.txt`. Adafruit identifies this library as MIT licensed. The application uses native ESP-IDF SPI calls rather than compiling the Arduino display library.

Lucid wiring, panel selection, and SD-before-LCD initialization are based on the user's existing `lucid.ino`, with the S3 pin mapping supplied in this conversation.
