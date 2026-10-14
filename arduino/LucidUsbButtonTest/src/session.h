#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Group a short press and its release without assuming their unknown byte values.
// The operator waits for READY and taps once. This counts report bursts, not usages.
class ClickGroups {
public:
    unsigned completed = 0;
    bool connected = false, active = false;
    uint32_t armedAt = 0, armDelay = 3000, started = 0, lastChange = 0;
    void attach(uint32_t now) { connected=true; armedAt=now; armDelay=3000; }
    bool ready(uint32_t now) const {
        return connected && !active && completed<3 && uint32_t(now-armedAt)>=armDelay;
    }
    unsigned input(uint32_t now, bool changed) {
        if (!active && changed && ready(now)) { active=true; started=lastChange=now; }
        if (active && changed) lastChange=now;
        return active ? completed+1 : 0;
    }
    bool poll(uint32_t now) {
        if (!active || uint32_t(now-started)<1500 || uint32_t(now-lastChange)<1000) return false;
        active=false; ++completed; armedAt=now; armDelay=completed*300+1400;
        return true;
    }
};

struct RawReport {
    uint32_t ms;
    uint8_t address, interface, klass, endpoint, length;
    uint8_t data[64];
};
struct SavedCapture {
    uint32_t magic = 0x4c554233; // LUB3
    uint16_t version = 3;
    uint8_t completed = 0, finished = 0;
    uint16_t descriptorBytes = 0, diagnosticBytes = 0;
    uint16_t descriptorsDropped = 0, diagnosticDropped = 0;
    uint32_t reportsSeen = 0;
    uint16_t reportsDropped[4] = {};
    uint8_t reportCounts[4] = {};
    char reason[48] = {};
    char diagnostic[256] = {};
    uint8_t descriptors[1536] = {};
    RawReport initial[4] = {};
    RawReport clicks[3][8] = {};

    bool valid() const {
        if (magic!=0x4c554233 || version!=3 || completed>3 || descriptorBytes>sizeof(descriptors) ||
            diagnosticBytes>sizeof(diagnostic) || reportCounts[0]>4 || reason[sizeof(reason)-1]) return false;
        for (unsigned g=1;g<4;++g) if (reportCounts[g]>8) return false;
        for (unsigned g=0;g<4;++g) for (unsigned i=0;i<reportCounts[g];++i)
            if ((g ? clicks[g-1][i] : initial[i]).length>64) return false;
        return true;
    }
    void descriptor(uint8_t type, uint8_t addr, uint8_t iface, const uint8_t *data, size_t bytes) {
        // Complete descriptors only; a dropped descriptor never steals click capacity.
        if (bytes>sizeof(descriptors)-descriptorBytes || bytes+5>sizeof(descriptors)-descriptorBytes) {
            if (descriptorsDropped!=UINT16_MAX) ++descriptorsDropped;
            return;
        }
        auto *out=descriptors+descriptorBytes;
        out[0]=type; out[1]=addr; out[2]=iface; out[3]=bytes&255; out[4]=bytes>>8;
        memcpy(out+5,data,bytes); descriptorBytes+=5+bytes;
    }
    void report(unsigned group, uint32_t now, uint8_t addr, uint8_t iface, uint8_t klass,
                uint8_t ep, const uint8_t *data, size_t bytes) {
        ++reportsSeen;
        if (group>3) return;
        unsigned cap=group ? 8 : 4;
        if (bytes>64 || reportCounts[group]==cap) {
            if (reportsDropped[group]!=UINT16_MAX) ++reportsDropped[group];
            return;
        }
        RawReport &r=group ? clicks[group-1][reportCounts[group]++] : initial[reportCounts[0]++];
        r.ms=now; r.address=addr; r.interface=iface; r.klass=klass; r.endpoint=ep; r.length=bytes;
        memcpy(r.data,data,bytes);
    }
};
static_assert(sizeof(SavedCapture)<=4096, "Keep atomic NVS snapshots small enough for the default partition");
