"""Compile the actual descriptor/report decoder and exercise common headset layouts."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'arduino/LucidUacMp3Player/src'

class HidConsumerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        harness = root/'harness.c'
        harness.write_text('''#include "hid_consumer.h"
static lucid_hid_map_t map;
int parse(const uint8_t *p, size_t n) { return lucid_hid_parse(&map,p,n); }
int input(const uint8_t *p,size_t n) {
    uint8_t pressed,held;
    return lucid_hid_input(&map,p,n,&pressed,&held) ? pressed | (held << 8) : -1;
}
''')
        library = root/'hid.so'
        subprocess.run(['cc','-std=c11','-shared','-fPIC','-Wall','-Wextra','-Werror',
                        '-I'+str(SRC),str(SRC/'hid_consumer.c'),str(harness),'-o',str(library)],check=True)
        cls.lib = ctypes.CDLL(str(library))
        for fn in (cls.lib.parse, cls.lib.input):
            fn.argtypes = [ctypes.c_char_p,ctypes.c_size_t]
            fn.restype = ctypes.c_int

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def parse(self, descriptor):
        data=bytes.fromhex(descriptor)
        return self.lib.parse(data,len(data))
    def report(self, *data):
        data=bytes(data)
        return self.lib.input(data,len(data))

    def test_numbered_bitfield_press_release_and_held(self):
        self.assertTrue(self.parse('05 0c 09 01 a1 01 85 07 15 00 25 01 '
                                  '09 cd 09 b5 09 b6 09 e9 09 ea 09 e2 '
                                  '75 01 95 06 81 02 75 02 95 01 81 03 c0'))
        self.assertEqual(self.report(7,1),0x101)
        self.assertEqual(self.report(7,1),0x100) # held pause does not toggle again
        self.assertEqual(self.report(7,0),0)
        self.assertEqual(self.report(7,0x18),0x1818) # volume up and down
        self.assertEqual(self.report(7),-1) # truncated: retain held state
        self.assertEqual(self.report(8,0),-1) # unknown ID must not release keys
        self.assertEqual(self.report(7,0x18),0x1800)
        self.assertEqual(self.report(7,0),0)
        self.assertEqual(self.report(7,1),0x101)

    def test_sixteen_bit_usage_array_and_multiple_keys(self):
        self.assertTrue(self.parse('05 0c 09 01 a1 01 15 00 26 ff 03 19 00 2a ff 03 '
                                  '75 10 95 02 81 00 c0'))
        self.assertEqual(self.report(0xcd,0,0xe9,0),0x0909)
        self.assertEqual(self.report(0,0,0,0),0)
        self.assertEqual(self.report(0xb6,0,0xe2,0),0x2424)
        self.assertEqual(self.report(0,1,0,0),0) # unrelated Consumer usage

    def test_array_logical_minimum_and_explicit_usage_list(self):
        self.assertTrue(self.parse('05 0c 15 01 25 03 09 cd 09 b5 09 b6 75 08 95 01 81 00'))
        self.assertEqual(self.report(2),0x0202)
        self.assertTrue(self.parse('05 0c 15 01 25 02 19 e9 29 ea 75 08 95 01 81 00'))
        self.assertEqual(self.report(2),0x1010)

    def test_padding_nonconsumer_fields_and_push_pop(self):
        self.assertTrue(self.parse('05 01 09 30 75 03 95 01 81 02 '
                                  '05 0c 75 01 95 01 a4 '
                                  '05 01 75 04 95 01 09 31 81 02 b4 '
                                  '81 03 09 cd 81 02 75 07 95 01 81 03'))
        # First 3 generic bits + 4 pushed generic bits + 1 constant; pause at bit 8.
        self.assertEqual(self.report(255,1),0x101)
        self.assertEqual(self.report(255,0),0)

    def test_multiple_report_ids_preserve_other_reports(self):
        self.assertTrue(self.parse('05 0c 75 08 95 01 85 01 09 cd 81 02 '
                                  '85 02 09 e9 81 02'))
        self.assertEqual(self.report(1,1),0x101)
        self.assertEqual(self.report(2,1),0x0908)
        self.assertEqual(self.report(2,0),0x0100)
        self.assertEqual(self.report(1,1),0x0100)
        self.assertEqual(self.report(1,0),0)

    def test_captured_001f_0b21_headset_reports(self):
        # Exact 33-byte descriptor from the user's SD log: three Telephony bits
        # precede Next, Play/Pause, Previous, Volume Up, Volume Down. No report ID.
        self.assertTrue(self.parse(
            '05 0c 09 01 a1 01 75 01 05 0b 95 03 09 21 81 02 '
            '05 0c 09 b5 09 cd 09 b6 09 e9 09 ea 95 05 81 42 c0'))
        # Actual new SD capture: 40/00, 40/00, 80/00. The prompted physical
        # button labels cannot change what these identical packets decode to.
        for value, decoded in [(0x40, 0x0808), (0, 0),
                               (0x40, 0x0808), (0, 0),
                               (0x80, 0x1010), (0, 0)]:
            self.assertEqual(self.report(value), decoded)
        # Descriptor-derived possibilities, not claimed to have been observed.
        for value, decoded in [(0x08, 0x0202), (0x10, 0x0101), (0x20, 0x0404)]:
            self.assertEqual(self.report(value), decoded)
            self.assertEqual(self.report(0), 0)
        for telephony_bit in (1, 2, 4):
            self.assertEqual(self.report(telephony_bit), 0)
            self.assertEqual(self.report(0), 0)

    def test_malformed_and_unsupported_descriptors(self):
        for descriptor in ['05','fe','fe 05 00 00','b4','75 21 95 01 81 02',
                           '05 0c 19 ea 29 e9 75 01 95 01 81 02',
                           '05 01 09 30 75 08 95 01 81 02',
                           '85 00','05 0c 09 cd 75 01 95 01 81 02 85 01 09 cd 81 02']:
            with self.subTest(descriptor=descriptor): self.assertFalse(self.parse(descriptor))

if __name__ == '__main__': unittest.main()
