#include <Arduino.h>
#include <SD.h>
#include <cstdlib>
#include <vector>
#include "music.h"
static std::vector<std::string> played;
static std::string status;
extern "C" void lucid_music_progress(unsigned, unsigned) {}
extern "C" void *helix_malloc(int size) { return malloc(size); }
extern "C" void helix_free(void *p) { free(p); }
extern "C" void lucid_status(const char *s, const char *detail) {
    status=s;
    if (status=="PLAYING") played.emplace_back(detail);
}
extern "C" void host_setup(const char *root,unsigned rate,unsigned channels) {
    SD.root=root;played.clear();status.clear();
    lucid_music_scan(true);lucid_music_start(rate,channels);
}
extern "C" unsigned host_played() { return played.size(); }
extern "C" const char *host_title(unsigned i) { return i<played.size() ? played[i].c_str() : ""; }
extern "C" const char *host_status() { return status.c_str(); }
