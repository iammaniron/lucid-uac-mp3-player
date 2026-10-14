"""Actual live-player trace preserves raw reports and defers card writes out of HID callbacks."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class ButtonTraceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        root=Path(cls.tmp.name)
        src=ROOT/'arduino/LucidUacMp3Player/src'
        harness=root/'trace_test.cpp'
        harness.write_text(r'''
#include <Arduino.h>
#include <SD.h>
#include "button_trace.h"
#include <cassert>
#include <cstdlib>
extern "C" bool lucid_music_paused(void) { return false; }
extern "C" unsigned lucid_music_volume(void) { return 50; }
int main(int argc,char **argv) {
  assert(argc==2);
  int test=atoi(argv[1]);
  lucid_button_trace_begin(true);
  assert(lucid_button_trace_status()==1);
  assert(card.contents.find("LIVE MUSIC BUTTON TRACE")!=std::string::npos);
  uint8_t up=0x40, zero=0, telephone=1;
  if (test==1) {
    mockMillis=123;
    lucid_button_trace_report(1,3,0x82,&up,1,true,8,true);
    ++mockMillis; lucid_button_trace_report(1,3,0x82,&zero,1,true,0,true);
    ++mockMillis; lucid_button_trace_report(1,3,0x82,&telephone,1,true,0,true);
    assert(lucid_button_trace_last()==1);
    assert(card.contents.find("RAW ")==std::string::npos); // Callbacks only copy into RAM.
    mockMillis=2000; lucid_button_trace_poll(300,0);
    assert(card.contents.find("RAW t_ms=123 addr=1 if=3 ep=82 streaming=1 paused_before=0 volume_before=50 decoded=1 pressed=08 len=1 : 40")!=std::string::npos);
    assert(card.contents.find("pressed=00 len=1 : 00")!=std::string::npos);
    assert(card.contents.find("pressed=00 len=1 : 01")!=std::string::npos); // Unmapped reports still captured.
    assert(card.contents.find("UAC_completed=300 UAC_errors=0 dropped_reports=0")!=std::string::npos);
  } else if (test==2) {
    for (unsigned i=0;i<70;++i) lucid_button_trace_report(1,3,0x82,&up,1,true,8,false);
    mockMillis=2000; lucid_button_trace_poll(0,0);
    assert(card.contents.find("streaming=0")!=std::string::npos);
    assert(card.contents.find("dropped_reports=6")!=std::string::npos);
  } else if (test==3) {
    lucid_button_trace_report(1,3,0x82,&up,1,true,8,true);
    card.writeBudget=2; mockMillis=2000; lucid_button_trace_poll(100,0);
    assert(lucid_button_trace_status()==2);
  } else return 1;
}
''')
        cls.binary=root/'trace_test'
        subprocess.run(['c++','-std=c++17','-DLUCID_BUTTON_TRACE=1','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'tests/host_sd'),'-I'+str(src),
                        str(src/'button_trace.cpp'),str(harness),'-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def test_streaming_context_raw_releases_and_unmapped_reports(self): subprocess.run([str(self.binary),'1'],check=True)
    def test_overflow_and_no_stream_are_explicit(self): subprocess.run([str(self.binary),'2'],check=True)
    def test_card_write_failure_disables_only_logging(self): subprocess.run([str(self.binary),'3'],check=True)
