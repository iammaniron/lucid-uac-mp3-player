#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <string.h>
#include "../LucidPins.h"
#include "music.h"
#include "player.h"
#include "track_decoder.h"

namespace {
constexpr unsigned MAX_DEPTH = 8;
// Walk directories incrementally: no fixed track limit or whole-card playlist in RAM.
class CardWalker {
    File dirs[MAX_DEPTH + 1];
    String paths[MAX_DEPTH + 1];
    unsigned depth = 0;
    bool active = false;
public:
    void close() { for (auto &dir : dirs) dir.close(); active = false; depth = 0; }
    bool begin() { close(); paths[0] = "/"; dirs[0] = SD.open("/"); active = bool(dirs[0]); return active; }
    bool next(String &path) {
        while (active) {
            File entry = dirs[depth].openNextFile();
            if (!entry) {
                dirs[depth].close();
                if (!depth) { active = false; break; }
                --depth;
                continue;
            }
            String name = entry.name();
            name = name.substring(name.lastIndexOf('/') + 1);
            bool directory = entry.isDirectory();
            entry.close(); // Copy name and release the entry before opening it again.
            if (!name.length() || name.startsWith(".") || name.equalsIgnoreCase("System Volume Information")) continue;
            path = (paths[depth] == "/" ? paths[depth] : paths[depth] + "/") + name;
            if (path.length() > 255) { Serial0.printf("Skip long path: %s\n", path.c_str()); continue; }
            if (directory) {
                if (depth == MAX_DEPTH) { Serial0.printf("Skip deep folder: %s\n", path.c_str()); continue; }
                File child = SD.open(path);
                if (child && child.isDirectory()) {
                    ++depth;
                    dirs[depth] = child;
                    paths[depth] = path;
                }
            } else {
                name.toLowerCase();
                if (name.endsWith(".mp3") || name.endsWith(".wav")) return true;
            }
        }
        return false;
    }
} walker;

File song;
TrackDecoder decoder;
std::vector<String> badTracks; // Small cache only for corrupt/unsupported tracks.
String currentPath;
uint32_t trackCount = 0, trackNumber = 0;
unsigned outputRate = 48000, outputChannels = 2;
bool trackOpen = false, exhausted = false, passPlayable = false;
unsigned volume = LUCID_MUSIC_VOLUME;
bool paused = false, muted = false;

void showSong()
{
    String title = currentPath.substring(currentPath.lastIndexOf('/') + 1);
    lucid_status(paused ? "PAUSED" : "PLAYING", title.c_str());
    lucid_music_progress(0, 0);
}

size_t readFile(void *context, uint8_t *out, size_t bytes)
{
    int got = static_cast<File *>(context)->read(out, bytes);
    return got > 0 ? size_t(got) : 0;
}
bool seekFile(void *context, uint32_t offset) { return static_cast<File *>(context)->seek(offset); }
void rememberBad() { if (badTracks.size() < 64) badTracks.push_back(currentPath); }
bool isBad(const String &path) {
    for (const auto &bad : badTracks) if (path == bad.c_str()) return true;
    return false;
}

bool nextSong()
{
    decoder.close();
    song.close();
    trackOpen = false;
    while (true) {
        if (!walker.next(currentPath)) {
            if (!passPlayable || !walker.begin()) {
                exhausted = true;
                lucid_status("NO PLAYABLE TRACKS", "CHECK MP3 OR PCM16 WAV");
                return false;
            }
            // Repeat the entire card. Known bad files are skipped on later passes.
            passPlayable = false;
            trackNumber = 0;
            Serial0.println("End of playlist; looping to first track");
            continue;
        }
        ++trackNumber;
        if (isBad(currentPath)) continue;
        song = SD.open(currentPath, FILE_READ);
        String lower = currentPath;
        lower.toLowerCase();
        TrackInput input = {&song, readFile, seekFile, song ? uint32_t(song.size()) : 0};
        if (song && decoder.open(input, lower.endsWith(".wav"))) {
            trackOpen = passPlayable = true;
            Serial0.printf("Track %u/%u: %s (%u Hz -> %u Hz USB)\n",
                           unsigned(trackNumber), unsigned(trackCount), currentPath.c_str(),
                           decoder.sampleRate(), outputRate);
            showSong();
            return true;
        }
        Serial0.printf("Skipping %s: %s\n", currentPath.c_str(), decoder.error());
        rememberBad();
        decoder.close();
        song.close();
        // One new invalid file per poll keeps USB lifecycle handling responsive.
        return false;
    }
}
}

extern "C" void lucid_music_scan(bool sd_ready)
{
    lucid_music_stop();
    trackCount = 0;
    badTracks.clear();
    exhausted = false;
    volume = LUCID_MUSIC_VOLUME > 100 ? 100 : LUCID_MUSIC_VOLUME;
    muted = false;
    if (!sd_ready) { lucid_status("SD NOT READY", "INSERT CARD AND RESET"); return; }
    lucid_status("SCANNING SD CARD", "MP3 AND WAV FILES");
    String path;
    if (walker.begin()) while (walker.next(path)) ++trackCount;
    walker.close();
    Serial0.printf("Playlist: %u tracks\n", unsigned(trackCount));
    char detail[26];
    snprintf(detail, sizeof(detail), "%u TRACKS - LOOP ALL", unsigned(trackCount));
    lucid_status(trackCount ? "SD MUSIC READY" : "NO MUSIC ON SD", detail);
}

extern "C" bool lucid_music_available(void) { return trackCount && !exhausted; }

extern "C" void lucid_music_stop(void)
{
    decoder.close();
    song.close();
    walker.close();
    trackOpen = passPlayable = false;
    trackNumber = 0;
    paused = false;
}

extern "C" void lucid_music_start(unsigned rate, unsigned channels)
{
    lucid_music_stop();
    outputRate = rate;
    outputChannels = channels;
    if (trackCount && !walker.begin()) exhausted = true;
}

extern "C" size_t lucid_music_read(uint8_t *out, size_t frames)
{
    if (!lucid_music_available()) return 0;
    size_t produced = 0;
    unsigned transitions = 0;
    while (produced < frames && transitions < 4) {
        if (!trackOpen && !nextSong()) {
            if (!exhausted) {
                // Keep the DAC clocked while moving past an invalid file.
                memset(out + produced * outputChannels * 2, 0, (frames - produced) * outputChannels * 2);
                return frames;
            }
            break;
        }
        if (paused) {
            // Keep USB transfers alive without consuming any decoded samples.
            memset(out + produced * outputChannels * 2, 0, (frames-produced) * outputChannels * 2);
            return frames;
        }
        size_t count = decoder.render(out + produced * outputChannels * 2,
                                      frames - produced, outputRate, outputChannels, muted ? 0 : volume);
        produced += count;
        if (count == 0) {
            if (decoder.error()[0]) {
                Serial0.printf("Track error: %s: %s\n", currentPath.c_str(), decoder.error());
                rememberBad();
            }
            trackOpen = false;
            ++transitions;
        }
    }
    return produced;
}

extern "C" unsigned lucid_music_volume(void) { return volume; }
extern "C" bool lucid_music_paused(void) { return paused; }
extern "C" bool lucid_music_muted(void) { return muted; }

extern "C" void lucid_music_control(uint16_t usage)
{
    if (!lucid_music_available()) return;
    switch (usage) {
    case 0xcd: paused = !paused; break;
    case 0xb0: paused = false; break;
    case 0xb1: paused = true; break;
    case 0xe9: volume = volume > 95 ? 100 : volume + 5; muted = false; break;
    case 0xea: volume = volume < 5 ? 0 : volume - 5; muted = false; break;
    case 0xe2: muted = !muted; break;
    case 0xb5: // Walker already points immediately after the current track.
        decoder.close(); song.close(); trackOpen = false;
        return;
    case 0xb6: {
        // Rewalk the directory order, avoiding a RAM-sized playlist/history limit.
        // Unknown corrupt candidates are still validated by nextSong().
        CardWalker search;
        String path;
        uint32_t index = 0, target = 0, last = 0;
        // Release the existing directory chain first: the SD mount has 12 handles.
        decoder.close(); song.close(); walker.close(); trackOpen = false;
        if (!search.begin()) { exhausted = true; return; }
        while (search.next(path)) {
            ++index;
            if (!isBad(path)) {
                last = index;
                if (index < trackNumber) target = index;
            }
            if (index >= trackNumber && target) break;
        }
        search.close();
        if (!target) target = last; // Previous at the beginning wraps to the end.
        if (!target) { exhausted = true; return; }
        if (!walker.begin()) { exhausted = true; return; }
        trackNumber = 0;
        while (trackNumber + 1 < target && walker.next(path)) ++trackNumber;
        passPlayable = true;
        return;
    }
    default: return;
    }
    Serial0.printf("Media 0x%04x: volume %u%%, paused %d, mute %d\n", usage, volume, paused, muted);
    if (trackOpen) showSong();
}
