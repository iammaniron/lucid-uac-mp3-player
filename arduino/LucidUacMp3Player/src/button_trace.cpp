#include <Arduino.h>
#include <SD.h>
#include "../LucidPins.h"
#include "button_trace.h"
#include "music.h"

namespace {
constexpr const char *path="/buttons-playing.txt";
struct Report {
    uint32_t ms;
    uint8_t address, interface, endpoint, bytes, pressed, volume;
    bool decoded, streaming, paused;
    uint8_t data[64];
};
Report pending[64];
unsigned count, dropped, state;
uint32_t lastFlush;
int last=-1;

bool append(const uint8_t *data, size_t bytes) {
    digitalWrite(LUCID_LCD_CS,HIGH);
    File file=SD.open(path,FILE_APPEND);
    if (!file) return false;
    bool ok=file.write(data,bytes)==bytes;
    file.flush(); file.close();
    return ok;
}
}
extern "C" void lucid_button_trace_begin(bool sd_ready) {
    count=dropped=state=0; last=-1;
#if LUCID_BUTTON_TRACE
    if (!sd_ready) return;
    const char header[]="\n=== LIVE MUSIC BUTTON TRACE v1 ===\n"
        "Press centre, volume +, volume - with 3 seconds between; music must be audible first.\n"
        "Names are not inferred. t_ms is since boot; paused/volume are BEFORE the reported action.\n"
        "pressed mask: 01=play/pause 02=next 04=previous 08=volume+ 10=volume- 20=mute 40=play 80=pause.\n";
    state=append((const uint8_t *)header,sizeof(header)-1) ? 1 : 2;
    lastFlush=millis();
#else
    (void)sd_ready;
#endif
}
extern "C" void lucid_button_trace_report(uint8_t addr, uint8_t iface, uint8_t ep,
    const uint8_t *data, size_t bytes, bool decoded, uint8_t pressed, bool streaming) {
    if (state!=1) return;
    if (count==64 || bytes>64) { ++dropped; return; }
    Report &r=pending[count++];
    r.ms=millis(); r.address=addr; r.interface=iface; r.endpoint=ep; r.bytes=bytes;
    r.decoded=decoded; r.pressed=pressed; r.streaming=streaming;
    r.paused=lucid_music_paused(); r.volume=lucid_music_volume();
    memcpy(r.data,data,bytes);
    // Retain the last nonzero packet byte so a 1ms release doesn't hide it on LCD.
    if (bytes && data[0]) last=data[0];
}
extern "C" void lucid_button_trace_poll(unsigned transfers, unsigned errors) {
    if (state!=1 || (!count && !dropped) || millis()-lastFlush<2000) return;
    // Same Arduino task as decoding/display. Transfers were requeued before here.
    digitalWrite(LUCID_LCD_CS,HIGH);
    File file=SD.open(path,FILE_APPEND);
    if (!file) { state=2; return; }
    bool ok=true;
    char line[384];
    for (unsigned i=0;i<count && ok;++i) {
        const Report &r=pending[i];
        int n=snprintf(line,sizeof(line),"RAW t_ms=%lu addr=%u if=%u ep=%02X streaming=%u paused_before=%u volume_before=%u decoded=%u pressed=%02X len=%u :",
            (unsigned long)r.ms,r.address,r.interface,r.endpoint,unsigned(r.streaming),unsigned(r.paused),r.volume,unsigned(r.decoded),r.pressed,r.bytes);
        for (unsigned j=0;j<r.bytes;++j) n+=snprintf(line+n,sizeof(line)-n," %02X",r.data[j]);
        line[n++]='\n';
        ok=file.write((const uint8_t *)line,n)==size_t(n);
    }
    int n=snprintf(line,sizeof(line),"FLUSH t_ms=%lu UAC_completed=%u UAC_errors=%u dropped_reports=%u\n",
        millis(),transfers,errors,dropped);
    if (ok) ok=file.write((const uint8_t *)line,n)==size_t(n);
    file.flush(); file.close();
    if (!ok) state=2;
    count=dropped=0; lastFlush=millis();
}
extern "C" unsigned lucid_button_trace_status(void) { return state; }
extern "C" int lucid_button_trace_last(void) { return last; }
