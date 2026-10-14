#include "usb_probe.h"

lucid_usb_probe_t lucid_usb_probe(const uint8_t *bytes, size_t length)
{
    lucid_usb_probe_t result = {0};
    if (!bytes || length < 9 || bytes[0] < 9 || bytes[1] != 2) return result;
    const size_t total = bytes[2] | ((size_t)bytes[3] << 8);
    if (total < 9 || total > length) return result;
    bool uac1_stream = false;
    for (size_t offset = 0; offset < total;) {
        if (total - offset < 2) return result;
        const uint8_t *d = bytes + offset;
        if (d[0] < 2 || d[0] > total - offset) return result;
        if (d[1] == 4) { // Interface descriptor
            if (d[0] < 9) return result;
            result.hub |= d[5] == 9;
            result.audio |= d[5] == 1;
            result.uac2 |= d[5] == 1 && d[7] == 0x20;
            uac1_stream = d[5] == 1 && d[6] == 2 && d[7] == 0 && d[3] != 0;
        } else if (d[1] == 5) { // Endpoint descriptor
            if (d[0] < 7) return result;
            if (uac1_stream && !(d[2] & 0x80) && (d[3] & 3) == 1)
                result.uac1_output = true;
        }
        offset += d[0];
    }
    result.valid = true;
    return result;
}
