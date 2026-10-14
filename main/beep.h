#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t rate;
    uint32_t position;
    unsigned channels;
} beep_t;

// Signed PCM16 little endian: 880 Hz for 250 ms, then 750 ms silence.
// Call with complete frames; the phase and envelope continue across calls.
void beep_fill(beep_t *tone, uint8_t *out, size_t frames);
