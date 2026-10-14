#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// USB HID Consumer page (0x0c). Bits follow this table, never device byte layout.
enum { LUCID_MEDIA_KEYS = 8, LUCID_HID_REPORTS = 16, LUCID_HID_FIELDS = 64 };
extern const uint16_t lucid_media_usages[LUCID_MEDIA_KEYS];
typedef struct {
    uint16_t offset, count;
    uint32_t value;
    uint8_t size, report, key;
    bool array;
} lucid_hid_field_t;
typedef struct {
    uint16_t bits;
    uint8_t id, held;
} lucid_hid_report_t;
typedef struct {
    lucid_hid_field_t fields[LUCID_HID_FIELDS];
    lucid_hid_report_t reports[LUCID_HID_REPORTS];
    unsigned field_count, report_count;
    bool numbered;
} lucid_hid_map_t;
bool lucid_hid_parse(lucid_hid_map_t *map, const uint8_t *descriptor, size_t bytes);
// False for truncated/unknown reports; these must not change held-key state.
bool lucid_hid_input(lucid_hid_map_t *map, const uint8_t *data, size_t bytes,
                     uint8_t *pressed, uint8_t *held);
