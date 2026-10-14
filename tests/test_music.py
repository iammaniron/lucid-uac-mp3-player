"""Real Helix decoding + actual playlist/renderer, with host-backed SD File I/O."""
import ctypes
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'arduino/LucidUacMp3Player/src'


class MusicTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # This test writes compiler artifacts only into its temporary directory.
        os.environ['CCACHE_DISABLE'] = '1'
        cls.tmp = tempfile.TemporaryDirectory()
        cls.build = Path(cls.tmp.name)
        helix = os.environ.get('HELIX_DIR')
        if not helix:
            libs = json.loads(subprocess.check_output(['arduino-cli','lib','list','--format','json']))
            helix = next(x['library']['source_dir'] for x in libs['installed_libraries']
                         if x['library']['name'] == 'libhelix')
        helix = Path(helix)
        objects = []
        for source in sorted((helix / 'libhelix-mp3').glob('*.c')):
            target = cls.build / (source.stem + '.o')
            subprocess.run(['cc','-O2','-fPIC','-I'+str(helix),'-c',str(source),'-o',str(target)],check=True)
            objects.append(str(target))
        library = cls.build / 'music.so'
        subprocess.run(['c++','-std=c++17','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'tests/host'),'-I'+str(SRC),'-I'+str(helix),
                        str(SRC/'track_decoder.cpp'),str(SRC/'music.cpp'),str(ROOT/'tests/host/music_harness.cpp'),
                        *objects,'-o',str(library)],check=True)
        cls.lib=ctypes.CDLL(str(library))
        cls.lib.host_setup.argtypes=[ctypes.c_char_p,ctypes.c_uint,ctypes.c_uint]
        cls.lib.lucid_music_read.argtypes=[ctypes.POINTER(ctypes.c_uint8),ctypes.c_size_t]
        cls.lib.lucid_music_read.restype=ctypes.c_size_t
        cls.lib.lucid_music_available.restype=ctypes.c_bool
        cls.lib.host_title.argtypes=[ctypes.c_uint]
        cls.lib.host_title.restype=ctypes.c_char_p
        cls.lib.host_status.restype=ctypes.c_char_p

    @classmethod
    def tearDownClass(cls):
        cls.lib.lucid_music_stop()
        cls.tmp.cleanup()

    def setUp(self):
        self.card=tempfile.TemporaryDirectory()
        self.root=Path(self.card.name)

    def tearDown(self):
        self.lib.lucid_music_stop()
        self.card.cleanup()

    def wav(self,name,samples,rate=48000,channels=1):
        path=self.root/name
        path.parent.mkdir(parents=True,exist_ok=True)
        with wave.open(str(path),'wb') as f:
            f.setnchannels(channels);f.setsampwidth(2);f.setframerate(rate)
            f.writeframes(struct.pack('<'+'h'*len(samples),*samples))
        return path

    def start(self,rate=48000,channels=2):
        self.channels=channels
        self.lib.host_setup(os.fsencode(self.root),rate,channels)

    def read(self,frames=480):
        output=(ctypes.c_uint8*(frames*self.channels*2))()
        count=self.lib.lucid_music_read(output,frames)
        return struct.unpack('<'+'h'*(count*self.channels),bytes(output)[:count*self.channels*2])

    def test_playlist_nested_hidden_bad_skipped_and_repeats(self):
        (self.root/'00bad.mp3').write_bytes(b'not an mp3'*200)
        self.wav('02.wav',[-2000]*480)
        self.wav('album/01.WAV',[1000]*480)
        self.wav('.hidden.wav',[3000]*480)
        self.wav('.hidden/x.wav',[3000]*480)
        self.start()
        output=[self.read() for _ in range(8)]
        # Native directory order varies by filesystem. Exactly one silence chunk
        # skips the bad file; the two real songs alternate on every later pass.
        self.assertEqual(sum(set(x)=={0} for x in output),1)
        music=[x for x in output if set(x)!={0}]
        titles=[self.lib.host_title(i).decode() for i in range(len(music))]
        self.assertEqual(set(titles),{'02.wav','01.WAV'})
        for i,x in enumerate(music):
            self.assertEqual(x[::2],x[1::2])
            self.assertEqual(set(x),{-1000 if titles[i]=='02.wav' else 500})
            if i>=2:self.assertEqual(titles[i],titles[i-2])

    def test_stereo_to_mono_and_reconnect_restarts_playlist(self):
        self.wav('a.wav',[2000,-1000]*480,channels=2)
        self.start(channels=1)
        self.assertEqual(set(self.read()),{250})
        self.lib.lucid_music_stop();self.lib.lucid_music_start(48000,1)
        self.assertEqual(set(self.read()),{250})

    def test_pause_preserves_position_and_volume_mute_keep_timeline(self):
        self.wav('ramp.wav',list(range(2000,12000)))
        self.start(channels=1)
        first=self.read(100)
        expected=self.read(100)
        self.start(channels=1)
        self.assertEqual(self.read(100),first)
        self.lib.lucid_music_control(0xcd)
        for _ in range(5): self.assertEqual(set(self.read(100)),{0})
        self.assertEqual(self.lib.host_status(),b'PAUSED')
        self.lib.lucid_music_control(0xcd)
        self.assertEqual(self.read(100),expected)
        self.assertEqual(self.lib.host_status(),b'PLAYING')
        for _ in range(30): self.lib.lucid_music_control(0xe9)
        self.assertEqual(self.lib.lucid_music_volume(),100)
        self.assertEqual(self.read(100),tuple(range(2200,2300)))
        self.lib.lucid_music_control(0xe2)
        self.assertEqual(set(self.read(100)),{0})
        self.lib.lucid_music_control(0xe2)
        self.assertEqual(self.read(100),tuple(range(2400,2500)))
        for _ in range(30): self.lib.lucid_music_control(0xea)
        self.assertEqual(self.lib.lucid_music_volume(),0)
        self.assertEqual(set(self.read(100)),{0})
        self.lib.lucid_music_control(0xe9)
        self.assertEqual(self.lib.lucid_music_volume(),5)

    def test_buttons_next_previous_wrap_and_navigation_while_paused(self):
        for i in range(3): self.wav(f'{i}.wav',[1000*(i+1)]*10000)
        self.start(channels=1)
        order=[]
        for _ in range(3):
            order.append(self.read(100)[0])
            self.lib.lucid_music_control(0xb5)
        self.assertEqual(len(set(order)),3)
        self.assertEqual(self.read(100)[0],order[0])
        self.lib.lucid_music_control(0xb6)
        self.assertEqual(self.read(100)[0],order[2])
        self.lib.lucid_music_control(0xb6)
        self.assertEqual(self.read(100)[0],order[1])
        self.lib.lucid_music_control(0xb1)
        self.lib.lucid_music_control(0xb5)
        self.assertEqual(set(self.read(100)),{0})
        self.assertEqual(self.lib.host_status(),b'PAUSED')
        self.lib.lucid_music_control(0xb0)
        self.assertEqual(self.read(100)[0],order[2])

    def test_resampling_duration_pitch_and_chunk_boundaries(self):
        for source_rate,target_rate in [(44100,48000),(48000,44100),(22050,48000)]:
            with self.subTest(source_rate=source_rate,target_rate=target_rate):
                self.wav('sine.wav',[int(12000*math.sin(2*math.pi*1000*i/source_rate))
                                     for i in range(source_rate)],source_rate)
                self.start(target_rate,1)
                samples=[]
                while len(samples)<target_rate:
                    samples.extend(self.read(min(137,target_rate-len(samples))))
                crossings=sum(a<=0<b for a,b in zip(samples,samples[1:]))
                self.assertAlmostEqual(crossings,1000,delta=1)
                self.assertEqual(self.lib.host_played(),1)
                self.read(2)
                self.assertEqual(self.lib.host_played(),2)
                self.assertLessEqual(max(samples),6000)

    def test_empty_and_all_invalid_stop_without_spinning(self):
        self.start();self.assertEqual(self.read(),());self.assertFalse(self.lib.lucid_music_available())
        (self.root/'bad.mp3').write_bytes(b'\xff'*9000)
        (self.root/'truncated.wav').write_bytes(b'RIFF')
        self.start();self.assertEqual(set(self.read()),{0})
        self.assertEqual(set(self.read()),{0})
        self.assertEqual(self.read(),());self.assertFalse(self.lib.lucid_music_available())
        self.assertEqual(self.lib.host_status(),b'NO PLAYABLE TRACKS')

    def test_large_playlist_has_no_256_track_limit(self):
        for i in range(270):self.wav(f'{i:03}.wav',[1000]*10)
        self.start(channels=1)
        for _ in range(271):self.assertEqual(set(self.read(10)),{500})
        self.assertEqual(self.lib.host_played(),271)
        self.assertEqual(self.lib.host_title(0),self.lib.host_title(270))

    @unittest.skipUnless(shutil.which('ffmpeg'),'ffmpeg needed for real MP3 fixtures')
    def test_real_mp3_cbr_vbr_mono_stereo_and_large_id3(self):
        for rate,channels,vbr in [(44100,2,False),(48000,2,True),(22050,1,False)]:
            with self.subTest(rate=rate,channels=channels,vbr=vbr):
                path=self.root/'test.mp3'
                subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i',
                                f'sine=frequency=880:sample_rate={rate}:duration=1',
                                '-ac',str(channels),'-c:a','libmp3lame',
                                *(['-q:a','3'] if vbr else ['-b:a','320k' if rate==44100 else '128k']),str(path)],check=True)
                # Simulate album artwork in a leading ID3v2 tag, without loading it into the decoder.
                n=200000
                tag=b'ID3\x04\x00\x00'+bytes([(n>>21)&127,(n>>14)&127,(n>>7)&127,n&127])+bytes(n)
                path.write_bytes(tag+path.read_bytes())
                self.start()
                samples=[]
                for _ in range(150):samples.extend(self.read()[::2])
                self.assertGreaterEqual(self.lib.host_played(),2)
                section=samples[12000:36000]
                crossings=sum(a<=0<b for a,b in zip(section,section[1:]))
                self.assertAlmostEqual(crossings,440,delta=3)
                self.assertGreater(max(section),500)


if __name__=='__main__':unittest.main()
