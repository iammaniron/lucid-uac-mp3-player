// Three guided earphone button presses -> /file.txt on the SD card.
// ESP32S3 Dev Module, USB-OTG (TinyUSB), CDC/MSC/DFU On Boot DISABLED.
// No Serial Monitor needed. Previous flash captures are recovered onto SD.
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "src/capture.h"
#include "src/session.h"
#include "src/sd_log.h"
#include "src/capture_text.h"

#if !defined(CONFIG_IDF_TARGET_ESP32S3) || ARDUINO_USB_MODE
#error "Select ESP32S3 Dev Module and USB Mode: USB-OTG (TinyUSB)."
#endif
#if ARDUINO_USB_CDC_ON_BOOT || ARDUINO_USB_MSC_ON_BOOT || ARDUINO_USB_DFU_ON_BOOT
#error "Disable CDC/MSC/DFU On Boot. The USB port is needed to host the earphones."
#endif

static constexpr int LED_PIN = 21; // Glyph schematic: GPIO -> resistor -> LED -> GND.
static Adafruit_ST7735 lcd(&SPI, 43, 18, 17);
static ClickGroups clicks;
static SdCaptureLog sdLog;
static bool storageFailed, sawDevice, baselineSaved;
static uint32_t attachedAt, feedbackAt, lastDraw, lastSync, bootHeld;
static unsigned feedbackCount;
static char statusText[96] = "CONNECT EARPHONES";

Print &capture_log() { return sdLog; }
void capture_preview(const char *text) {
  snprintf(statusText, sizeof(statusText), "%s", text);
  if (strstr(text,"DISCONNECTED") || strstr(text,"USB ERROR")) clicks.connected=false;
}
void capture_device_ready(unsigned interfaces) {
  sawDevice=true; bool haveInputs=interfaces!=0; attachedAt=millis(); baselineSaved=false;
  if (haveInputs) clicks.attach(attachedAt);
  capture_preview(haveInputs ? "WAIT: HANDS OFF 3 SEC" : "NO INTERRUPT INPUT\nHold BOOT to save");
}
void capture_descriptor(uint8_t kind, uint8_t addr, uint8_t iface, const uint8_t *data, size_t bytes) {
  capture_descriptor_text(sdLog,kind,addr,iface,data,bytes);
}
void capture_report(uint8_t addr, uint8_t iface, uint8_t klass, uint8_t ep,
                    const uint8_t *data, size_t bytes, bool changed) {
  unsigned group=clicks.input(millis(),changed);
  capture_raw_text(sdLog,group,capture_button_label(group),millis(),addr,iface,klass,ep,data,bytes);
}

static void led(bool on) { digitalWrite(LED_PIN,on ? HIGH : LOW); }
static void screen(const char *text) {
  digitalWrite(44,HIGH);
  lcd.fillRect(0,17,160,48,ST77XX_BLACK);
  lcd.setCursor(3,19); lcd.setTextColor(ST77XX_WHITE); lcd.setTextWrap(true); lcd.print(text);
}
static bool syncLog(bool reopen=true) {
  if (!sdLog.checkpoint(reopen)) {
    storageFailed=true;
    screen("SD WRITE/VERIFY ERROR\nCheck card, then reset");
    return false;
  }
  lastSync=millis();
  return true;
}
static bool checkpoint(const char *reason, bool finished) {
  sdLog.printf("CHECKPOINT t_ms=%lu groups=%u/3 finished=%u reason=%s\n",
      millis(),clicks.completed,unsigned(finished),reason);
  return syncLog(!finished);
}
static void sleepUntilReset() {
  // Deep sleep cannot cut the board's 5V supply or switch earphone power off.
  led(false); digitalWrite(16,LOW);
  gpio_hold_en(GPIO_NUM_21); gpio_hold_en(GPIO_NUM_16);
  gpio_deep_sleep_hold_en();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
}
static void finish(const char *reason) {
  if (!checkpoint(reason,true)) return;
  SD.end(); // All file handles are closed before the completion indication.
  screen(clicks.completed==3 ? "SAVED: /file.txt\nPower off, read card" : "PARTIAL: /file.txt\nPower off, read card");
  // Final three-flash acknowledgement is shown only after verified SD storage.
  for (unsigned i=0;i<3;++i) { led(true); delay(150); led(false); delay(150); }
  led(true); delay(2000);
  sleepUntilReset();
}
static bool recoverPreviousCapture() {
  Preferences flash;
  if (!flash.begin("lucid-btn",false)) {
    sdLog.println("Previous flash namespace unavailable; starting a fresh SD test.");
    return true;
  }
  if (flash.isKey("capture")) {
    static SavedCapture previous;
    bool valid=flash.getBytesLength("capture")==sizeof(previous) &&
        flash.getBytes("capture",&previous,sizeof(previous))==sizeof(previous) && previous.valid();
    if (valid) {
      capture_recovered_text(sdLog,previous);
      // Retain the old flash record until its text has been closed and read back.
      if (!syncLog()) { flash.end(); return false; }
      if (!flash.remove("capture")) sdLog.println("Old flash log retained: it may be exported again on the next boot.");
      screen("OLD LOG COPIED TO SD\nStarting a new test");
      delay(1500);
    } else sdLog.println("Previous flash capture invalid/incompatible; retained unchanged.");
  }
  flash.end();
  return true;
}
void setup() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_21); gpio_hold_dis(GPIO_NUM_16);
  pinMode(LED_PIN,OUTPUT); led(false);
  pinMode(0,INPUT_PULLUP);
  pinMode(44,OUTPUT); digitalWrite(44,HIGH);
  pinMode(43,OUTPUT); digitalWrite(43,HIGH);
  pinMode(16,OUTPUT); digitalWrite(16,HIGH);
  SPI.begin(35,37,36);
  bool sdReady=SD.begin(44,SPI,10000000,"/sd",12,false);
  digitalWrite(44,HIGH);
  lcd.initR(INITR_MINI160x80); lcd.setRotation(3); lcd.setSPISpeed(10000000);
  lcd.fillScreen(ST77XX_BLACK); lcd.setTextColor(ST77XX_CYAN);
  lcd.setTextSize(1); lcd.setCursor(3,3); lcd.print("USB 3-CLICK CAPTURE");
  lcd.setCursor(3,68); lcd.print("SD LOG: /file.txt");
  if (!sdReady || !sdLog.begin()) {
    storageFailed=true; screen("SD NOT READY\nInsert card, then reset"); return;
  }
  if (!recoverPreviousCapture()) return;
  sdLog.println("\n=== NEW SD BUTTON CAPTURE v4 ===");
  sdLog.println("Expected order: 1=CENTRE/PAUSE, 2=VOLUME +, 3=VOLUME -.");
  sdLog.println("Names are prompted test labels, NOT decoded button identities. Note any different order you used.");
  sdLog.println("t_ms is milliseconds since this boot, not wall-clock time. Group 0 is startup/between-click traffic.");
  sdLog.println("Every received interrupt report, including release/unchanged reports, is written below.");
  if (!syncLog()) return;
  screen("CONNECT EARPHONES\nWait for slow blink");
  capture_begin(); // This sketch always hosts the earphones; no USB Serial replay.
}
void loop() {
  const uint32_t now=millis();
  if (storageFailed) { led((now/100)%2); delay(10); return; }
  capture_poll();
  // Flush setup data and each completed click before acknowledging with the LED.
  if (sawDevice && !baselineSaved && millis()-attachedAt>=3000) {
    baselineSaved=checkpoint("Setup saved; waiting for clicks",false);
  }
  if (storageFailed) return;
  if (sdLog.error()) { storageFailed=true; screen("SD WRITE FAILED\nCheck card, then reset"); return; }
  if (clicks.poll(millis())) {
    if (clicks.completed==3) { finish("Three click groups captured"); return; }
    if (!checkpoint("Click saved; capture in progress",false)) return;
    feedbackAt=millis(); feedbackCount=clicks.completed;
  }
  // Also preserve ongoing/partial input without waiting for all three clicks.
  if (millis()-lastSync>=2000 && !syncLog()) return;
  // Partial log escape for an earphone that doesn't send button reports.
  if (!digitalRead(0)) {
    if (!bootHeld) bootHeld=millis();
    if (millis()-bootHeld>=2000) { finish("Manual partial capture (BOOT held)"); return; }
  } else bootHeld=0;
  if (sawDevice && millis()-attachedAt>=90000) { finish("90-second timeout: partial capture"); return; }
  uint32_t feedbackAge=millis()-feedbackAt;
  if (feedbackCount && feedbackAge<feedbackCount*300) led(feedbackAge%300<150);
  else if (clicks.active) led(true);
  else if (clicks.ready(millis())) led(millis()%1000<150);
  else led(false);
  if (millis()-lastDraw>=150) {
    lastDraw=millis(); char text[96];
    if (feedbackCount && feedbackAge<feedbackCount*300+600)
      snprintf(text,sizeof(text),"CLICK %u SAVED\nWait for slow blink",feedbackCount);
    else if (clicks.active) snprintf(text,sizeof(text),"CLICK %u: RELEASE NOW\n%s",clicks.completed+1,capture_button_label(clicks.completed+1));
    else if (clicks.ready(millis())) snprintf(text,sizeof(text),"READY: CLICK %u OF 3\n%s",clicks.completed+1,capture_button_label(clicks.completed+1));
    else if (clicks.connected) snprintf(text,sizeof(text),"WAIT FOR SLOW BLINK\nKeep buttons released");
    else snprintf(text,sizeof(text),"%s",statusText);
    screen(text);
  }
  delay(1);
}
