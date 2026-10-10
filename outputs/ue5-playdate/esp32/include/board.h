#pragma once
#include <stdint.h>
struct BoardConfig {
    const char *id;
    int width, height;
    int screen_x, screen_y, screen_w, screen_h;
    int encoder_pulses_per_rotation;
    bool round;
};
struct BoardInput {
    uint32_t buttons;
    float crank_delta;
    bool menu_pressed;
};
const BoardConfig &board_config();
void board_begin();
BoardInput board_poll();
void board_present(const uint8_t *bits, bool inverted, int scale);
void board_message(const char *title, const char *text);
void board_tone(float hz, unsigned ms, float volume);
void board_audio_stop();
