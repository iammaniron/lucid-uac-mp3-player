# Changelog

## Unreleased

Initial public source snapshot:

- Arduino SD music player for Glyph ESP32-S3 and Lucid LCD/microSD wiring.
- Automatic UAC1/UAC2 host-driver selection, MP3/PCM16 WAV playback, and playlist repeat.
- UAC2 PCM container packing, clock negotiation, feedback-aware packet scheduling, and larger USB descriptor support.
- Full LCD brightness and playback/USB status footer.
- Native regression tests, pinned Arduino setup, and a GitHub Actions build/test workflow.
- Retained standalone button capture sketch and legacy ESP-IDF UAC1 beep example.

Hardware status: the original UAC1 beep setup was confirmed working with external 5 V power. The combined UAC1/UAC2 music revision still needs physical validation. Earphone buttons remain disabled in the music player.
