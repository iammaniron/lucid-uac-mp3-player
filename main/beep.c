#include "beep.h"
#include <math.h>

void beep_fill(beep_t *tone, uint8_t *out, size_t frames)
{
    const uint32_t on_frames = tone->rate / 4;
    const uint32_t ramp_frames = tone->rate / 200; // 5 ms click suppression
    for (size_t i = 0; i < frames; ++i) {
        const uint32_t p = tone->position;
        int16_t sample = 0;
        if (p < on_frames) {
            float gain = 1.0f;
            if (p < ramp_frames) gain = (float)p / ramp_frames;
            if (on_frames - 1 - p < ramp_frames)
                gain = (float)(on_frames - 1 - p) / ramp_frames;
            sample = (int16_t)(2621.0f * gain *
                sinf(6.28318530718f * 880.0f * (float)p / tone->rate));
        }
        for (unsigned ch = 0; ch < tone->channels; ++ch) {
            *out++ = (uint16_t)sample & 0xff;
            *out++ = (uint16_t)sample >> 8;
        }
        tone->position = (p + 1) % tone->rate;
    }
}
