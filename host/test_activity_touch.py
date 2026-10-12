from pathlib import Path
import subprocess
import tempfile
import unittest
from test_todo_touch import function, ROOT

class ActivityTouchTests(unittest.TestCase):
    def test_completion_lifetime_merge_dedup_and_details(self):
        pages=(ROOT/'firmware/src/pages.cpp').read_text()
        main=(ROOT/'firmware/src/main.cpp').read_text()
        source=r'''#include <ArduinoJson.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using std::min;
static uint32_t tick=1000;
static uint32_t millis(){return tick;}
static int constrain(int x,int a,int b){return std::max(a,std::min(x,b));}
struct Arduino_Canvas{};
static int completionBody(Arduino_Canvas*,const char*,int){return 2;}
static uint8_t shownRot;
'''
        source+=function(main,'static void canvasPageXY(')
        source+=pages[pages.index('static int activityN'):pages.index('// Layout once per frame;')]
        source+=function(pages,'bool completionTouch(')
        source+=r'''
static void send(const char* id,const char* title){JsonDocument d;d["item"]["id"]=id;d["item"]["title"]=title;d["item"]["body"]="reply";completedSet(d);}
static void nativePoint(int px,int py,int& x,int& y){switch((4-shownRot)&3){case 1:x=479-py;y=px;break;case 2:x=479-px;y=479-py;break;case 3:x=py;y=479-px;break;default:x=px;y=py;}}
#define CHECK(x) if(!(x))return __LINE__;
int main(){
 JsonDocument activity;activity["n"]=3;activitySet(activity);CHECK(activityN==3);
 activity["n"]=nullptr;activitySet(activity);CHECK(activityN==-1);
 activity["n"]=-2;activitySet(activity);CHECK(activityN==-1);
 send("a","first");CHECK(completionVisible(8999));CHECK(!completionVisible(9000));
 tick=2000;send("a","duplicate");CHECK(completedTotal==1);CHECK(completedUntil==9000);
 send("b","second");CHECK(completedTotal==2);CHECK(completedUntil==9000);
 for(shownRot=0;shownRot<4;shownRot++){
 int x,y,px,py;nativePoint(240,399,x,y);canvasPageXY(x,y,px,py);
 completedUntil=9000;detailOpen=false;CHECK(completionTouch(px,py,0,0,2000));CHECK(detailOpen);
 CHECK(detailN==2);CHECK(!strcmp(detailItems[detailIndex].title,"second"));
 CHECK(completionTouch(240,200,0,-100,10000));CHECK(detailPage==1);CHECK(detailOpen);
 CHECK(completionTouch(240,200,100,0,10000));CHECK(detailIndex==0);CHECK(detailPage==0);
 CHECK(completionTouch(30,40,0,0,10000));CHECK(!detailOpen);
 }
 tick=10000;send("c","third");CHECK(completedTotal==1);CHECK(completedUntil==18000);
 CHECK(completionTouch(240,399,0,0,10000));CHECK(detailOpen);
 tick=10001;send("d","fourth");CHECK(!strcmp(detailItems[detailIndex].title,"third"));
 CHECK(completionTouch(30,40,0,0,10001));CHECK(!detailOpen);
 CHECK(!completionTouch(240,399,0,0,18001));
 tick=20000;
 for(int i=0;i<5;i++){char id[8];snprintf(id,sizeof(id),"batch%d",i);send(id,id);}
 CHECK(completedTotal==5);CHECK(completedN==4);CHECK(!strcmp(completedItems[0].title,"batch1"));
 CHECK(completionTouch(240,399,0,0,20000));CHECK(detailN==4);CHECK(detailIndex==3);
 CHECK(!strcmp(detailItems[detailIndex].title,"batch4"));
 CHECK(completionTouch(30,40,0,0,20000));
 completedN=0;completedUntil=0;tick=0xfffffff0;send("e","wrap");CHECK(completionVisible(tick+7999));CHECK(!completionVisible(tick+8000));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';cpp.write_text(source)
            include=ROOT/'firmware/.pio/libdeps/amoled216/ArduinoJson/src'
            built=subprocess.run(['clang++','-std=c++17','-I'+str(include),str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            result=subprocess.run([str(exe)])
            self.assertEqual(result.returncode,0,source.splitlines()[result.returncode-1] if result.returncode else '')
if __name__=='__main__':unittest.main()
