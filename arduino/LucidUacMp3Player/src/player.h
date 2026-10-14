#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Call once from setup(), then poll from Arduino loop().
void lucid_audio_begin(void);
void lucid_audio_poll(void);
unsigned lucid_audio_class(void);
unsigned lucid_audio_bits(void);
unsigned lucid_audio_rate(void);
void lucid_status(const char *status, const char *detail);

#ifdef __cplusplus
}
#endif
