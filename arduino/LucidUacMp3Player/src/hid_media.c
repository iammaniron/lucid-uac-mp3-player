// Uses the existing observer client; UAC retains ownership of audio interfaces.
// All state and callbacks run on the Arduino task, never the audio driver task.
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "hid_media.h"
#include "hid_consumer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lucid_hid";
typedef struct {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *control, *input;
    lucid_hid_map_t map;
    uint8_t address, interface, endpoint;
    uint16_t packet, descriptor_bytes;
    bool claimed, removing, control_pending, input_pending, control_done, input_done, active;
    unsigned logged_reports;
} hid_t;
static hid_t interfaces[4];
static lucid_hid_report_callback_t report_callback;
void lucid_hid_set_report_callback(lucid_hid_report_callback_t callback) { report_callback=callback; }

static void complete(usb_transfer_t *transfer)
{
    hid_t *h = transfer->context;
    if (transfer == h->control) { h->control_pending = false; h->control_done = true; }
    else { h->input_pending = false; h->input_done = true; }
}
static void remove_interface(hid_t *h)
{
    h->removing = true;
    if (h->input_pending) {
        usb_host_endpoint_halt(h->device, h->endpoint);
        usb_host_endpoint_flush(h->device, h->endpoint);
    }
    // Never cancel endpoint zero: it is also used by the audio driver.
    // On unplug the host cancels it; wait for its callback before freeing.
}
static void attach_interface(usb_host_client_handle_t client, usb_device_handle_t device,
                             uint8_t address, uint8_t interface, uint8_t endpoint,
                             uint16_t packet, uint16_t descriptor_bytes)
{
    if (!endpoint || !packet || packet > 64 || !descriptor_bytes || descriptor_bytes > 2048) {
        ESP_LOGW(TAG, "Unsupported HID interface %u: packet=%u report descriptor=%u", interface, packet, descriptor_bytes);
        return;
    }
    hid_t *h = NULL;
    for (unsigned i = 0; i < 4; ++i) {
        if (interfaces[i].device == device && interfaces[i].interface == interface) return;
        if (!interfaces[i].device && !h) h = &interfaces[i];
    }
    if (!h) { ESP_LOGW(TAG, "HID interface limit reached"); return; }
    *h = (hid_t){.client=client, .device=device, .address=address, .interface=interface,
                 .endpoint=endpoint, .packet=packet, .descriptor_bytes=descriptor_bytes};
    esp_err_t err = usb_host_interface_claim(client, device, interface, 0);
    if (err != ESP_OK) goto failed;
    h->claimed = true;
    err = usb_host_transfer_alloc(8 + descriptor_bytes, 0, &h->control);
    if (err != ESP_OK) goto failed;
    err = usb_host_transfer_alloc(packet, 0, &h->input);
    if (err != ESP_OK) goto failed;
    // Standard GET_DESCRIPTOR(Report), recipient Interface, device-to-host.
    const uint8_t setup[8] = {0x81, 6, 0, 0x22, interface, 0,
                             descriptor_bytes & 255, descriptor_bytes >> 8};
    memcpy(h->control->data_buffer, setup, 8);
    h->control->num_bytes = 8 + descriptor_bytes;
    h->control->device_handle = device;
    h->control->bEndpointAddress = 0;
    h->control->callback = complete;
    h->control->context = h;
    h->input->num_bytes = packet;
    h->input->device_handle = device;
    h->input->bEndpointAddress = endpoint;
    h->input->callback = complete;
    h->input->context = h;
    err = usb_host_transfer_submit_control(client, h->control);
    if (err != ESP_OK) goto failed;
    h->control_pending = true;
    ESP_LOGI(TAG, "Reading HID report descriptor: addr %u interface %u", address, interface);
    return;
failed:
    ESP_LOGW(TAG, "HID interface %u setup: %s (audio continues)", interface, esp_err_to_name(err));
    remove_interface(h);
}

void lucid_hid_attach(usb_host_client_handle_t client, usb_device_handle_t device,
                      uint8_t address, const uint8_t *config, size_t bytes)
{
    bool hid = false, found = false;
    uint8_t interface = 0, endpoint = 0;
    uint16_t packet = 0, descriptor_bytes = 0;
    for (size_t pos = 0; pos < bytes;) {
        if (bytes-pos < 2 || config[pos] < 2 || config[pos] > bytes-pos) break;
        const uint8_t *d = config+pos;
        unsigned len = d[0];
        if (d[1] == 4 && len >= 9) {
            if (hid) attach_interface(client, device, address, interface, endpoint, packet, descriptor_bytes);
            hid = d[5] == 3 && d[3] == 0;
            found |= hid;
            interface = d[2]; endpoint = 0; packet = descriptor_bytes = 0;
        } else if (hid && d[1] == 0x21 && len >= 6) {
            for (unsigned i = 0; i < d[5] && 6+3*i+2 < len; ++i)
                if (d[6+3*i] == 0x22) descriptor_bytes = d[7+3*i] | (d[8+3*i] << 8);
        } else if (hid && d[1] == 5 && len >= 7 && (d[2]&0x80) && (d[3]&3) == 3 && !endpoint) {
            endpoint = d[2]; packet = d[4] | (d[5] << 8);
        }
        pos += len;
    }
    if (hid) attach_interface(client, device, address, interface, endpoint, packet, descriptor_bytes);
    if (!found) ESP_LOGI(TAG, "Device addr %u has no HID button interface", address);
}
void lucid_hid_detach(usb_device_handle_t device)
{
    for (unsigned i = 0; i < 4; ++i)
        if (interfaces[i].device == device) remove_interface(&interfaces[i]);
}
bool lucid_hid_busy(usb_device_handle_t device)
{
    for (unsigned i = 0; i < 4; ++i) if (interfaces[i].device == device) return true;
    return false;
}
void lucid_hid_poll(lucid_media_callback_t callback)
{
    for (unsigned i = 0; i < 4; ++i) {
        hid_t *h = &interfaces[i];
        if (!h->device) continue;
        if (!h->removing && h->control_done) {
            h->control_done = false;
            usb_transfer_t *t = h->control;
            if (t->status != USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes != 8+h->descriptor_bytes ||
                !lucid_hid_parse(&h->map, t->data_buffer+8, h->descriptor_bytes)) {
                ESP_LOGW(TAG, "No supported Consumer media controls on interface %u (USB status %d)", h->interface, t->status);
                remove_interface(h);
            } else {
                h->active = true;
                for (unsigned r = 0; r < h->map.report_count; ++r)
                    if ((h->map.reports[r].bits+7u)/8 + h->map.numbered > h->packet) h->active = false;
                if (!h->active) remove_interface(h);
                else ESP_LOGI(TAG, "Earphone media buttons ready: addr %u interface %u", h->address, h->interface);
            }
        }
        if (!h->removing && h->input_done) {
            h->input_done = false;
            usb_transfer_t *t = h->input;
            if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
                ESP_LOGW(TAG, "HID input error %d; reconnect earphones to retry", t->status);
                remove_interface(h);
            } else {
                // A few raw reports help diagnose headsets using nonstandard buttons.
                if (h->logged_reports++ < 8)
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, t->data_buffer, t->actual_num_bytes, ESP_LOG_INFO);
                uint8_t pressed=0, held=0;
                bool decoded=lucid_hid_input(&h->map, t->data_buffer, t->actual_num_bytes, &pressed, &held);
                // The observer copies into RAM; SD writes happen after re-submission.
                // Include unrecognized/Telephony reports and releases for diagnosis.
                if (report_callback) report_callback(h->address,h->interface,h->endpoint,
                    t->data_buffer,t->actual_num_bytes,decoded,pressed);
                if (decoded)
                    for (unsigned k = 0; k < LUCID_MEDIA_KEYS; ++k)
                        if (pressed & (1u << k)) callback(h->address, lucid_media_usages[k]);
            }
        }
        if (!h->removing && h->active && !h->input_pending) {
            esp_err_t err = usb_host_transfer_submit(h->input);
            if (err == ESP_OK) h->input_pending = true;
            else {
                ESP_LOGW(TAG, "HID input submit: %s", esp_err_to_name(err));
                remove_interface(h);
            }
        }
        if (h->removing && !h->control_pending && !h->input_pending) {
            if (h->claimed) {
                esp_err_t err = usb_host_interface_release(h->client, h->device, h->interface);
                if (err != ESP_OK) continue; // Keep ownership until safe to release.
            }
            usb_host_transfer_free(h->control);
            usb_host_transfer_free(h->input);
            memset(h, 0, sizeof(*h));
        }
    }
}
