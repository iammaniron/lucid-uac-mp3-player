"""Actual SD log buffering/verification and text export against a fault-injecting card."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class ButtonSdTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        root=Path(cls.tmp.name)
        harness=root/'sd_test.cpp'
        harness.write_text(r'''
#include "sd_log.h"
#include "capture_text.h"
#include <cassert>
#include <cstdlib>
int main(int argc,char **argv) {
    assert(argc==2);
    int test=atoi(argv[1]);
    SdCaptureLog log;
    if (test==1) {
        card.contents="existing user text\n";
        assert(log.begin());
        std::string large(1700,'x');
        assert(log.write((const uint8_t*)large.data(),large.size())==large.size());
        assert(log.checkpoint());
        assert(card.contents=="existing user text\n"+large);
        log.println("next group"); assert(log.checkpoint(false));
        assert(card.contents=="existing user text\n"+large+"next group\n");
        assert(card.flushes==2);
    } else if (test==2) {
        assert(log.begin()); card.writeBudget=3;
        log.println("more bytes than card space");
        assert(!log.checkpoint() && log.error());
        assert(!log.begin()); // Never acknowledge or silently retry after a short write.
    } else if (test==3) {
        assert(log.begin()); log.println("verify me");card.readCorrupt=true;
        assert(!log.checkpoint() && log.error());
    } else if (test==4) {
        card.openFails=true;assert(!log.begin());assert(!log.checkpoint());
    } else if (test==5) {
        assert(log.begin());
        uint8_t press[]={0x01,0xcd,0x00},release[]={1,0,0};
        capture_raw_text(log,1,capture_button_label(1),12034,1,3,3,0x83,press,3);
        capture_raw_text(log,1,capture_button_label(1),12200,1,3,3,0x83,release,3);
        capture_raw_text(log,2,capture_button_label(2),14000,1,3,3,0x83,press,3);
        assert(log.checkpoint(false));
        assert(card.contents.find("t_ms=12034 group=1 expected_button=\"CENTRE/PAUSE\"")!=std::string::npos);
        assert(card.contents.find("len=3 : 01 CD 00")!=std::string::npos);
        assert(card.contents.find("t_ms=12200")!=std::string::npos);
        assert(card.contents.find("len=3 : 01 00 00")!=std::string::npos);
        assert(card.contents.find("expected_button=\"VOLUME +\"")!=std::string::npos);
    } else if (test==6) {
        SavedCapture old;
        const uint8_t bytes[]={1,4};
        old.report(1,123,1,3,3,0x83,bytes,2);old.completed=1;
        old.descriptor(3,1,3,bytes,2);
        assert(old.valid() && log.begin());
        capture_recovered_text(log,old);assert(log.checkpoint(false));
        assert(card.contents.find("RECOVERED PREVIOUS FLASH CAPTURE")!=std::string::npos);
        assert(card.contents.find("HID_REPORT_DESC addr=1 if=3 bytes=2")!=std::string::npos);
        assert(card.contents.find("t_ms=123 group=1 expected_button=\"UNKNOWN (previous test)\"")!=std::string::npos);
        assert(card.contents.find("CENTRE/PAUSE")==std::string::npos); // Don't invent old button identities.
    } else return 1;
}
''')
        cls.binary=root/'sd_test'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'tests/host_sd'),'-I'+str(ROOT/'arduino/LucidUsbButtonTest/src'),
                        str(harness),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def test_append_buffer_boundaries_preserve_existing_file(self): subprocess.run([str(self.binary),'1'],check=True)
    def test_short_write_never_acknowledged(self): subprocess.run([str(self.binary),'2'],check=True)
    def test_readback_corruption_is_reported(self): subprocess.run([str(self.binary),'3'],check=True)
    def test_missing_card_is_reported(self): subprocess.run([str(self.binary),'4'],check=True)
    def test_press_release_labels_timestamps_and_hex(self): subprocess.run([str(self.binary),'5'],check=True)
    def test_old_capture_recovery_does_not_invent_button_names(self): subprocess.run([str(self.binary),'6'],check=True)
