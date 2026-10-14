#include "hid_consumer.h"
#include <string.h>

const uint16_t lucid_media_usages[LUCID_MEDIA_KEYS] = {
    0xcd, 0xb5, 0xb6, 0xe9, 0xea, 0xe2, 0xb0, 0xb1
};
typedef struct { uint32_t page, size, count, id; int32_t minimum; } globals_t;
typedef struct { uint32_t usages[64], min, max; unsigned count; bool range, have_max; } locals_t;

static uint32_t usage(uint32_t value, unsigned bytes, uint32_t page)
{ return bytes == 4 ? value : (page << 16) | value; }
static int key(uint32_t u)
{
    if ((u >> 16) != 0x0c) return -1;
    for (unsigned i = 0; i < LUCID_MEDIA_KEYS; ++i)
        if ((u & 0xffff) == lucid_media_usages[i]) return (int)i;
    return -1;
}
static bool field(lucid_hid_map_t *m, unsigned report, unsigned offset,
                  const globals_t *g, int k, bool array, uint32_t value)
{
    if (k < 0) return true;
    if (m->field_count == LUCID_HID_FIELDS || !g->size || g->size > 32) return false;
    m->fields[m->field_count++] = (lucid_hid_field_t){
        .offset = offset, .count = array ? g->count : 1, .value = value,
        .size = g->size, .report = report, .key = k, .array = array};
    return true;
}
bool lucid_hid_parse(lucid_hid_map_t *m, const uint8_t *d, size_t bytes)
{
    memset(m, 0, sizeof(*m));
    globals_t g = {0}, stack[8];
    unsigned depth = 0;
    locals_t l = {0};
    for (size_t pos = 0; pos < bytes;) {
        uint8_t prefix = d[pos++];
        if (prefix == 0xfe) { // Reserved long items carry their own length.
            if (bytes - pos < 2 || d[pos] > bytes - pos - 2) return false;
            pos += 2 + d[pos]; continue;
        }
        unsigned n = prefix & 3; if (n == 3) n = 4;
        if (n > bytes - pos) return false;
        uint32_t v = 0;
        for (unsigned i = 0; i < n; ++i) v |= (uint32_t)d[pos++] << (8 * i);
        unsigned type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (type == 1) {
            switch (tag) {
            case 0: if (v > 0xffff) return false; g.page = v; break;
            case 1:
                g.minimum = n == 1 ? (int8_t)v : n == 2 ? (int16_t)v : (int32_t)v;
                break;
            case 7: g.size = v; break;
            case 8: if (!v || v > 255) return false; g.id = v; m->numbered = true; break;
            case 9: g.count = v; break;
            case 10: if (depth == 8) return false; stack[depth++] = g; break;
            case 11: if (!depth) return false; g = stack[--depth]; break;
            }
        } else if (type == 2) {
            if (tag == 0) {
                if (l.count == 64) return false;
                l.usages[l.count++] = usage(v, n, g.page);
            } else if (tag == 1) { l.min = usage(v, n, g.page); l.range = true; }
            else if (tag == 2) { l.max = usage(v, n, g.page); l.have_max = true; }
        } else if (type == 0) {
            if (tag == 8) { // Only Input items consume input-report bits.
                if (g.size > 32 || g.count > 512 || g.size * g.count > 4096) return false;
                if (l.range && (!l.have_max || l.max < l.min)) return false;
                unsigned r = 0;
                while (r < m->report_count && m->reports[r].id != g.id) ++r;
                if (r == m->report_count) {
                    if (r == LUCID_HID_REPORTS) return false;
                    m->reports[r].id = g.id; ++m->report_count;
                }
                unsigned offset = m->reports[r].bits;
                if (offset + g.size * g.count > 4096) return false;
                if (!(v & 1)) {
                    if (v & 2) { // Variable: one usage per bit/field.
                        for (unsigned i = 0; i < g.count; ++i) {
                            uint32_t u = l.range ? l.min + (i <= l.max-l.min ? i : l.max-l.min) :
                                l.count ? l.usages[i < l.count ? i : l.count-1] : 0;
                            if (!field(m, r, offset + i*g.size, &g, key(u), false, 0)) return false;
                        }
                    } else { // Array: field value selects a usage from the local list.
                        for (unsigned k = 0; k < LUCID_MEDIA_KEYS; ++k) {
                            uint32_t u = 0x000c0000 | lucid_media_usages[k];
                            int index = -1;
                            if (l.range && u >= l.min && u <= l.max) index = (int)(u-l.min);
                            else for (unsigned i = 0; i < l.count; ++i)
                                if (l.usages[i] == u) { index = (int)i; break; }
                            if (index >= 0 && !field(m, r, offset, &g, k, true,
                                                   (uint32_t)((int64_t)g.minimum + index))) return false;
                        }
                    }
                }
                m->reports[r].bits += g.size * g.count;
            }
            memset(&l, 0, sizeof(l)); // Locals expire at every Main item, including collections.
        }
    }
    if (depth) return false;
    if (m->numbered)
        for (unsigned r = 0; r < m->report_count; ++r) if (!m->reports[r].id) return false;
    return m->field_count != 0;
}
static uint32_t bits(const uint8_t *data, unsigned offset, unsigned size)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < size; ++i)
        value |= (uint32_t)((data[(offset+i)/8] >> ((offset+i)%8)) & 1) << i;
    return value;
}
bool lucid_hid_input(lucid_hid_map_t *m, const uint8_t *data, size_t bytes,
                     uint8_t *pressed, uint8_t *held)
{
    *pressed = *held = 0;
    uint8_t id = 0;
    if (m->numbered) { if (!bytes) return false; id = *data++; --bytes; }
    unsigned r = 0;
    while (r < m->report_count && m->reports[r].id != id) ++r;
    if (r == m->report_count || bytes < (m->reports[r].bits + 7u)/8) return false;
    uint8_t before = 0, current = 0;
    for (unsigned i = 0; i < m->report_count; ++i) before |= m->reports[i].held;
    for (unsigned i = 0; i < m->field_count; ++i) {
        const lucid_hid_field_t *f = &m->fields[i];
        if (f->report != r) continue;
        uint32_t mask = f->size == 32 ? UINT32_MAX : (1u << f->size)-1;
        for (unsigned j = 0; j < f->count; ++j) {
            uint32_t v = bits(data, f->offset + j*f->size, f->size);
            if (f->array ? v == (f->value & mask) : v != 0) current |= 1u << f->key;
        }
    }
    m->reports[r].held = current;
    for (unsigned i = 0; i < m->report_count; ++i) *held |= m->reports[i].held;
    *pressed = *held & ~before;
    return true;
}
