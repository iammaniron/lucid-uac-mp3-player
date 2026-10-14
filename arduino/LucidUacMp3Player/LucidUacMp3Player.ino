// SD music player: open in Arduino IDE and keep the whole folder together.
// Board: ESP32S3 Dev Module, Espressif esp32 core 3.3.10.
// USB CDC On Boot: Disabled; USB Mode: USB-OTG (TinyUSB); PSRAM: Disabled.
// The src/ folder contains the bundled UAC1 and UAC2 host drivers.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <MP3DecoderHelix.h> // Select installed libhelix; decoder uses its C API.
#include "esp_log.h"
#include "LucidPins.h"
#include "src/player.h"
#include "src/board.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module in Tools > Board."
#endif
#if ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot to Disabled. USB is needed for the DAC."
#endif
#if ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Disable USB MSC On Boot and USB DFU On Boot."
#endif

void setup()
{
  // GPIO43/44 are occupied by Lucid. Optional logs use a separate UART adapter.
  Serial0.begin(115200, SERIAL_8N1, 14, 13);
  esp_log_level_set("lucid_audio", ESP_LOG_INFO);
  esp_log_level_set("uac2-host", ESP_LOG_INFO);
  Serial0.println("Lucid SD music player starting...");
  lucid_board_begin();
  lucid_audio_begin();
}

void loop()
{
  // Poll enumeration, audio and display state. USB work runs in background tasks.
  lucid_audio_poll();
  delay(1);
}
