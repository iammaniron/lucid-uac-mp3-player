#pragma once

// Your Lucid-to-Glyph S3 wiring. These are GPIO numbers.
#define LUCID_SCK     35
#define LUCID_MOSI    36
#define LUCID_MISO    37
#define LUCID_SD_CS   44  // CS1
#define LUCID_LCD_CS  43  // CS2
#define LUCID_LCD_DC  18
#define LUCID_LCD_RST 17
#define LUCID_LCD_BL  16

// Set to 0 for a USB-only test without the Lucid board connected.
#define LUCID_USE_LCD_SD 1

// Software volume 0..100. Earphone controls are disabled in the music build.
// The DAC also receives the working optional -6 dB request.
#define LUCID_MUSIC_VOLUME 50

// Retained diagnostic module; music playback does not install HID or write logs.
#ifndef LUCID_BUTTON_TRACE
#define LUCID_BUTTON_TRACE 0
#endif
