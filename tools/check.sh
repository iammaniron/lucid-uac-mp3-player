#!/usr/bin/env bash
# Build the music sketch and run native tests; leave the installed hardware alone.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=default,PSRAM=disabled,FlashSize=4M \
  --build-path "$project_dir/build-arduino" \
  arduino/LucidUacMp3Player
python3 -m unittest discover -s tests -v
