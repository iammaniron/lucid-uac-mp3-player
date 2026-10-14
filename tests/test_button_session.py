"""Exercise actual click grouping and bounded flash-record layout without hardware."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class ButtonSessionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory()
        root=Path(cls.tmp.name)
        source=root/'session_test.cpp'
        source.write_text(r'''
#include "session.h"
#include <cassert>
#include <cstdlib>
int main(int argc,char **argv) {
  assert(argc==2);
  const int test=atoi(argv[1]);
  ClickGroups s;
  if (test==1) {
    s.attach(100);
    assert(!s.ready(3099) && s.ready(3100));
    assert(s.input(1000,true)==0); // Enumeration/initial reports are not clicks.
    assert(s.input(3100,true)==1); // Press.
    assert(s.input(3220,true)==1); // Release belongs to the same group.
    assert(!s.poll(4599) && s.poll(4600));
    assert(s.completed==1 && !s.ready(6299) && s.ready(6300));
    assert(s.input(6400,true)==2 && s.input(6500,true)==2);
    assert(s.poll(7900) && s.completed==2);
    assert(!s.ready(9899) && s.ready(9900));
    assert(s.input(10000,true)==3 && s.input(10100,true)==3);
    assert(s.poll(11500) && s.completed==3 && !s.ready(99999));
    assert(s.input(99999,true)==0 && s.completed==3);
  } else if (test==2) {
    s.attach(0);
    assert(s.input(4000,false)==0); // Identical idle packets cannot start a group.
    assert(s.input(4100,true)==1);
    assert(s.input(5500,true)==1); // Another change keeps the capture window open.
    assert(!s.poll(5600) && !s.poll(6499) && s.poll(6500));
    assert(s.input(6600,true)==0); // Cooldown cannot create a second click.
    assert(s.completed==1);
  } else if (test==3) {
    s.attach(UINT32_MAX-1000);
    assert(!s.ready(1000) && s.ready(1999)); // Millisecond counter rollover.
    assert(s.input(2000,true)==1 && s.input(2100,true)==1 && s.poll(3500));
  } else if (test==4) {
    SavedCapture data;
    uint8_t bytes[64]; memset(bytes,0xaa,sizeof(bytes));
    data.descriptor(1,1,0,bytes,sizeof(bytes));
    assert(data.descriptorBytes==69);
    for (unsigned i=0;i<100;++i) data.report(0,i,1,3,3,0x83,bytes,sizeof(bytes));
    assert(data.reportCounts[0]==4 && data.reportsDropped[0]==96);
    for (unsigned g=1;g<4;++g) {
      for (unsigned i=0;i<20;++i) data.report(g,1000*g+i,1,3,3,0x83,bytes,sizeof(bytes));
      assert(data.reportCounts[g]==8 && data.reportsDropped[g]==12);
    }
    data.completed=3; data.finished=1;
    uint8_t flash[sizeof(data)]; memcpy(flash,&data,sizeof(data));
    SavedCapture restored; memcpy(&restored,flash,sizeof(restored));
    assert(restored.valid() && restored.completed==3 && restored.clicks[2][7].data[63]==0xaa);
    assert(restored.clicks[1][0].ms==2000);
    restored.clicks[2][0].length=65; assert(!restored.valid());
  } else if (test==5) {
    SavedCapture data;
    uint8_t huge[2048]={};
    data.descriptor(3,1,3,huge,sizeof(huge));
    assert(!data.descriptorBytes && data.descriptorsDropped==1);
    uint8_t report[]={1,4}; data.report(1,99,1,3,3,0x83,report,2);
    assert(data.reportCounts[1]==1 && data.clicks[0][0].data[1]==4 && data.valid());
    data.version=999; assert(!data.valid());
  } else return 1;
}
''')
        cls.binary=root/'session_test'
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'arduino/LucidUsbButtonTest/src'),str(source),
                        '-o',str(cls.binary)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def test_press_release_three_groups_and_cooldown(self): subprocess.run([str(self.binary),'1'],check=True)
    def test_idle_packets_and_quiet_period(self): subprocess.run([str(self.binary),'2'],check=True)
    def test_timer_rollover(self): subprocess.run([str(self.binary),'3'],check=True)
    def test_snapshot_roundtrip_and_reserved_space_per_click(self): subprocess.run([str(self.binary),'4'],check=True)
    def test_descriptor_overflow_preserves_click_records(self): subprocess.run([str(self.binary),'5'],check=True)
