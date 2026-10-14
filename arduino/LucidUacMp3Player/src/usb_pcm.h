#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "uac2/uac2_desc.h"

// Pure helpers shared by the player/USB scheduler and native tests.
bool lucid_uac2_mode_ok(const uac2_as_iface_t *as, uint8_t interface, uint32_t rate);
bool lucid_rate_in_range(uint32_t rate, uint32_t min, uint32_t max, uint32_t step);
size_t lucid_pack_pcm16(uint8_t *out, const uint8_t *in, size_t samples, unsigned slot);
uint16_t lucid_usb_packet_bytes(uint32_t rate, uint32_t feedback, unsigned frame_bytes,
    unsigned max_packet, uint32_t *nominal_phase, uint32_t *feedback_phase);
