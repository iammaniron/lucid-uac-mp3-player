#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void lucid_music_scan(bool sd_ready);
bool lucid_music_available(void);
void lucid_music_start(unsigned output_rate, unsigned output_channels);
void lucid_music_stop(void);
size_t lucid_music_read(uint8_t *out, size_t frames);
void lucid_music_progress(unsigned transfers, unsigned errors);
// Standard USB HID Consumer usages (play/pause, next/previous, volume, mute).
void lucid_music_control(uint16_t usage);
unsigned lucid_music_volume(void);
bool lucid_music_paused(void);
bool lucid_music_muted(void);
#ifdef __cplusplus
}
#endif
