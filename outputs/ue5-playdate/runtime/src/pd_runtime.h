#ifndef PD_RUNTIME_H
#define PD_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <SDL2/SDL.h>

#if !SDL_VERSION_ATLEAST(2, 0, 18)
/* Older SDL2 (e.g. Ubuntu 20.04) lacks SDL_GetTicks64 */
#define SDL_GetTicks64() ((Uint64)SDL_GetTicks())
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#if defined(CRANK_ESP32) && !defined(MICRO_ALLOW_SYSTEM_ALLOC)
#include "core_memory.h"
#endif

struct PDZFile;
struct PDZEntry;

#define PD_SCREEN_WIDTH  400
#define PD_SCREEN_HEIGHT 240
#define PD_REFRESH_RATE 30.0f

typedef enum {
    kColorBlack = 0,
    kColorWhite = 1,
    kColorClear = 2,
    kColorXOR   = 3
} LCDSolidColor;

typedef enum {
    kDrawModeCopy           = 0,
    kDrawModeWhiteTransparent = 1,
    kDrawModeBlackTransparent = 2,
    kDrawModeFillWhite      = 3,
    kDrawModeFillBlack      = 4,
    kDrawModeXOR            = 5,
    kDrawModeNXOR           = 6,
    kDrawModeInverted       = 7
} LCDBitmapDrawMode;

typedef enum {
    kBitmapUnflipped  = 0,
    kBitmapFlippedX    = 1,
    kBitmapFlippedY    = 2,
    kBitmapFlippedXY   = 3
} LCDBitmapFlip;

typedef enum {
    kButtonLeft  = (1<<0),
    kButtonRight = (1<<1),
    kButtonUp    = (1<<2),
    kButtonDown  = (1<<3),
    kButtonB     = (1<<4),
    kButtonA     = (1<<5)
} PDButtons;

typedef struct {
    int width;
    int height;
    int rowbytes;
    uint8_t *mask;
    uint8_t *data;
    SDL_Texture *texture;
    int inverted;
} LCDBitmap;

typedef struct {
    int count;
    int width;
    int height;
    LCDBitmap **bitmaps;
} LCDBitmapTable;

#define PD_FONT_MAX_GLYPHS 512

/* Glyphs above PD_FONT_MAX_GLYPHS (button symbols like U+24B6 "A",
   d-pad/crank glyphs, etc.) are kept in a sparse overflow list. */
typedef struct {
    uint32_t cp;
    LCDBitmap *bmp;
    uint8_t adv;
} PDGlyphExt;

typedef struct {
    char *font_data;
    int font_data_size;
    char *glyph_table;
    int glyph_table_size;
    int glyph_width;
    int glyph_height;
    int page_width;
    int page_height;
    int tracking;
    int has_glyphs;
    LCDBitmap *glyph_bmp[PD_FONT_MAX_GLYPHS];
    uint8_t glyph_adv[PD_FONT_MAX_GLYPHS];
    PDGlyphExt *glyph_ext;
    int glyph_ext_count;
    int glyph_ext_cap;
} LCDFont;

typedef struct {
    lua_State *L;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *framebuffer;
    uint8_t *frame;
    LCDBitmap *display_bitmap;
    LCDBitmapDrawMode draw_mode;
    LCDSolidColor bg_color;
    LCDSolidColor fg_color;
    int clip_x, clip_y, clip_w, clip_h;
    int clip_enabled;
    int draw_offset_x, draw_offset_y;
    LCDFont *current_font;
    int text_tracking;
    int text_leading;
    float crank_angle;
    float crank_change;
    int crank_docked;
    PDButtons buttons_current;
    PDButtons buttons_pushed;
    PDButtons buttons_released;
    int inverted;
    int display_scale;
    float refresh_rate;
    uint64_t start_time;
    uint64_t wait_until;   /* playdate.wait(): skip update until this tick */
    int running;
    char *pdx_path;
    char *pdx_dir;
    char *save_dir;
    char game_name[256];
    char game_author[256];
    char game_bundle_id[256];
    char game_build[64];
    char game_version[64];
    struct PDZFile *pdz;
} PDRuntime;
extern PDRuntime g_pd;

void pd_graphics_register(lua_State *L);
void pd_display_register(lua_State *L);
void pd_input_register(lua_State *L);
void pd_system_register(lua_State *L);
void pd_file_register(lua_State *L);
void pd_sound_register(lua_State *L);
void pd_sound_shutdown(void);
void pd_sound_set_suspended(int suspended);
void pd_network_register(lua_State *L);
int pd_network_pump(lua_State *L);
void pd_network_shutdown(lua_State *L);
int pd_host_active(void);
int pd_host_lockstep(void);
int pd_host_step_wait(lua_State *L);
int pd_host_should_stop(void);
const char *pd_host_save_path(void);
void pd_host_fail(const char *error);
void pd_host_native_info(const char *json);
int pd_sound_native_loaded(lua_State *L,int index);
void pd_sound_native_dispose(lua_State *L,int index);
void pd_sound_native_volume(lua_State *L,int index,float left,float right);
void pd_sound_native_pause(lua_State *L,int index,int paused);
void pd_sprite_native_release(lua_State *L,int index);
void pd_sprite_native_collect(lua_State *L);
void pd_host_input(void);
void pd_host_publish(void);
void pd_host_audio(const int16_t *samples, int count);
int pd_host_lifecycle(lua_State *L);
int pd_host_finish_menu(lua_State *L);
int pd_host_launcher_install(lua_State *L);
void pd_host_menu(const char *json, const uint8_t *image);
void pd_sdkmenu_register(lua_State *L);
void pd_sdkmenu_publish(lua_State *L);
int pd_sdkmenu_select(lua_State *L, uint32_t id, int value);
int pd_sdkmenu_close(lua_State *L);
int pd_call_lifecycle(lua_State *L, const char *callback);
uint64_t pd_time_milliseconds(void);
void pd_math_register(lua_State *L);
void pd_geometry_register(lua_State *L);
void pd_sprite_register(lua_State *L);
void pd_sprite_shutdown(void);
void pd_graphics_shutdown(void);
void pd_json_register(lua_State *L);
void pd_json_encode_table(lua_State *L, int index);
void pd_string_register(lua_State *L);
void pd_table_register(lua_State *L);
int pd_dispatch_input_handler(lua_State *L, const char *handler);
void pd_gfx_reset_focus(void);
LCDBitmap *pd_gfx_get_stencil(void);
void pd_gfx_set_stencil(LCDBitmap *bm);
void pd_install_api_stubs(lua_State *L);
extern int g_sprites_drawn_this_frame;
extern int pd_frame_draw_ops;    /* pixels written by the game this frame */
extern uint32_t pd_frame_serial; /* increments once per main-loop frame */
extern LCDBitmap *pd_screen_bitmap; /* the real screen (not focus targets) */
extern int pd_damage_protect;    /* 1 while a sprite pass is painting */
void pd_damage_reset(void);
void pd_clear_display_bg_rect(int x0, int y0, int x1, int y1);
int pd_rom_menu(const char *base_dir, char *out_dir, size_t out_sz);
void pd_builtin_text(LCDBitmap *bm, int x, int y, int scale, int black, const char *str);
const uint8_t *pd_font5x7_glyph(int c);

int pd_import(lua_State *L);
int pd_flip_arg(lua_State *L, int idx);
int pd_drawmode_arg(lua_State *L, int idx);
LCDBitmap *pd_load_pdi(const char *path);
LCDFont *pd_load_pft(const char *path);
int pd_font_glyph_bitmap(LCDFont *font, uint32_t cp, const uint8_t **data, const uint8_t **mask,
                         int *width, int *height, int *rowbytes);
void pd_font_release(LCDFont *font);
LCDFont *pd_load_pft_detached(const char *path);
void pd_setup_imports(lua_State *L);

void pd_call_input_handlers(int button, int pressed);
void pd_present_frame(void);

#endif
