#include "board.h"
#include "boards/m5dial.h"
#include <M5Dial.h>
#include <Arduino.h>
#include <math.h>

#if !defined(CRANK_BOARD_M5DIAL)
#error Select a supported board configuration in platformio.ini
#endif
static constexpr BoardConfig config=M5DialBoard::config;
static long encoder_position;
static bool controls_visible;
static uint32_t painted_buttons;
const BoardConfig &board_config() { return config; }
extern "C" uint64_t micro_board_millis() { return millis(); }

static void draw_controls(uint32_t buttons) {
    using namespace M5DialBoard;
    auto &lcd=M5Dial.Display;
    lcd.fillRect(0,182,M5DialBoard::config.width,M5DialBoard::config.height-182,yellow);
    lcd.fillRoundRect(43,190,50,20,5,button_fill);
    lcd.fillRoundRect(58,182,20,39,5,button_fill);
    const uint32_t masks[4]={1,2,4,8};
    for(int i=0;i<4;i++) {
        int x=direction_points[i].x,y=direction_points[i].y;bool pressed=buttons&masks[i];
        uint32_t color=pressed ? yellow : ink;
        if(pressed)lcd.fillCircle(x,y,9,ink);
        if(i==0)lcd.fillTriangle(x-5,y,x+4,y-5,x+4,y+5,color);
        if(i==1)lcd.fillTriangle(x+5,y,x-4,y-5,x-4,y+5,color);
        if(i==2)lcd.fillTriangle(x,y-5,x-5,y+4,x+5,y+4,color);
        if(i==3)lcd.fillTriangle(x,y+5,x-5,y-4,x+5,y-4,color);
    }
    lcd.setTextDatum(middle_center);
    const int xs[2]={button_b_x,button_a_x};
    const char *labels[2]={"B","A"};
    for(int i=0;i<2;i++) {
        bool pressed=buttons&(16u<<i);
        lcd.fillCircle(xs[i],button_y,button_radius,pressed ? ink : button_fill);
        lcd.setTextColor(pressed ? yellow : ink,pressed ? ink : button_fill);
        lcd.drawString(labels[i],xs[i],button_y);
    }
    painted_buttons=buttons;
}

void board_begin() {
    static bool initialized=false;
    if(!initialized) {
        pinMode(M5DialBoard::power_hold_pin,OUTPUT); digitalWrite(M5DialBoard::power_hold_pin,HIGH);
        auto cfg=M5.config();cfg.internal_imu=false;cfg.internal_rtc=false;
        M5Dial.begin(cfg,true,false);initialized=true;
        board_diagnostics();
    }
    M5Dial.Display.setBrightness(128);
    M5Dial.Display.fillScreen(M5DialBoard::yellow);
    M5Dial.Display.setTextColor(M5DialBoard::ink,M5DialBoard::yellow);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::Font2);
    M5Dial.Display.drawString("PLAYDATE",120,30);
    controls_visible=true;draw_controls(0);
    encoder_position=M5Dial.Encoder.read();
}
void board_diagnostics() {
    auto touch=M5Dial.Touch.getDetail();
    Serial.printf("HARDWARE {\"m5dialDetected\":%s,\"touchEnabled\":%s,\"width\":%d,\"height\":%d,\"encoderRaw\":%ld,\"touchX\":%d,\"touchY\":%d,\"touchPressed\":%s}\n",
        M5.getBoard()==m5::board_t::board_M5Dial ? "true" : "false",
        M5Dial.Touch.isEnabled() ? "true" : "false",M5Dial.Display.width(),M5Dial.Display.height(),
        M5Dial.Encoder.read(),touch.x,touch.y,touch.isPressed() ? "true" : "false");
}
BoardInput board_poll() {
    M5Dial.update(); BoardInput result={0,0,false};
    long current=M5Dial.Encoder.read();
    result.crank_delta=(current-encoder_position)*360.f/config.encoder_pulses_per_rotation;
    encoder_position=current;
    for(unsigned i=0;i<M5Dial.Touch.getCount();i++) {
        auto touch=M5Dial.Touch.getDetail(i);
        if(touch.isPressed())result.buttons|=M5DialBoard::touch_buttons(touch.x,touch.y);
    }
    if(M5Dial.BtnA.isPressed() && !M5Dial.BtnA.pressedFor(M5DialBoard::menu_hold_ms)) result.buttons|=1<<5;
    result.menu_pressed=M5Dial.BtnA.wasReleaseFor(M5DialBoard::menu_hold_ms);
    if(controls_visible && result.buttons!=painted_buttons)draw_controls(result.buttons);
    return result;
}
void board_present(const uint8_t *bits,bool inverted,int scale) {
    if(!bits) return;
    const int logical_scale=(scale==2 || scale==4 || scale==8) ? scale : 1;
    uint16_t rows[200*8];
    M5Dial.Display.startWrite();
    for(int base=0;base<config.screen_h;base+=8) {
        for(int y=0;y<8;y++) for(int x=0;x<config.screen_w;x++) {
            int sx=x*400/config.screen_w/logical_scale;
            int sy=(base+y)*240/config.screen_h/logical_scale;
            bool black=((bits[sy*50+sx/8]>>(7-sx%8))&1)^inverted;
            rows[y*200+x]=black ? M5DialBoard::lcd_ink : M5DialBoard::lcd_paper;
        }
        M5Dial.Display.pushImage(config.screen_x,config.screen_y+base,config.screen_w,8,rows);
    }
    M5Dial.Display.endWrite();
}
void board_message(const char *title,const char *text) {
    controls_visible=false;
    M5Dial.Display.fillScreen(M5DialBoard::ink);
    M5Dial.Display.setTextColor(M5DialBoard::yellow,M5DialBoard::ink);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.drawString(title,120,62);
    /* Center bounded lines in the circular safe area, including errors.
       The complete error remains available over serial. */
    for(int row=0;row<6 && *text;row++) {
        char line[96];int n=0;
        while(*text && *text!='\n' && n<95) {
            line[n]=*text;line[n+1]=0;
            if(M5Dial.Display.textWidth(line)>180){line[n]=0;break;}
            text++;n++;
        }
        line[n]=0;
        if(*text=='\n')text++;
        if(row==5 && *text) {
            while(n>0 && (n>90 || M5Dial.Display.textWidth(line)>155))line[--n]=0;
            strlcat(line,"...",sizeof(line));
        }
        M5Dial.Display.drawString(line,120,84+row*18);
    }
}
void board_tone(float hz,unsigned ms,float volume) {
    M5Dial.Speaker.setVolume((uint8_t)(fmaxf(0,fminf(1,volume))*128));
    M5Dial.Speaker.tone(hz,ms);
}
void board_audio_stop() {M5Dial.Speaker.stop();}
