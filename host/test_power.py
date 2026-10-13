"""Compile the actual firmware power policy, including the overnight boundary."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_todo_touch import function

ROOT = Path(__file__).resolve().parents[1]

class PowerTests(unittest.TestCase):
    def check_policy(self, checks, prelude=''):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'power.cpp'
            binary = Path(tmp) / 'power'
            source.write_text('#include <cassert>\n#include "power_policy.h"\n' + prelude + '\nint main(){' + checks + '}')
            result = subprocess.run(['clang++', '-std=c++17', '-I'+str(ROOT/'firmware/src'), str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(subprocess.run([str(binary)]).returncode, 0)

    def test_overnight_and_morning_boundaries(self):
        self.check_policy('''
          uint32_t day=1767225600u;
          assert(nightSleepSeconds(day+20*3600-1)==0);
          assert(nightSleepSeconds(day+20*3600)==13*3600);
          assert(nightSleepSeconds(day+86400-1)==9*3600+1);
          assert(nightSleepSeconds(day+86400)==9*3600);
          assert(nightSleepSeconds(day+9*3600-1)==1);
          assert(nightSleepSeconds(day+9*3600)==0);
          for(uint32_t s=0;s<86400;s++){
            auto n=nightSleepSeconds(day+s);
            assert((n>0)==(s<9*3600 || s>=20*3600));
            if(n) assert((day+s+n)%86400==9*3600);
          }
        ''')

    def test_invalid_clock_and_temporary_wake(self):
        self.check_policy('''
          assert(nightSleepSeconds(0)==0);
          assert(nightSleepSeconds(100)==0);
          uint32_t now=1767225600u+21*3600;
          assert(nightSleepSeconds(now,now+600)==0);
          assert(nightSleepSeconds(now+599,now+600)==0);
          assert(nightSleepSeconds(now+600,now+600)==12*3600-600);
        ''')

    def test_brightness_and_face_down(self):
        self.check_policy('''
          assert(powerBrightness(180,false,false,0,false)==110);
          assert(powerBrightness(180,false,false,29999,false)==110);
          assert(powerBrightness(180,false,false,30000,false)==40);
          assert(powerBrightness(180,false,true,60000,false)==110);
          assert(powerBrightness(180,true,true,60000,false)==180);
          assert(powerBrightness(180,true,false,21*60000,false)==40);
          assert(powerBrightness(180,false,true,0,true)==0);
        ''')

    def test_render_cadence_keeps_interaction_fast(self):
        self.check_policy('''
          assert(powerFrameInterval(true,false,false)==67);
          assert(powerFrameInterval(false,false,false)==1000);
          assert(powerFrameInterval(false,true,false)==33);
          assert(powerFrameInterval(false,false,true)==33);
        ''')

    def test_rtc_alarm_disables_day_and_weekday_match(self):
        rtc = (ROOT/'firmware/src/boardrtc.cpp').read_text()
        prelude = '''
          #include <vector>
          static bool validClock=true;
          static bool rtcRead(){return validClock;}
          static struct {
            bool enabled=false;
            void disableAlarm(){enabled=false;}
            void resetAlarm(){}
            void enableAlarm(){enabled=true;}
          } rtc;
          static struct {
            std::vector<uint8_t> data;
            int addr=0, error=0;
            void beginTransmission(int a){addr=a;data.clear();}
            void write(const uint8_t* p, unsigned n){data.assign(p,p+n);}
            int endTransmission(){return error;}
          } Wire;
        ''' + function(rtc, 'bool rtcArmMorning(')
        self.check_policy('''
          validClock=false;assert(!rtcArmMorning());assert(!rtc.enabled);
          validClock=true;Wire.error=1;assert(!rtcArmMorning());assert(!rtc.enabled);
          Wire.error=0;assert(rtcArmMorning());assert(rtc.enabled);assert(Wire.addr==0x51);
          assert(Wire.data==std::vector<uint8_t>({0x0B,0,0,9,0x80,0x80}));
        ''', prelude)

if __name__ == '__main__': unittest.main()
