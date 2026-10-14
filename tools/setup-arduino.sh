#!/usr/bin/env bash
# Install the toolchain and libraries used by CI. Requires Arduino CLI and Git.
set -euo pipefail
command -v arduino-cli >/dev/null
command -v git >/dev/null
boards_url='https://espressif.github.io/arduino-esp32/package_esp32_index.json'
arduino-cli core update-index --additional-urls "$boards_url"
arduino-cli core install esp32:esp32@3.3.10 --additional-urls "$boards_url"
arduino-cli lib update-index
arduino-cli lib install --no-deps \
  'Adafruit BusIO@1.17.4' \
  'Adafruit GFX Library@1.12.6' \
  'Adafruit ST7735 and ST7789 Library@1.11.0'
# libhelix is installed directly from its pinned upstream v0.9.4 commit.
# Enable Git installation only for this command, without changing global config.
ARDUINO_LIBRARY_ENABLE_UNSAFE_INSTALL=true arduino-cli lib install --git-url \
  'https://github.com/pschatzmann/arduino-libhelix.git#6183305de92c6c8e445aa95116319b64963a6ad3'
