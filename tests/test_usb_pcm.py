"""Run actual UAC2 parsing, mode validation, PCM packing and USB packet timing."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class UsbPcmTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        (root/'esp_log.h').write_text('''#pragma once
#include <stdio.h>
#define ESP_LOGI(tag, ...) do { (void)(tag); if (0) fprintf(stderr, __VA_ARGS__); } while (0)
#define ESP_LOGW ESP_LOGI
#define ESP_LOGD ESP_LOGI
#define ESP_LOGE ESP_LOGI
''')
        src = ROOT/'arduino/LucidUacMp3Player/src'
        cls.binary = root/'test_usb_pcm'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
            '-I'+str(root),'-I'+str(src),'-I'+str(ROOT/'tests'),
            str(src/'usb_pcm.c'),str(src/'uac2/uac2_desc.c'),
            str(ROOT/'tests/usb_pcm_harness.c'),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def run_case(self, name): subprocess.run([str(self.binary),name],check=True)
    def test_real_minidsp_descriptor_and_alternates(self): self.run_case('descriptor')
    def test_malformed_and_uac1_descriptors_rejected(self): self.run_case('malformed')
    def test_pcm_widths_sign_and_in_place_packing(self): self.run_case('packing')
    def test_rate_ranges_and_invalid_audio_modes(self): self.run_case('modes')
    def test_exact_44100_and_48000_nominal_packet_counts(self): self.run_case('nominal')
    def test_feedback_at_48000_and_44100(self): self.run_case('feedback')
    def test_corrupt_feedback_and_frame_aligned_capacity(self): self.run_case('capacity')
