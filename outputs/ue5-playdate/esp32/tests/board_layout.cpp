#include "boards/m5dial.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace M5DialBoard;
static void require(bool ok,const char *message) {
    if(!ok){std::fprintf(stderr,"%s\n",message);std::exit(1);}
}
static bool visible(int x,int y,int radius=0) {
    return std::hypot(x-120.,y-120.)+radius<=120;
}
int main() {
    require(visible(config.screen_x,config.screen_y),"Game top-left is cropped");
    require(visible(config.screen_x+config.screen_w,config.screen_y+config.screen_h),"Game bottom-right is cropped");
    require(config.screen_w*240==config.screen_h*400,"Game aspect ratio changed");
    require(visible(button_a_x,button_y,button_radius),"A outline is clipped by the round panel");
    require(visible(button_b_x,button_y,button_radius),"B outline is clipped by the round panel");
    for(int i=0;i<4;i++) {
        Point p=direction_points[i];
        require(visible(p.x,p.y,9),"Direction pressed indicator is clipped");
        for(int dx=-3;dx<=3;dx++)for(int dy=-3;dy<=3;dy++)
            require(touch_buttons(p.x+dx,p.y+dy)==(1u<<i),"Direction icon has the wrong touch mapping");
    }
    require(touch_buttons(button_a_x,button_y)==32,"A touch mapping is wrong");
    require(touch_buttons(button_b_x,button_y)==16,"B touch mapping is wrong");
    require(touch_buttons(dpad_x,dpad_y)==0,"D-pad center should be a dead zone");
    require(touch_buttons(0,239)==0,"Off-panel touch must not press a button");
    require(touch_buttons(120,120)==0,"Game image touch must not press a control");
    std::puts("Round display geometry and control targets passed");
}
