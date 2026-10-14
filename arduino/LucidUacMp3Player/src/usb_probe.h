#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool valid;
    bool audio;
    bool uac1_output;
    bool uac2;
    bool hub;
} lucid_usb_probe_t;

// Inspect an enumerated device even when the UAC driver cannot open it.
lucid_usb_probe_t lucid_usb_probe(const uint8_t *bytes, size_t length);
