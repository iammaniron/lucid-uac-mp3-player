#include "track_decoder.h"
#include <algorithm>
#include <string.h>

static uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }

void TrackDecoder::close()
{
    if (mp3) MP3FreeDecoder(mp3);
    mp3 = nullptr;
    buffered = pcmCount = pcmIndex = 0;
    sourceRate = sourceChannels = phase = 0;
    primed = nextAvailable = false;
    finished = true;
}

bool TrackDecoder::seek(uint32_t position)
{
    if (position > input.size || !input.seek(input.context, position)) return false;
    cursor = position;
    return true;
}

bool TrackDecoder::readExact(uint8_t *out, size_t count)
{
    while (count) {
        size_t got = input.read(input.context, out, count);
        if (!got || got > count) return false;
        cursor += got;
        out += got;
        count -= got;
    }
    return true;
}

bool TrackDecoder::open(TrackInput reader, bool wav)
{
    close();
    input = reader;
    end = input.size;
    wave = wav;
    failure = "BAD AUDIO FILE";
    if (!input.read || !input.seek || !seek(0)) return false;
    if (!(wave ? openWave() : openMp3())) return false;
    if (!sourceSample(a)) return false;
    nextAvailable = sourceSample(b);
    if (!nextAvailable) { b[0] = a[0]; b[1] = a[1]; }
    primed = true;
    finished = false;
    failure = "";
    return true;
}

bool TrackDecoder::openWave()
{
    uint8_t header[16];
    if (!readExact(header, 12) || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) return false;
    uint64_t declaredEnd = uint64_t(le32(header + 4)) + 8;
    if (declaredEnd > input.size || declaredEnd < 12) return false;
    uint32_t riffEnd = declaredEnd;
    bool formatFound = false;
    for (unsigned chunks = 0; chunks < 256 && cursor + 8 <= riffEnd; ++chunks) {
        if (!readExact(header, 8)) return false;
        uint32_t length = le32(header + 4), start = cursor;
        if (uint64_t(start) + length > riffEnd) return false;
        if (!memcmp(header, "fmt ", 4)) {
            if (length < 16 || !readExact(header, 16)) return false;
            sourceChannels = le16(header + 2);
            sourceRate = le32(header + 4);
            if (le16(header) != 1 || le16(header + 14) != 16 ||
                (sourceChannels != 1 && sourceChannels != 2) ||
                sourceRate < 8000 || sourceRate > 48000 || le16(header + 12) != sourceChannels * 2) {
                failure = "NEED PCM16 WAV";
                return false;
            }
            formatFound = true;
        } else if (!memcmp(header, "data", 4)) {
            if (!formatFound || !length || length % (sourceChannels * 2)) return false;
            end = start + length;
            return true;
        }
        uint64_t next = uint64_t(start) + length + (length & 1);
        if (next > riffEnd || !seek(next)) return false;
    }
    return false;
}

bool TrackDecoder::openMp3()
{
    // Skip leading ID3v2 tags, including large embedded artwork. Never send
    // arbitrary tag contents into the MP3 frame decoder.
    uint8_t header[10];
    for (unsigned tags = 0; tags < 8; ++tags) {
        uint32_t start = cursor;
        if (input.size - cursor < 10 || !readExact(header, 10)) return false;
        if (memcmp(header, "ID3", 3)) { if (!seek(start)) return false; break; }
        if ((header[3] < 2 || header[3] > 4) ||
            (header[6] | header[7] | header[8] | header[9]) & 0x80) return false;
        uint32_t size = (uint32_t(header[6]) << 21) | (uint32_t(header[7]) << 14) |
                        (uint32_t(header[8]) << 7) | header[9];
        uint64_t next = uint64_t(start) + 10 + size + ((header[3] == 4 && (header[5] & 0x10)) ? 10 : 0);
        if (next >= input.size || !seek(next)) return false;
        if (tags == 7) return false;
    }
    uint32_t audioStart = cursor;
    // Exclude an ID3v1 trailer from sync searches at EOF.
    if (input.size >= 128 && seek(input.size - 128) && readExact(header, 3) && !memcmp(header, "TAG", 3))
        end = input.size - 128;
    if (!seek(audioStart) || audioStart >= end) return false;
    mp3 = MP3InitDecoder();
    if (!mp3) { failure = "DECODER OUT OF MEMORY"; return false; }
    return true;
}

bool TrackDecoder::decodeFrame()
{
    pcmCount = pcmIndex = 0;
    if (wave) {
        size_t bytes = std::min(size_t(end - cursor), sizeof(decoded));
        bytes -= bytes % (sourceChannels * 2);
        if (!bytes || !readExact(reinterpret_cast<uint8_t *>(decoded), bytes)) return false;
        pcmCount = bytes / 2;
        return true;
    }
    // Bound work on junk/truncated files. A bad track cannot spin forever.
    for (unsigned attempts = 0; attempts < 32; ++attempts) {
        if (cursor < end && buffered < sizeof(compressed)) {
            size_t wanted = std::min(sizeof(compressed) - buffered, size_t(end - cursor));
            size_t got = input.read(input.context, compressed + buffered, wanted);
            if (got > wanted) return false;
            cursor += got;
            buffered += got;
            if (!got) end = cursor;
        }
        if (buffered < 4) return false;
        int sync = MP3FindSyncWord(compressed, buffered);
        if (sync < 0) {
            // Retain the last bytes so a sync/header split across reads survives.
            size_t keep = std::min(buffered, size_t(3));
            memmove(compressed, compressed + buffered - keep, keep);
            buffered = keep;
            if (cursor == end) return false;
            continue;
        }
        if (sync) {
            buffered -= sync;
            memmove(compressed, compressed + sync, buffered);
        }
        if (buffered < 4) continue;
        uint8_t *data = compressed;
        int left = buffered;
        int result = MP3Decode(mp3, &data, &left, decoded, 0);
        size_t consumed = buffered - left;
        if (result == ERR_MP3_INDATA_UNDERFLOW && cursor < end && buffered < sizeof(compressed)) continue;
        if (!consumed) consumed = 1;
        buffered -= consumed;
        memmove(compressed, compressed + consumed, buffered);
        if (result != ERR_MP3_NONE) continue;
        MP3FrameInfo info;
        MP3GetLastFrameInfo(mp3, &info);
        if (info.layer != 3 || info.bitsPerSample != 16 || info.samprate < 8000 || info.samprate > 48000 ||
            (info.nChans != 1 && info.nChans != 2) || info.outputSamps <= 0 || info.outputSamps > 2304) return false;
        if (sourceRate && (sourceRate != unsigned(info.samprate) || sourceChannels != unsigned(info.nChans))) {
            failure = "FORMAT CHANGES IN TRACK";
            return false;
        }
        sourceRate = info.samprate;
        sourceChannels = info.nChans;
        pcmCount = info.outputSamps;
        return true;
    }
    failure = "CORRUPT MP3 - SKIPPING";
    return false;
}

bool TrackDecoder::sourceSample(int16_t sample[2])
{
    if (pcmIndex == pcmCount && !decodeFrame()) return false;
    sample[0] = decoded[pcmIndex++];
    sample[1] = sourceChannels == 2 ? decoded[pcmIndex++] : sample[0];
    return true;
}

size_t TrackDecoder::render(uint8_t *out, size_t frames, unsigned rate, unsigned channels, unsigned volume)
{
    if (!primed || finished || (rate != 44100 && rate != 48000) || (channels != 1 && channels != 2)) return 0;
    volume = std::min(volume, 100u);
    size_t count = 0;
    while (count < frames && !finished) {
        int32_t samples[2];
        for (unsigned ch = 0; ch < 2; ++ch)
            samples[ch] = a[ch] + (int64_t(b[ch] - a[ch]) * phase) / rate;
        if (channels == 1) samples[0] = (samples[0] + samples[1]) / 2;
        for (unsigned ch = 0; ch < channels; ++ch) {
            int16_t sample = samples[ch] * int32_t(volume) / 100;
            *out++ = uint16_t(sample) & 255;
            *out++ = uint16_t(sample) >> 8;
        }
        ++count;
        phase += sourceRate;
        while (phase >= rate) {
            phase -= rate;
            if (!nextAvailable) { finished = true; break; }
            a[0] = b[0]; a[1] = b[1];
            nextAvailable = sourceSample(b);
            if (!nextAvailable) { b[0] = a[0]; b[1] = a[1]; }
        }
    }
    return count;
}
