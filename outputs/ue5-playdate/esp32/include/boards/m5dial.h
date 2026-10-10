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
}
