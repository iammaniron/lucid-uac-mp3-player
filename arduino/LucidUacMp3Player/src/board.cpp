#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "../LucidPins.h"
#include "board.h"
#include "player.h"
#include "music.h"

#if LUCID_USE_LCD_SD
static Adafruit_ST7735 lcd(&SPI, LUCID_LCD_CS, LUCID_LCD_DC, LUCID_LCD_RST);
static bool sdReady;
static bool lcdReady;
#endif

void lucid_board_begin()
{
#if LUCID_USE_LCD_SD
  // Deselect both peripherals before any SPI clocks, as in the tested sketch.
  pinMode(LUCID_SD_CS, OUTPUT);
  pinMode(LUCID_LCD_CS, OUTPUT);
  digitalWrite(LUCID_SD_CS, HIGH);
  digitalWrite(LUCID_LCD_CS, HIGH);
  pinMode(LUCID_LCD_BL, OUTPUT);
  // Active-high backlight: steady HIGH is 100% duty, from startup onward.
  digitalWrite(LUCID_LCD_BL, HIGH);
  SPI.begin(LUCID_SCK, LUCID_MISO, LUCID_MOSI);

  // SD must enter SPI mode before initializing the LCD on the shared bus.
  sdReady = SD.begin(LUCID_SD_CS, SPI, 10000000, "/sd", 12, false);
  Serial0.println(sdReady ? "SD ready" : "SD unavailable; insert card and reset");
  pinMode(LUCID_SD_CS, OUTPUT);
  digitalWrite(LUCID_SD_CS, HIGH);
  lcd.initR(INITR_MINI160x80);
  lcd.setRotation(3);
  lcd.setSPISpeed(10000000);
  lcd.setTextWrap(false);
  lcdReady = true;
  lucid_status("STARTING PLAYER", "SD TO USB AUDIO");
  digitalWrite(LUCID_LCD_BL, HIGH);
  lucid_music_scan(sdReady);
#else
  lucid_music_scan(false);
#endif
}

extern "C" void lucid_status(const char *status, const char *detail)
{
  Serial0.printf("%s | %s\n", status, detail);
#if LUCID_USE_LCD_SD
  if (!lcdReady) return;
  digitalWrite(LUCID_SD_CS, HIGH);
  lcd.fillScreen(ST77XX_BLACK);
  lcd.setTextColor(ST77XX_CYAN);
  lcd.setTextSize(2);
  lcd.setCursor(4, 3);
  lcd.print("LUCID MP3");
  lcd.setTextSize(1);
  lcd.setTextColor(ST77XX_WHITE);
  lcd.setCursor(4, 27);
  lcd.print(status);
  lcd.setTextColor(ST77XX_GREEN);
  lcd.setCursor(4, 43);
  lcd.print(detail);
  lcd.setTextColor(0x8410);
  lcd.setCursor(4, 65);
  lcd.print(sdReady ? "SD READY" : "SD NOT READY");
#endif
}

extern "C" void lucid_music_progress(unsigned transfers, unsigned errors)
{
  (void)transfers;
#if LUCID_USE_LCD_SD
  if (!lcdReady) return;
  // Update only the footer while streaming to leave the song name visible
  // and keep SPI traffic below the USB audio buffer's margin.
  digitalWrite(LUCID_SD_CS, HIGH);
  lcd.fillRect(0, 63, 160, 17, ST77XX_BLACK);
  lcd.setTextSize(1);
  lcd.setTextColor(ST77XX_WHITE);
  lcd.setCursor(4, 65);
  lcd.printf("UAC%u %s %ub V%u E%u", lucid_audio_class(),
    lucid_audio_rate() == 44100 ? "44.1K" : "48K", lucid_audio_bits(),
    lucid_music_volume(), errors);
#endif
}
