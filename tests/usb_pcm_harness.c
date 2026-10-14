#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "usb_pcm.h"
#include "fixtures/minidsp_2x4hd_descriptors.h"

static uac2_device_info_t parse(void) {
    uac2_device_info_t info;
    assert(uac2_parse_config_descriptor(minidsp_2x4hd_config1_desc,
        sizeof(minidsp_2x4hd_config1_desc), &info));
    return info;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "descriptor")) {
        uac2_device_info_t info = parse();
        assert(info.is_uac2 && info.num_clock_sources == 1);
        assert(info.num_as_ifaces == 3 && info.bcdADC == 0x0200);
        unsigned playback = 0;
        for (unsigned i = 0; i < info.num_as_ifaces; ++i) {
            uac2_as_iface_t *as = &info.as_ifaces[i];
            assert(as->formats == 1);
            if (as->interface_num == 1) {
                assert(lucid_uac2_mode_ok(as, 1, 48000));
                assert(lucid_uac2_mode_ok(as, 1, 44100));
                assert(as->fb_ep_addr && as->nr_channels == 2);
                assert(as->bit_resolution == 16 || as->bit_resolution == 24);
                ++playback;
            } else assert(!lucid_uac2_mode_ok(as, 1, 48000));
        }
        assert(playback == 2);
    } else if (!strcmp(argv[1], "malformed")) {
        uac2_device_info_t info;
        uint8_t data[sizeof(minidsp_2x4hd_config1_desc)];
        for (unsigned n = 0; n < sizeof(data); ++n)
            assert(!uac2_parse_config_descriptor(minidsp_2x4hd_config1_desc, n, &info));
        assert(!uac2_parse_config_descriptor(NULL, sizeof(data), &info));
        assert(!uac2_parse_config_descriptor(data, sizeof(data), NULL));
        // Truncated descriptor at each descriptor boundary, even after the AC
        // header has already claimed UAC2. No partial success is permitted.
        for (unsigned offset = 0; offset < sizeof(data);) {
            memcpy(data, minidsp_2x4hd_config1_desc, sizeof(data));
            unsigned len = data[offset];
            data[offset] = 1;
            assert(!uac2_parse_config_descriptor(data, sizeof(data), &info));
            offset += len;
        }
        memcpy(data, minidsp_2x4hd_config1_desc, sizeof(data));
        for (unsigned offset = 0; offset < sizeof(data); offset += data[offset])
            if (data[offset+1] == 4) data[offset+7] = 0; // UAC1 interfaces
        assert(!uac2_parse_config_descriptor(data, sizeof(data), &info));
    } else if (!strcmp(argv[1], "packing")) {
        const uint8_t source[] = {0,0, 0xff,0x7f, 0,0x80, 0xff,0xff, 0x34,0x12};
        for (unsigned slot = 2; slot <= 4; ++slot) {
            uint8_t data[32]; memset(data, 0xaa, sizeof(data));
            memcpy(data, source, sizeof(source));
            assert(lucid_pack_pcm16(data, data, 5, slot) == 5*slot);
            for (unsigned i=0;i<5;++i) {
                for (unsigned j=0;j<slot-2;++j) assert(data[i*slot+j] == 0);
                assert(data[i*slot+slot-2] == source[i*2]);
                assert(data[i*slot+slot-1] == source[i*2+1]);
            }
            assert(data[5*slot] == 0xaa);
        }
        uint8_t out[20];
        assert(!lucid_pack_pcm16(out, source, 5, 1));
        assert(!lucid_pack_pcm16(out, source, 5, 5));
    } else if (!strcmp(argv[1], "modes")) {
        assert(lucid_rate_in_range(48000, 44100, 96000, 0));
        assert(!lucid_rate_in_range(48000, 44100, 88200, 44100));
        assert(lucid_rate_in_range(44100, 44100, 44100, 0));
        assert(!lucid_rate_in_range(48000, 96000, 44100, 0));
        uac2_device_info_t info = parse();
        uac2_as_iface_t base = info.as_ifaces[0], as;
        base.bit_resolution = 32; base.sub_slot_size = 4; base.ep_max_packet_size = 392;
        assert(lucid_uac2_mode_ok(&base, 1, 48000));
        base.bit_resolution = 24; // 24 valid bits in a four-byte container
        assert(lucid_uac2_mode_ok(&base, 1, 48000));
        as=base; as.formats=4; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.formats=3; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.sub_slot_size=2; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.ep_max_packet_size=384; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.fb_ep_addr=0; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.ep_interval=2; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.ep_max_packet_size=1024; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.nr_channels=6; assert(!lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.nr_channels=1; assert(lucid_uac2_mode_ok(&as,1,48000));
        as=base; as.ep_attributes=9; as.fb_ep_addr=0; // Adaptive, no feedback
        assert(lucid_uac2_mode_ok(&as,1,44100));
        assert(!lucid_uac2_mode_ok(&as,1,96000));
    } else if (!strcmp(argv[1], "nominal")) {
        for (unsigned rate=44100;rate<=48000;rate+=3900) {
            uint32_t phase=0, fbphase=0, sum=0;
            for (unsigned n=0;n<1000;++n) sum+=lucid_usb_packet_bytes(rate,0,6,600,&phase,&fbphase)/6;
            assert(sum == rate && phase == 0);
        }
    } else if (!strcmp(argv[1], "feedback")) {
        for (unsigned rate=44100;rate<=48000;rate+=3900) {
            uint32_t phase=0, fbphase=0, sum=0;
            // A DAC clock slightly faster than nominal: must also be obeyed at 48k.
            uint32_t fb = (uint32_t)(((uint64_t)rate<<16)/1000) + 8192;
            for (unsigned n=0;n<1000;++n) sum+=lucid_usb_packet_bytes(rate,fb,4,600,&phase,&fbphase)/4;
            assert(sum == ((uint64_t)fb*1000 >> 16));
            assert(sum >= rate + 124);
        }
    } else if (!strcmp(argv[1], "capacity")) {
        uint32_t phase=0, fbphase=0;
        assert(lucid_usb_packet_bytes(48000,UINT32_MAX,6,600,&phase,&fbphase)==288);
        assert(lucid_usb_packet_bytes(48000,1,6,600,&phase,&fbphase)==288);
        assert(lucid_usb_packet_bytes(48000,49u<<16,6,289,&phase,&fbphase)==288);
        assert(!lucid_usb_packet_bytes(48000,0,0,600,&phase,&fbphase));
    } else return 1;
    return 0;
}
