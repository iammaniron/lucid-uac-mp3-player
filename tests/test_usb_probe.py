"""Exercise descriptor classification without a USB peripheral attached."""
import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Probe(ctypes.Structure):
    _fields_ = [(name, ctypes.c_bool) for name in
                ("valid", "audio", "uac1_output", "uac2", "hub")]


def config(*descriptors):
    payload = b"".join(bytes(d) for d in descriptors)
    length = 9 + len(payload)
    return bytes([9, 2, length & 255, length >> 8, 1, 1, 0, 0x80, 50]) + payload


def interface(cls=1, sub=2, protocol=0, alt=1):
    return [9, 4, 1, alt, 1, cls, sub, protocol, 0]


class UsbProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        target = Path(cls.tmp.name) / "probe.so"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "arduino/LucidUacMp3Player/src/usb_probe.c"),
                        "-o", str(target)], check=True)
        cls.lib = ctypes.CDLL(str(target))
        cls.lib.lucid_usb_probe.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
        cls.lib.lucid_usb_probe.restype = Probe

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def probe(self, data):
        return self.lib.lucid_usb_probe((ctypes.c_uint8 * len(data)).from_buffer_copy(data), len(data))

    def test_uac1_speaker_with_feedback_and_uac2(self):
        for protocol in (0, 0x20):
            result = self.probe(config(interface(protocol=protocol),
                                       [7, 5, 1, 5, 192, 0, 1],
                                       [7, 5, 0x81, 0x11, 3, 0, 1]))
            self.assertTrue(result.valid)
            self.assertTrue(result.audio)
            self.assertEqual(result.uac1_output, protocol == 0)
            self.assertEqual(result.uac2, protocol == 0x20)

    def test_microphone_bulk_and_inactive_interfaces_are_not_speakers(self):
        for endpoint, attributes, alt in ((0x81, 1, 1), (1, 2, 1), (1, 1, 0)):
            result = self.probe(config(interface(alt=alt), [7, 5, endpoint, attributes, 64, 0, 1]))
            self.assertTrue(result.valid)
            self.assertTrue(result.audio)
            self.assertFalse(result.uac1_output)

    def test_non_audio_and_hub(self):
        for cls in (3, 9):
            result = self.probe(config(interface(cls=cls, sub=0)))
            self.assertTrue(result.valid)
            self.assertFalse(result.audio)
            self.assertEqual(result.hub, cls == 9)

    def test_composite_device_does_not_attribute_hid_endpoint_to_audio(self):
        result = self.probe(config(interface(), interface(cls=3, sub=0), [7, 5, 1, 1, 64, 0, 1]))
        self.assertTrue(result.valid)
        self.assertTrue(result.audio)
        self.assertFalse(result.uac1_output)

    def test_malformed_descriptors_are_rejected(self):
        valid = config(interface(), [7, 5, 1, 1, 64, 0, 1])
        cases = [b"", valid[:8], valid[:-1], config([0, 4]), config([1, 4]),
                 config([20, 4, 0]), config([2, 4]), config([2, 5]), config([3])]
        for raw in cases:
            with self.subTest(raw=raw):
                self.assertFalse(self.probe(raw).valid)


if __name__ == "__main__":
    unittest.main()
