#pragma once
#include "session.h"

inline const char *capture_button_label(unsigned group) {
    const char *names[]={"UNASSIGNED", "CENTRE/PAUSE", "VOLUME +", "VOLUME -"};
    return group<4 ? names[group] : "UNASSIGNED";
}
template<class Output>
void capture_hex(Output &out, const uint8_t *data, size_t bytes) {
    for (size_t i=0;i<bytes;++i) out.printf(" %02X",data[i]);
}
template<class Output>
void capture_raw_text(Output &out, unsigned group, const char *label,
                      uint32_t ms, uint8_t addr, uint8_t iface, uint8_t klass,
                      uint8_t ep, const uint8_t *data, size_t bytes) {
    out.printf("RAW t_ms=%lu group=%u expected_button=\"%s\" addr=%u if=%u class=%02X ep=%02X len=%u :",
        (unsigned long)ms,group,label,addr,iface,klass,ep,unsigned(bytes));
    capture_hex(out,data,bytes); out.println();
}
template<class Output>
void capture_descriptor_text(Output &out, uint8_t kind, uint8_t addr, uint8_t iface,
                             const uint8_t *data, size_t bytes) {
    const char *name=kind==1 ? "DEVICE_DESC" : kind==2 ? "CONFIG_DESC" : "HID_REPORT_DESC";
    out.printf("%s addr=%u if=%u bytes=%u\n",name,addr,iface,unsigned(bytes));
    for (size_t offset=0;offset<bytes;offset+=16) {
        out.printf("  %04X:",unsigned(offset));
        capture_hex(out,data+offset,bytes-offset<16 ? bytes-offset : 16); out.println();
    }
}
template<class Output>
void capture_recovered_text(Output &out, const SavedCapture &saved) {
    out.println("\n=== RECOVERED PREVIOUS FLASH CAPTURE v3 ===");
    out.printf("Captured groups: %u/3; finished=%u; reason: %s\n",saved.completed,saved.finished,saved.reason);
    out.println("Button names were unknown in this older test. Tell us what you pressed in each group.");
    out.println("Timestamps are milliseconds since THAT capture boot, not the current boot or wall-clock time.");
    out.write(reinterpret_cast<const uint8_t *>(saved.diagnostic),saved.diagnosticBytes); out.println();
    for (size_t pos=0;pos+5<=saved.descriptorBytes;) {
        const uint8_t *d=saved.descriptors+pos;
        size_t bytes=d[3] | (d[4]<<8);
        if (bytes>saved.descriptorBytes-pos-5) { out.println("Invalid saved descriptor length"); break; }
        capture_descriptor_text(out,d[0],d[1],d[2],d+5,bytes); pos+=5+bytes;
    }
    for (unsigned group=0;group<4;++group) {
        out.printf("--- PREVIOUS GROUP %u ---\n",group);
        for (unsigned i=0;i<saved.reportCounts[group];++i) {
            const RawReport &r=group ? saved.clicks[group-1][i] : saved.initial[i];
            capture_raw_text(out,group,"UNKNOWN (previous test)",r.ms,r.address,r.interface,r.klass,r.endpoint,r.data,r.length);
        }
        if (saved.reportsDropped[group]) out.printf("Additional reports omitted in previous group: %u\n",saved.reportsDropped[group]);
    }
    out.printf("Total packets=%lu; omitted descriptors=%u; omitted diagnostic characters=%u\n",
        (unsigned long)saved.reportsSeen,saved.descriptorsDropped,saved.diagnosticDropped);
    out.println("=== END RECOVERED CAPTURE ===");
}
