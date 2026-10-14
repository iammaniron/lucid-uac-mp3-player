#pragma once
#include "Arduino.h"
#include <string>
#include <limits>
#define FILE_READ "r"
#define FILE_APPEND "a"
struct CardState {
    std::string contents;
    bool openFails=false, readCorrupt=false;
    size_t writeBudget=std::numeric_limits<size_t>::max();
    unsigned flushes=0;
};
inline CardState card;
class File {
    bool opened=false, append=false;
    size_t pos=0;
public:
    File() = default;
    explicit File(const char *mode):opened(!card.openFails),append(!strcmp(mode,"a")) {}
    explicit operator bool() const { return opened; }
    bool isDirectory() const { return false; }
    size_t size() const { return opened ? card.contents.size() : 0; }
    size_t write(const uint8_t *p,size_t n) {
        if (!opened || !append) return 0;
        size_t count=min(n,card.writeBudget);
        card.contents.append((const char*)p,count);card.writeBudget-=count;return count;
    }
    int read(uint8_t *p,size_t n) {
        if (!opened || append) return -1;
        size_t count=min(n,card.contents.size()-pos);
        memcpy(p,card.contents.data()+pos,count);pos+=count;
        if (count && card.readCorrupt) p[0]^=1;
        return count;
    }
    bool seek(uint32_t offset) { if (!opened || offset>card.contents.size()) return false;pos=offset;return true; }
    void flush() { ++card.flushes; }
    void close() { opened=false; }
};
struct HostSD {
    File open(const char *path,const char *mode) {
        if (strcmp(path,"/file.txt") && strcmp(path,"/buttons-playing.txt")) return File();
        return File(mode);
    }
};
inline HostSD SD;
