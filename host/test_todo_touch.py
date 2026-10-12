#!/usr/bin/python3
"""Run the real task hit handler against pixels placed by Arduino_Canvas."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class TaskTouchTest(unittest.TestCase):
    def test_rotated_task_hit_targets(self):
        main = (ROOT / 'firmware/src/main.cpp').read_text()
        pages = (ROOT / 'firmware/src/pages.cpp').read_text()
        libs = ROOT / 'firmware/.pio/libdeps/amoled216'
        canvas = (libs / 'GFX Library for Arduino/src/canvas/Arduino_Canvas.cpp').read_text()
        branch = main.split('} else if (shownPage == PAGE_CALENDAR) {', 1)[1].split('} else if (shownPage == PAGE_CLOCK', 1)[0]
        source = r'''#include <ArduinoJson.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
using std::min;
static uint32_t millis() { return 1000; }
static int constrain(int x, int a, int b) { return std::max(a, std::min(x, b)); }
struct Arduino_Canvas {
 uint16_t pixels[480*480] = {};
 uint16_t* _framebuffer = pixels;
 int _rotation=0, _height=480, _width=480, _max_x=479, _max_y=479;
 void writePixelPreclipped(int16_t, int16_t, uint16_t);
};
static uint8_t shownRot;
static int tpLastX, tpLastY, tpStartX, tpStartY, vdx, vdy;
static JsonDocument sent;
static void sendJson(JsonDocument& d) { sent.set(d); }
static void noteActivity() {}
'''
        if 'static void canvasPageXY(' in main:
            source += function(main, 'static void canvasPageXY(')
        source += function(canvas, 'void Arduino_Canvas::writePixelPreclipped')
        source += function(main, 'static void pageXY(')
        source += main[main.index('enum ClockView'):main.index('// Which source the play page')]
        clock_branch = function(main, '} else if (shownPage == PAGE_CLOCK) {')
        source += '\nstatic const int SWIPE_MIN_PX=40, SND_SELECT=0, SND_TICK=1;\nstatic void audioPlay(int){}\n'
        source += 'static void dispatchClock(){' + clock_branch[clock_branch.index('{')+1:-1] + '}\n'
        source += pages[pages.index('struct TodoItem'):pages.index('static void todoSort()')]
        source += function(pages, 'static TodoItem* todoChosen()')
        source += function(pages, 'static uint32_t focusRemaining(')
        source += function(pages, 'bool todoTouch(')
        source += '\nstatic void dispatch() {' + branch + '}\n'
        source += r'''
static void nativePoint(int x, int y, int& nx, int& ny) {
 Arduino_Canvas c; c._rotation=(4-shownRot)&3;
 c.writePixelPreclipped(x,y,1);
 for(int i=0;i<480*480;i++) if(c.pixels[i]) { nx=i%480; ny=i/480; return; }
 std::abort();
}
static void gesture(int x,int y,int sx,int sy) {
 nativePoint(x,y,tpLastX,tpLastY);nativePoint(sx,sy,tpStartX,tpStartY);
 int dx=tpLastX-tpStartX,dy=tpLastY-tpStartY;
 switch((4-shownRot)&3) {
 case 1:vdx=-dy;vdy=dx;break;case 2:vdx=-dx;vdy=-dy;break;
 case 3:vdx=dy;vdy=-dx;break;default:vdx=dx;vdy=dy;
 }
 sent.clear();todoBusy=false;todoAt=1000;dispatch();
}
static bool expect(bool yes,const char* name) {
 if(!yes)std::cerr<<"rotation "<<int(shownRot)<<": "<<name<<" FAIL\n";
 return yes;
}
int main() {
 bool ok=expect(clockView==CV_USAGE,"quota is the initial page");
 for(shownRot=0;shownRot<4;shownRot++) {
 clockView=CV_USAGE;
 nativePoint(160,240,tpLastX,tpLastY);nativePoint(300,240,tpStartX,tpStartY);dispatchClock();
 ok &= expect(clockView==CV_CLOCK,"left swipe opens clock second");
 nativePoint(300,240,tpLastX,tpLastY);nativePoint(160,240,tpStartX,tpStartY);dispatchClock();
 ok &= expect(clockView==CV_USAGE,"right swipe returns to quota");
 todoPending[0]=0;todoN=4;todoRev=7;strcpy(todoCurrent,"task");
 for(int i=0;i<4;i++){todoOrder[i]=i;strcpy(todoItems[i].id,i?"other":"task");todoItems[i].done=false;}
 todoView=1;gesture(126,389,126,389);
 ok &= expect(sent["done"].is<bool>() && sent["done"]==true,"detail left completes");
 ok &= expect(!strcmp(todoPending,"task") && todoPendingDone,"detail completion has immediate feedback");
 todoView=1;gesture(347,389,347,389);
 ok &= expect(todoView==2 && sent.isNull() && focusRunning,"detail right focuses");
 todoView=2;focusRunning=true;focusEnd=1501000;gesture(126,389,126,389);
 ok &= expect(!focusRunning && sent.isNull(),"focus left pauses");
 todoView=2;focusRunning=true;gesture(347,389,347,389);
 ok &= expect(sent["done"].is<bool>() && sent["done"]==true,"focus right completes");
 todoView=0;todoOffset=0;gesture(47,176,47,176);
 ok &= expect(sent["done"].is<bool>() && sent["done"]==true,"list circle completes");
 todoView=0;gesture(150,176,150,176);
 ok &= expect(todoView==1 && sent.isNull(),"list title opens details");
 todoView=0;todoOffset=0;gesture(220,200,220,300);
 ok &= expect(todoOffset==3,"up swipe advances page");
 gesture(220,300,220,200);ok &= expect(todoOffset==0,"down swipe returns page");
 }
 return ok?0:1;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'touch.cpp'
            binary = Path(directory) / 'touch'
            cpp.write_text(source)
            built = subprocess.run(['clang++', '-std=c++17', '-I'+str(libs/'ArduinoJson/src'), str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stderr)


if __name__ == '__main__':
    unittest.main()
