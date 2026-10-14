#pragma once
#include <Arduino.h>
void capture_begin();
void capture_poll();
Print &capture_log();
void capture_preview(const char *text);
void capture_device_ready(unsigned interfaces);
void capture_descriptor(uint8_t type, uint8_t address, uint8_t interface,
                        const uint8_t *data, size_t bytes);
void capture_report(uint8_t address, uint8_t interface, uint8_t klass,
                    uint8_t endpoint, const uint8_t *data, size_t bytes, bool changed);
