#pragma once
#include "usb/usb_host.h"
typedef void (*lucid_media_callback_t)(uint8_t address, uint16_t usage);
typedef void (*lucid_hid_report_callback_t)(uint8_t address, uint8_t interface,
    uint8_t endpoint, const uint8_t *data, size_t bytes, bool decoded, uint8_t pressed);
void lucid_hid_set_report_callback(lucid_hid_report_callback_t callback);
void lucid_hid_attach(usb_host_client_handle_t client, usb_device_handle_t device,
                      uint8_t address, const uint8_t *config, size_t bytes);
void lucid_hid_detach(usb_device_handle_t device);
bool lucid_hid_busy(usb_device_handle_t device);
void lucid_hid_poll(lucid_media_callback_t callback);
