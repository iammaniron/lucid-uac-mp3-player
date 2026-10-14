# Contributing

The main application is `arduino/LucidUacMp3Player`. The root ESP-IDF project is a retained UAC1 beep example; it is not the music player.

## Build and test

Install Arduino CLI 1.5.1, Git, Python 3, a C/C++ compiler, and ffmpeg. Then:

```bash
bash tools/setup-arduino.sh
bash tools/check.sh
```

The setup script installs ESP32 core 3.3.10, the pinned Adafruit libraries, and libhelix v0.9.4. It updates the user's Arduino installation. For an existing installation, the same versions can be selected manually using the [Arduino instructions](arduino/LucidUacMp3Player/README.md).

The native suite generates its own audio fixtures in temporary folders. It exercises actual Helix decoding, WAV rendering, resampling, playlist wraparound, malformed descriptors, PCM packing, packet timing, and the retained button diagnostics. It does not flash a board or establish physical DAC compatibility. To run only native tests:

```bash
python3 -m unittest discover -s tests -v
```

If Arduino CLI cannot locate libhelix, set `HELIX_DIR` to that library's `src` directory.

## Changes and pull requests

- Keep a pull request focused on one behavior and explain the problem it solves.
- Include the build/test results and distinguish native tests from physical listening tests.
- Add regression coverage for decoder, descriptor, timing, or lifecycle bugs when practical.
- Preserve the shared SPI ownership: SD and LCD operations run in the application task. USB callbacks must remain nonblocking.
- Keep upstream license headers. Document vendored driver changes and source revisions in the relevant `THIRD_PARTY.md`.
- The local USB enumerator uses private ESP-IDF 5.5.4 interfaces. A core upgrade requires reviewing that ABI and testing enumeration, playback, and reconnects.
- Do not commit build output, music files, credentials, or personal capture logs.

Contributions of original project code are accepted under the [MIT license](LICENSE). Upstream components retain their existing terms.

## Hardware reports

Include the board/core version, DAC model and VID/PID if known, UAC version, power/adapter arrangement, displayed status, and exact steps to reproduce. For new DAC support, test a track transition, playlist wrap, and unplug/reconnect. The standalone button sketch is experimental; media buttons are currently disabled in the player.
