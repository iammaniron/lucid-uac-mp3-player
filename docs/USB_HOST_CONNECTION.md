# Glyph battery power and USB-C earphones

The original battery-powered Glyph → USB-C earphones connection stayed at `CONNECT USB DAC`. A subsequent test using 5 V from a bench supply confirmed that USB beep playback works. SD playback needs the same powered host arrangement.

The published Glyph circuit contains no 5 V boost supply for USB VBUS. It also connects both USB-C CC pins to ground through 5.1 kΩ resistors, advertising a sink/device connection. Digital earphones need a USB host that supplies power and appropriate Type-C source signaling.

This follows from the [schematic linked in PCB Cupid's S3 pinout documentation](https://files.pcbcupid.com/Documentation/Boards/glyph-S3/S3%20schematic.pdf). Actual earphone model/UAC version and the voltage at your connector have not been measured. The schematic establishes the missing host-power capability; it does not establish the earphones' protocol compatibility.

## Working connection requirements

```text
Glyph native USB data ── powered USB 2.0 host connection ── digital earphones
                                      │
                           regulated 5 V + common GND
                           proper downstream Type-C CC
```

For a bench test, use a USB host breakout/adapter arrangement explicitly designed to route **D+/D−**, supply downstream **5 V VBUS**, and present the correct **source-side CC termination** to USB-C earphones. Supply Glyph USB/VBUS and the host setup from one regulated 5 V source; disconnect the computer before connecting this external supply. Keep 5 V off BAT, 3.3 V, and all GPIOs. The Glyph's native data pair is GPIO19 D− / GPIO20 D+ and is already wired to its USB-C connector.

A generic C-to-C cable, charging splitter, or powered hub is not by itself evidence of correct power/data/CC routing. In particular, an external hub's power brick does not guarantee valid upstream VBUS at the Glyph side. Check the specific wiring of the chosen host adapter before connecting it.

For a portable battery player, the hardware needs a battery-to-5 V boost supply, a downstream USB host power path, and USB-C source termination. Do not simply feed a boost converter from the battery back into the Glyph USB rail: the board's battery charger is on that rail, so this needs a properly designed power path rather than a charger/boost loop. A dedicated host-power board or a revised PCB can provide it.

## Earphone compatibility

Digital USB-C earphones contain a USB audio device/DAC. The music player includes Espressif UAC1 and Avery UAC2 host drivers. It selects mono/stereo PCM at 44.1/48 kHz; UAC2 can use 16-, 24-, or 32-bit transport. See the [compatibility limits](../arduino/LucidUacMp3Player/README.md#supported-playback). Passive analog USB-C earphones contain no enumerating USB audio device and cannot be driven by this UAC firmware.

The music sketch automatically selects UAC1 or UAC2 and shows the selected format on the LCD. If nothing enumerates, first establish power and a valid USB data/source connection. Connecting the earphones to a computer and checking whether they appear as a USB sound device can help identify them; it does not alone prove UAC1 compatibility.

## Why the linked device example does not fix this

Espressif's [USB Device UAC reference](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_device/usb_device_uac.html) makes the **ESP32 an audio device for a computer host**. Connecting that firmware to USB-C earphones would place two USB devices together. The required role here remains **ESP32 USB host → earphone USB audio device**, using the [UAC host example](https://github.com/espressif/esp-usb/tree/master/host/class/uac/usb_host_uac/examples/audio_player).
