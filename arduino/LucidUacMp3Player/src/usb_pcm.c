#include "usb_pcm.h"

bool lucid_rate_in_range(uint32_t rate, uint32_t min, uint32_t max, uint32_t step)
{
    return rate >= min && rate <= max && (!step || (rate - min) % step == 0);
}

bool lucid_uac2_mode_ok(const uac2_as_iface_t *as, uint8_t interface, uint32_t rate)
{
    if (!as || as->interface_num != interface || !as->alt_setting ||
        as->format_type != UAC2_FORMAT_TYPE_I || as->formats != UAC2_FORMAT_PCM ||
        !as->ep_addr || (as->ep_addr & 0x80) || (as->ep_attributes & 3) != 1 ||
        (as->ep_attributes & 0x30) || as->ep_interval != 1 ||
        (as->nr_channels != 1 && as->nr_channels != 2) ||
        (as->bit_resolution != 16 && as->bit_resolution != 24 && as->bit_resolution != 32) ||
        as->sub_slot_size < 2 || as->sub_slot_size > 4 ||
        as->bit_resolution > as->sub_slot_size * 8 ||
        (rate != 48000 && rate != 44100)) return false;
    // Asynchronous sinks need explicit feedback. Implicit feedback would
    // require a second audio stream, which this S3 playback build cannot run.
    if (((as->ep_attributes >> 2) & 3) == 1 && !as->fb_ep_addr) return false;
    if (as->fb_ep_addr && (!(as->fb_ep_addr & 0x80) ||
        as->fb_ep_max_packet_size < 3 || as->fb_ep_max_packet_size > 4 ||
        !as->fb_ep_interval || as->fb_ep_interval > 16)) return false;
    unsigned frames = (rate + 999) / 1000;
    // Leave a whole extra frame for asynchronous clock drift at integer rates.
    if (as->fb_ep_addr && rate % 1000 == 0) ++frames;
    return as->ep_max_packet_size >= frames * as->nr_channels * as->sub_slot_size &&
        as->ep_max_packet_size <= 600; // S3 periodic-OUT FIFO budget.
}

size_t lucid_pack_pcm16(uint8_t *out, const uint8_t *in, size_t samples, unsigned slot)
{
    if (!out || !in || slot < 2 || slot > 4) return 0;
    // USB PCM is signed little endian, left justified in its subslot. Widen
    // with zero low bits; this changes transport width, not source resolution.
    // Walk backwards so input/output may be the same buffer.
    for (size_t i = samples; i-- > 0;) {
        uint8_t lo = in[i * 2], hi = in[i * 2 + 1];
        for (unsigned j = 0; j < slot - 2; ++j) out[i * slot + j] = 0;
        out[i * slot + slot - 2] = lo;
        out[i * slot + slot - 1] = hi;
    }
    return samples * slot;
}

uint16_t lucid_usb_packet_bytes(uint32_t rate, uint32_t feedback, unsigned frame_bytes,
    unsigned max_packet, uint32_t *nominal_phase, uint32_t *feedback_phase)
{
    if (!frame_bytes || !rate || !nominal_phase || !feedback_phase) return 0;
    uint32_t nominal = (uint32_t)(((uint64_t)rate << 16) / 1000);
    uint32_t frames;
    // Ignore zero, corrupt or implausible feedback; never split a PCM frame.
    if (feedback >= nominal - nominal / 10 && feedback <= nominal + nominal / 10) {
        uint64_t phase = (uint64_t)*feedback_phase + feedback;
        frames = (uint32_t)(phase >> 16);
        *feedback_phase = phase & 0xffff;
        *nominal_phase = 0;
    } else {
        *nominal_phase += rate;
        frames = *nominal_phase / 1000;
        *nominal_phase %= 1000;
        *feedback_phase = 0;
    }
    if (frames > max_packet / frame_bytes) frames = max_packet / frame_bytes;
    return frames * frame_bytes;
}
