"""Exercise the actual C PCM generator on the host; no ESP32 required."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Tone(ctypes.Structure):
    _fields_ = [("rate", ctypes.c_uint32), ("position", ctypes.c_uint32),
                ("channels", ctypes.c_uint)]


class BeepTest(unittest.TestCase):
    source = ROOT / "main/beep.c"

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        library = Path(cls.tmp.name) / "beep.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(cls.source), "-lm", "-o", str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.beep_fill.argtypes = [ctypes.POINTER(Tone), ctypes.POINTER(ctypes.c_uint8),
                                     ctypes.c_size_t]

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def render(self, tone, frames):
        output = (ctypes.c_uint8 * (frames * tone.channels * 2))()
        self.lib.beep_fill(ctypes.byref(tone), output, frames)
        return bytes(output)

    def test_supported_rates_channels_and_beep_timing(self):
        for rate in (44100, 48000):
            for channels in (1, 2):
                with self.subTest(rate=rate, channels=channels):
                    tone = Tone(rate, 0, channels)
                    raw = self.render(tone, rate * 2)
                    samples = struct.unpack("<" + "h" * (len(raw) // 2), raw)
                    mono = samples[::channels]
                    self.assertEqual(tone.position, 0)
                    self.assertEqual(mono[:rate], mono[rate:])
                    self.assertEqual(set(mono[rate // 4:rate]), {0})
                    self.assertGreater(max(mono), 2500)
                    self.assertLessEqual(max(abs(x) for x in mono), 2621)
                    self.assertEqual(mono[0], 0)
                    self.assertEqual(mono[rate // 4 - 1], 0)
                    if channels == 2:
                        self.assertEqual(samples[::2], samples[1::2])
                    # Count positive crossings in the flat envelope (200 ms = 176 cycles).
                    segment = mono[rate // 50:rate * 22 // 100]
                    crossings = sum(a <= 0 < b for a, b in zip(segment, segment[1:]))
                    self.assertAlmostEqual(crossings, 176, delta=1)
                    self.assertLess(abs(sum(mono[:rate]) / rate), 1)

    def test_arbitrary_chunk_boundaries_preserve_phase_and_envelope(self):
        for rate in (44100, 48000):
            tone = Tone(rate, 0, 2)
            sizes = [17, 882, 1, 960, 4096, rate, 123]
            chunked = b"".join(self.render(tone, n) for n in sizes)
            reference = self.render(Tone(rate, 0, 2), sum(sizes))
            self.assertEqual(chunked, reference)


class ArduinoBeepTest(BeepTest):
    source = ROOT / "arduino/LucidUacMp3Player/src/beep.c"


if __name__ == "__main__":
    unittest.main()
