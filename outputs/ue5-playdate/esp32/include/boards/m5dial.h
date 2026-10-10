#pragma once
#include "board.h"
namespace M5DialBoard {
constexpr BoardConfig config={"m5dial",240,240,20,60,200,120,64,true};
constexpr int power_hold_pin=46;
/* Display/touch/encoder wiring is owned by the official M5Dial + M5GFX
   driver. Keep the physical map here for future custom board drivers. */
constexpr int lcd_dc=4,lcd_mosi=5,lcd_clock=6,lcd_cs=7,lcd_reset=8,lcd_backlight=9;
constexpr int i2c_sda=11,i2c_scl=12,touch_irq=14;
constexpr int encoder_a=41,encoder_b=40,button=42,buzzer=3;
constexpr int button_a_x=178,button_b_x=135,button_y=201;
constexpr int button_radius=18,touch_radius=23;
constexpr int dpad_x=68,dpad_y=201;
struct Point {int x,y;};
constexpr Point direction_points[4]={{50,200},{86,200},{68,187},{68,216}};
constexpr int menu_hold_ms=600;
constexpr uint32_t yellow=0xFFC600,ink=0x292D2C,button_fill=0xFFDE70;
constexpr uint16_t lcd_ink=0x2965,lcd_paper=0xD6B9;

/* One geometry source for painted controls and touch input. The round
   display cannot expose the square canvas's bottom corners. */
inline uint32_t touch_buttons(int x,int y) {
    int cx=x-config.width/2,cy=y-config.height/2;
    if(cx*cx+cy*cy>120*120 || y<182)return 0;
    int ay=y-button_y;
    int da=(x-button_a_x)*(x-button_a_x)+ay*ay;
    int db=(x-button_b_x)*(x-button_b_x)+ay*ay;
    if(da<=touch_radius*touch_radius && da<=db)return 32;
    if(db<=touch_radius*touch_radius)return 16;
    if(x<39 || x>103 || y>223)return 0;
    int dx=x-dpad_x,dy=y-dpad_y;
    int ax=dx<0 ? -dx : dx,by=dy<0 ? -dy : dy;
    if(ax<=5 && by<=5)return 0;
    return ax>by ? (dx<0 ? 1 : 2) : (dy<0 ? 4 : 8);
}
}
