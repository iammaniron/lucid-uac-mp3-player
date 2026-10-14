"""Run the real HID host state machine against a USB ownership/callback mock."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class HidMediaTest(unittest.TestCase):
    def test_attach_buttons_disconnect_during_control_and_input_and_reconnect(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=ROOT/'arduino/LucidUacMp3Player/src'
            binary=Path(tmp)/'hid_media'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'tests/host'),'-I'+str(src),
                            str(src/'hid_media.c'),str(src/'hid_consumer.c'),
                            str(ROOT/'tests/host/hid_media_harness.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
