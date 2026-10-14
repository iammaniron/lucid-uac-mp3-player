#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void *usb_host_client_handle_t;
typedef void *usb_device_handle_t;
typedef int esp_err_t;
#define ESP_OK 0
#define USB_TRANSFER_STATUS_COMPLETED 0
#define USB_TRANSFER_STATUS_CANCELED 1
typedef struct usb_transfer {
    uint8_t *data_buffer;
    int num_bytes, actual_num_bytes, status;
    usb_device_handle_t device_handle;
    uint8_t bEndpointAddress;
    void (*callback)(struct usb_transfer *);
    void *context;
    bool pending; // Test-only ownership assertion.
} usb_transfer_t;
const char *esp_err_to_name(esp_err_t error);
esp_err_t usb_host_interface_claim(usb_host_client_handle_t, usb_device_handle_t, uint8_t, uint8_t);
esp_err_t usb_host_interface_release(usb_host_client_handle_t, usb_device_handle_t, uint8_t);
esp_err_t usb_host_transfer_alloc(size_t, int, usb_transfer_t **);
esp_err_t usb_host_transfer_free(usb_transfer_t *);
esp_err_t usb_host_transfer_submit(usb_transfer_t *);
esp_err_t usb_host_transfer_submit_control(usb_host_client_handle_t, usb_transfer_t *);
esp_err_t usb_host_endpoint_halt(usb_device_handle_t, uint8_t);
esp_err_t usb_host_endpoint_flush(usb_device_handle_t, uint8_t);
