#pragma once
#include <stddef.h>
#include <stdint.h>
#include <libhelix-mp3/mp3dec.h>

// The same streaming decoder is exercised with host files in tests and SD File
// callbacks on the ESP32. No whole-track buffer or PSRAM is required.
struct TrackInput {
    void *context;
    size_t (*read)(void *, uint8_t *, size_t);
    bool (*seek)(void *, uint32_t);
    uint32_t size;
};

class TrackDecoder {
public:
    ~TrackDecoder() { close(); }
    bool open(TrackInput input, bool wav);
    void close();
    size_t render(uint8_t *out, size_t frames, unsigned rate, unsigned channels, unsigned volume);
    unsigned sampleRate() const { return sourceRate; }
    const char *error() const { return failure; }
private:
    TrackInput input{};
    HMP3Decoder mp3 = nullptr;
    uint8_t compressed[8192]{};
    int16_t decoded[2304]{};
    size_t buffered = 0, pcmCount = 0, pcmIndex = 0;
    uint32_t cursor = 0, end = 0, phase = 0;
    unsigned sourceRate = 0, sourceChannels = 0;
    bool wave = false, primed = false, nextAvailable = false, finished = true;
    int16_t a[2]{}, b[2]{};
    const char *failure = "";
    bool readExact(uint8_t *out, size_t count);
    bool seek(uint32_t position);
    bool openWave();
    bool openMp3();
    bool decodeFrame();
    bool sourceSample(int16_t sample[2]);
};
