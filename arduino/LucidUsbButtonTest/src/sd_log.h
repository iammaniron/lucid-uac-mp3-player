#pragma once
#include <Arduino.h>
#include <SD.h>

// SD and LCD share SPI; every operation here runs on the Arduino application task.
// Never truncate an existing file. Acknowledgements follow flush/close/readback.
class SdCaptureLog : public Print {
    File file;
    uint8_t buffer[512];
    size_t used = 0, chunkStart = 0, chunkBytes = 0;
    uint32_t hash = 2166136261u;
    bool failed = false;
    static uint32_t addHash(uint32_t h, const uint8_t *p, size_t n) {
        while (n--) h = (h ^ *p++) * 16777619u;
        return h;
    }
    bool drain() {
        if (failed || !file) return false;
        if (!used) return true;
        digitalWrite(43,HIGH);
        size_t count=file.write(buffer,used);
        hash=addHash(hash,buffer,count); chunkBytes+=count;
        if (count!=used) failed=true;
        used=0;
        return !failed;
    }
public:
    static constexpr const char *path = "/file.txt";
    using Print::write;
    bool begin() {
        if (failed) return false;
        digitalWrite(43,HIGH);
        file=SD.open(path,FILE_APPEND);
        if (!file || file.isDirectory()) { failed=true; file.close(); return false; }
        chunkStart=file.size(); chunkBytes=used=0; hash=2166136261u;
        return true;
    }
    size_t write(uint8_t c) override { return write(&c,1); }
    size_t write(const uint8_t *data, size_t bytes) override {
        if (failed || !file) return 0;
        size_t accepted=0;
        while (accepted<bytes) {
            if (used==sizeof(buffer) && !drain()) break;
            size_t n=min(bytes-accepted,sizeof(buffer)-used);
            memcpy(buffer+used,data+accepted,n); used+=n; accepted+=n;
        }
        return accepted;
    }
    bool checkpoint(bool reopen=true) {
        if (!drain()) { file.close(); return false; }
        file.flush();
        size_t end=file.size(); file.close();
        if (end!=chunkStart+chunkBytes) { failed=true; return false; }
        File verify=SD.open(path,FILE_READ);
        bool ok=verify && verify.size()==end && verify.seek(chunkStart);
        uint32_t actual=2166136261u;
        size_t remaining=chunkBytes;
        uint8_t block[128];
        while (ok && remaining) {
            size_t wanted=min(remaining,sizeof(block));
            int n=verify.read(block,wanted);
            if (n<=0 || size_t(n)>wanted) { ok=false; break; }
            actual=addHash(actual,block,n); remaining-=n;
        }
        verify.close();
        if (!ok || actual!=hash) { failed=true; return false; }
        return reopen ? begin() : true;
    }
    bool error() const { return failed; }
};
