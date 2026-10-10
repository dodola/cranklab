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
const BoardConfig &board_config() { return config; }
extern "C" uint64_t micro_board_millis() { return millis(); }

void board_begin() {
    static bool initialized=false;
    if(!initialized) {
        pinMode(M5DialBoard::power_hold_pin,OUTPUT); digitalWrite(M5DialBoard::power_hold_pin,HIGH);
        auto cfg=M5.config();cfg.internal_imu=false;cfg.internal_rtc=false;
        M5Dial.begin(cfg,true,false);initialized=true;
        Serial.printf("HARDWARE {\"m5dialDetected\":%s,\"touchEnabled\":%s,\"width\":%d,\"height\":%d}\n",
            M5.getBoard()==m5::board_t::board_M5Dial ? "true" : "false",
            M5Dial.Touch.isEnabled() ? "true" : "false",M5Dial.Display.width(),M5Dial.Display.height());
    }
    M5Dial.Display.setBrightness(128);
    M5Dial.Display.fillScreen(0xFFC600);
    M5Dial.Display.setTextColor(0x292D2C,0xFFC600);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::Font2);
    M5Dial.Display.drawString("PLAYDATE",120,30);
    M5Dial.Display.fillTriangle(27,205,37,199,37,211,0x292D2C);
    M5Dial.Display.fillTriangle(92,205,82,199,82,211,0x292D2C);
    M5Dial.Display.fillTriangle(59,186,53,196,65,196,0x292D2C);
    M5Dial.Display.fillTriangle(59,226,53,216,65,216,0x292D2C);
    M5Dial.Display.drawCircle(144,204,17,0x292D2C);
    M5Dial.Display.drawCircle(184,204,17,0x292D2C);
    M5Dial.Display.drawString("B",144,205);
    M5Dial.Display.drawString("A",184,205);
    encoder_position=M5Dial.Encoder.read();
}
BoardInput board_poll() {
    M5Dial.update(); BoardInput result={0,0,false};
    long current=M5Dial.Encoder.read();
    result.crank_delta=(current-encoder_position)*360.f/config.encoder_pulses_per_rotation;
    encoder_position=current;
    auto touch=M5Dial.Touch.getDetail();
    if (touch.isPressed()) {
        int x=touch.x,y=touch.y;
        if(y>=182) {
            if(x>=166) result.buttons|=1<<5;
            else if(x>=123) result.buttons|=1<<4;
            else if(x<40) result.buttons|=1<<0;
            else if(x>78) result.buttons|=1<<1;
            else if(y<208) result.buttons|=1<<2;
            else result.buttons|=1<<3;
        }
    }
    if(M5Dial.BtnA.isPressed() && !M5Dial.BtnA.pressedFor(600)) result.buttons|=1<<5;
    result.menu_pressed=M5Dial.BtnA.wasReleaseFor(600);
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
            rows[y*200+x]=black ? 0x2965 : 0xD6B9;
        }
        M5Dial.Display.pushImage(config.screen_x,config.screen_y+base,config.screen_w,8,rows);
    }
    M5Dial.Display.endWrite();
}
void board_message(const char *title,const char *text) {
    M5Dial.Display.fillScreen(0x292D2C);
    M5Dial.Display.setTextColor(0xFFC600,0x292D2C);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.drawString(title,120,62);
    M5Dial.Display.setTextDatum(top_left);M5Dial.Display.setCursor(22,90);
    M5Dial.Display.setTextWrap(true);M5Dial.Display.print(text);
}
void board_tone(float hz,unsigned ms,float volume) {
    M5Dial.Speaker.setVolume((uint8_t)(fmaxf(0,fminf(1,volume))*128));
    M5Dial.Speaker.tone(hz,ms);
}
void board_audio_stop() {M5Dial.Speaker.stop();}
