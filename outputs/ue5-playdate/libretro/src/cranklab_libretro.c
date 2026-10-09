/* libretro frontend for the CrankLab Playdate compatibility runtime.
   It drives the same C ABI (crank_host.h) that the Unreal application uses. */
#include "libretro.h"
#include "crank_host.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>

#include "cranklab_content.h"
#include "cranklab_menu.h"

#define SCREEN_W 400
#define SCREEN_H 240
#define SAMPLE_RATE 44100
#define DEFAULT_FPS 30.0
#define AUDIO_RING_FRAMES 16384
#define SUSPEND_GAP_MS 150

enum { PD_LEFT = 1, PD_RIGHT = 2, PD_UP = 4, PD_DOWN = 8, PD_B = 16, PD_A = 32 };
enum { PALETTE_LCD, PALETTE_BW, PALETTE_INVERTED };

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static const char *system_dir, *save_dir;
static char pdx_dir[CRANKLAB_PATH_MAX];
static int session_active;
static double current_fps = DEFAULT_FPS;

/* options */
static int palette = PALETTE_LCD;
static float crank_speed = 6.0f;
static int analog_crank = 1;
static int swap_ab = 0;
static int menu_on_select = 0;

/* input state owned by retro_run */
static int crank_docked = 1;
static int l3_was_down;
static int stick_active;
static float stick_angle;

/* video */
static uint32_t video_buffer[SCREEN_W * SCREEN_H];
static uint8_t frame_buffer[SCREEN_W * SCREEN_H * 4];
static uint32_t shown_serial;
static int palette_dirty = 1;

/* audio */
static int16_t audio_ring[AUDIO_RING_FRAMES * 2];
static size_t ring_read, ring_fill; /* in stereo frames */

/* The game thread runs in real time. libretro stops calling retro_run while the
   frontend menu is open, so a watchdog suspends the game when calls stop. */
static pthread_t watchdog;
static pthread_mutex_t watchdog_lock = PTHREAD_MUTEX_INITIALIZER;
static int watchdog_started, watchdog_quit, game_suspended, menu_active;
static uint64_t last_run_ms;

static void say(enum retro_log_level level, const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (log_cb) log_cb(level, "[CrankLab] %s\n", text);
    else fprintf(stderr, "[CrankLab] %s\n", text);
}

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)(t.tv_nsec / 1000000);
}

/* ---- options ---------------------------------------------------------- */

static const struct retro_variable option_definitions[] = {
    { "cranklab_palette", "Display palette; lcd|black_white|inverted" },
    { "cranklab_crank_speed", "Button crank speed (degrees per frame); 6|3|9|12|18|24" },
    { "cranklab_analog_crank", "Right stick turns the crank; enabled|disabled" },
    { "cranklab_swap_ab", "Swap A and B buttons; disabled|enabled" },
    { "cranklab_menu_button", "Playdate menu button; start|select" },
    { NULL, NULL }
};

static const char *option_value(const char *key) {
    struct retro_variable v = { key, NULL };
    return environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &v) && v.value ? v.value : NULL;
}

static void read_options(void) {
    const char *v;
    if ((v = option_value("cranklab_palette"))) {
        int next = !strcmp(v, "black_white") ? PALETTE_BW : !strcmp(v, "inverted") ? PALETTE_INVERTED : PALETTE_LCD;
        if (next != palette) { palette = next; palette_dirty = 1; }
    }
    if ((v = option_value("cranklab_crank_speed"))) {
        int speed = atoi(v);
        crank_speed = speed > 0 && speed <= 90 ? (float)speed : 6.0f;
    }
    if ((v = option_value("cranklab_analog_crank"))) analog_crank = strcmp(v, "disabled") != 0;
    if ((v = option_value("cranklab_swap_ab"))) swap_ab = !strcmp(v, "enabled");
    if ((v = option_value("cranklab_menu_button"))) menu_on_select = !strcmp(v, "select");
}

/* ---- watchdog --------------------------------------------------------- */

static void *watchdog_main(void *unused) {
    (void)unused;
    for (;;) {
        usleep(25000);
        pthread_mutex_lock(&watchdog_lock);
        if (watchdog_quit) { pthread_mutex_unlock(&watchdog_lock); return NULL; }
        if (session_active && !game_suspended && now_ms() - last_run_ms > SUSPEND_GAP_MS) {
            crank_set_suspended(1);
            game_suspended = 1;
        }
        pthread_mutex_unlock(&watchdog_lock);
    }
}

static void watchdog_start(void) {
    watchdog_quit = 0;
    game_suspended = 0;
    last_run_ms = now_ms();
    watchdog_started = pthread_create(&watchdog, NULL, watchdog_main, NULL) == 0;
}

static void watchdog_stop(void) {
    if (!watchdog_started) return;
    pthread_mutex_lock(&watchdog_lock);
    watchdog_quit = 1;
    pthread_mutex_unlock(&watchdog_lock);
    pthread_join(watchdog, NULL);
    watchdog_started = 0;
}

static void mark_run(void) {
    pthread_mutex_lock(&watchdog_lock);
    last_run_ms = now_ms();
    if (game_suspended && !menu_active) { crank_set_suspended(0); game_suspended = 0; }
    pthread_mutex_unlock(&watchdog_lock);
}


/* ---- system menu ------------------------------------------------------ */

static int menu_selected, menu_nav_prev;
static CrankMenuModel menu_model;
static uint8_t menu_image[SCREEN_W * SCREEN_H * 4];
static int menu_image_valid;
enum { NAV_UP = 1, NAV_DOWN = 2, NAV_LEFT = 4, NAV_RIGHT = 8, NAV_OK = 16, NAV_BACK = 32, NAV_MENU = 64 };

static void menu_refresh(void) {
    uint32_t need = crank_copy_menu(NULL, 0);
    if (need > 1 && need < (1u << 20)) {
        char *json = malloc(need);
        CrankMenuModel parsed;
        if (json && crank_copy_menu(json, need) <= need && cranklab_menu_parse(json, &parsed)) menu_model = parsed;
        free(json);
    }
    menu_image_valid = menu_model.has_image && crank_copy_menu_image(menu_image, sizeof(menu_image)) != 0;
    int rows = cranklab_menu_rows(&menu_model);
    if (menu_selected >= rows) menu_selected = rows - 1;
}

static void menu_begin(void) {
    if (menu_active || !session_active) return;
    pthread_mutex_lock(&watchdog_lock);
    menu_active = 1;
    if (!game_suspended) { crank_set_suspended(1); game_suspended = 1; }
    pthread_mutex_unlock(&watchdog_lock);
    crank_set_menu_open(1);
    for (int waited = 0; waited < 500 && crank_status() != 4 && crank_status() != 3; waited += 5) usleep(5000);
    ring_read = ring_fill = 0;
    menu_selected = 0;
    menu_refresh();
}

/* Closing resumes the game; queued item changes run before gameWillResume. */
static void menu_end(void) {
    if (!menu_active) return;
    crank_set_menu_open(0);
    pthread_mutex_lock(&watchdog_lock);
    menu_active = 0;
    last_run_ms = now_ms();
    crank_set_suspended(0);
    game_suspended = 0;
    pthread_mutex_unlock(&watchdog_lock);
    palette_dirty = 1;
}

static void menu_change(CrankMenuItem *item, int delta) {
    int value;
    if (item->kind == 1) value = !item->value;
    else if (item->kind == 2 && item->option_count > 0) value = (item->value - 1 + delta + item->option_count) % item->option_count + 1;
    else return;
    if (crank_menu_select((uint32_t)item->id, value)) item->value = value;
}

static void menu_navigate(int nav) {
    int rows = cranklab_menu_rows(&menu_model);
    CrankMenuItem *item = menu_selected < menu_model.count ? &menu_model.items[menu_selected] : NULL;
    if (nav & NAV_UP) menu_selected = (menu_selected + rows - 1) % rows;
    if (nav & NAV_DOWN) menu_selected = (menu_selected + 1) % rows;
    item = menu_selected < menu_model.count ? &menu_model.items[menu_selected] : NULL;
    if (item && (nav & NAV_LEFT)) menu_change(item, -1);
    if (item && (nav & NAV_RIGHT)) menu_change(item, 1);
    if (nav & NAV_OK) {
        if (!item) menu_end();
        else if (item->kind == 0) { crank_menu_select((uint32_t)item->id, 0); menu_end(); }
        else menu_change(item, 1);
    }
    if (nav & (NAV_BACK | NAV_MENU)) menu_end();
}

/* ---- session ---------------------------------------------------------- */

static int start_session(void) {
    char error[512];
    char saves[CRANKLAB_PATH_MAX];
    snprintf(saves, sizeof(saves), "%s/cranklab", save_dir && save_dir[0] ? save_dir : "/tmp");
    cranklab_mkdirs(saves);
    memset(audio_ring, 0, sizeof(audio_ring));
    ring_read = ring_fill = 0;
    shown_serial = 0;
    palette_dirty = 1;
    crank_docked = 1;
    current_fps = DEFAULT_FPS;
    stick_active = 0;
    crank_set_lockstep(1);
    if (!crank_start(pdx_dir, saves)) {
        say(RETRO_LOG_ERROR, "crank_start refused %s", pdx_dir);
        return 0;
    }
    session_active = 1;
    for (int waited = 0; waited < 30000; waited += 10) {
        int status = crank_status();
        if (status == 2 || status == 4) return 1;
        if (status == 3) break;
        usleep(10000);
    }
    crank_last_error(error, sizeof(error));
    say(RETRO_LOG_ERROR, "Game failed to start: %s", error[0] ? error : "timed out");
    crank_stop();
    session_active = 0;
    return 0;
}

static void end_session(void) {
    if (!session_active) return;
    /* Runs gameWillTerminate so games can write their save files. */
    crank_stop();
    session_active = 0;
}

/* ---- libretro API ----------------------------------------------------- */

void retro_set_environment(retro_environment_t cb) {
    environ_cb = cb;
    bool no_game = false;
    struct retro_log_callback logging;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *)option_definitions);
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging)) log_cb = logging.log;
}
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_init(void) {}
void retro_deinit(void) {}

void retro_get_system_info(struct retro_system_info *info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "CrankLab";
    info->library_version = "0.1.0";
    info->valid_extensions = "pdx|pdz|pdxl|zip";
    info->need_fullpath = true;
    info->block_extract = true;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = info->geometry.max_width = SCREEN_W;
    info->geometry.base_height = info->geometry.max_height = SCREEN_H;
    info->geometry.aspect_ratio = (float)SCREEN_W / (float)SCREEN_H;
    info->timing.fps = current_fps;
    info->timing.sample_rate = SAMPLE_RATE;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }

bool retro_load_game(const struct retro_game_info *game) {
    enum retro_pixel_format format = RETRO_PIXEL_FORMAT_XRGB8888;
    char error[512];
    static const struct retro_input_descriptor descriptors[] = {
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "D-Pad Left" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "D-Pad Up" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "D-Pad Down" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Crank counter-clockwise" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Crank clockwise" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Dock / undock crank" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Playdate menu" },
        { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Playdate menu (if selected in options)" },
        { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Crank (stick angle)" },
        { 0 }
    };
    if (!game || !game->path) { say(RETRO_LOG_ERROR, "No content was provided"); return false; }
    if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &format)) {
        say(RETRO_LOG_ERROR, "XRGB8888 is required");
        return false;
    }
    environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)descriptors);
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir)) system_dir = NULL;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &save_dir) || !save_dir) save_dir = system_dir;
    read_options();
    if (system_dir) {
        char font[CRANKLAB_PATH_MAX];
        snprintf(font, sizeof(font), "%s/cranklab/fonts/cranklab-zh.pft", system_dir);
        cranklab_menu_load_font(font);
    }

    if (!cranklab_resolve_content(game->path, system_dir ? system_dir : (save_dir ? save_dir : "/tmp"),
                                  pdx_dir, sizeof(pdx_dir), error, sizeof(error))) {
        say(RETRO_LOG_ERROR, "%s", error);
        return false;
    }
    say(RETRO_LOG_INFO, "Loading %s", pdx_dir);
    if (!start_session()) return false;
    watchdog_start();
    return true;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) {
    (void)type; (void)info; (void)num;
    return false;
}

void retro_unload_game(void) {
    watchdog_stop();
    menu_active = 0;
    end_session();
    cranklab_menu_free_font();
}

void retro_reset(void) {
    menu_active = 0;
    end_session();
    if (!start_session()) environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

/* Games keep their own save files; the runtime has no serializable machine state. */
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

/* ---- per-frame work --------------------------------------------------- */

static int pressed(unsigned id) { return input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id) != 0; }

static float wrap_degrees(float value) {
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

static void poll_input(void) {
    unsigned mask = 0;
    float delta = 0.0f;
    input_poll_cb();
    if (pressed(RETRO_DEVICE_ID_JOYPAD_LEFT)) mask |= PD_LEFT;
    if (pressed(RETRO_DEVICE_ID_JOYPAD_RIGHT)) mask |= PD_RIGHT;
    if (pressed(RETRO_DEVICE_ID_JOYPAD_UP)) mask |= PD_UP;
    if (pressed(RETRO_DEVICE_ID_JOYPAD_DOWN)) mask |= PD_DOWN;
    if (pressed(RETRO_DEVICE_ID_JOYPAD_B)) mask |= swap_ab ? PD_A : PD_B;
    if (pressed(RETRO_DEVICE_ID_JOYPAD_A)) mask |= swap_ab ? PD_B : PD_A;

    if (pressed(RETRO_DEVICE_ID_JOYPAD_L)) delta -= crank_speed;
    if (pressed(RETRO_DEVICE_ID_JOYPAD_R)) delta += crank_speed;
    if (analog_crank) {
        int x = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        int y = input_state_cb(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
        if ((double)x * x + (double)y * y > 16000.0 * 16000.0) {
            /* Screen Y grows downward, so atan2 increases clockwise like the crank. */
            float angle = atan2f((float)y, (float)x) * (180.0f / 3.14159265f);
            if (stick_active) delta += wrap_degrees(angle - stick_angle);
            stick_angle = angle;
            stick_active = 1;
        } else {
            stick_active = 0;
        }
    }
    {
        int nav = 0;
        if (pressed(RETRO_DEVICE_ID_JOYPAD_UP)) nav |= NAV_UP;
        if (pressed(RETRO_DEVICE_ID_JOYPAD_DOWN)) nav |= NAV_DOWN;
        if (pressed(RETRO_DEVICE_ID_JOYPAD_LEFT)) nav |= NAV_LEFT;
        if (pressed(RETRO_DEVICE_ID_JOYPAD_RIGHT)) nav |= NAV_RIGHT;
        if (pressed(RETRO_DEVICE_ID_JOYPAD_A)) nav |= NAV_OK;
        if (pressed(RETRO_DEVICE_ID_JOYPAD_B)) nav |= NAV_BACK;
        if (pressed(menu_on_select ? RETRO_DEVICE_ID_JOYPAD_SELECT : RETRO_DEVICE_ID_JOYPAD_START)) nav |= NAV_MENU;
        int edges = nav & ~menu_nav_prev;
        menu_nav_prev = nav;
        if (menu_active) {
            /* The game sees nothing while the menu is open. */
            crank_set_buttons(0);
            if (edges) menu_navigate(edges);
            return;
        }
        if (edges & NAV_MENU) { crank_set_buttons(0); menu_begin(); return; }
    }
    int l3 = pressed(RETRO_DEVICE_ID_JOYPAD_L3);
    if (l3 && !l3_was_down) crank_docked = !crank_docked;
    l3_was_down = l3;
    /* A docked crank cannot turn, so touching the crank input undocks it. */
    if (delta != 0.0f) crank_docked = 0;

    crank_set_buttons(mask);
    crank_add_crank(crank_docked ? 0.0f : delta, crank_docked);
}

static const uint32_t palette_black[] = { 0x00312F28u, 0x00000000u, 0x00FFFFFFu };
static const uint32_t palette_white[] = { 0x00B1AFA8u, 0x00FFFFFFu, 0x00000000u };

static void convert_frame(uint32_t serial) {
    const uint32_t black = palette_black[palette], white = palette_white[palette];
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        video_buffer[i] = frame_buffer[i * 4] > 127 ? white : black;
    shown_serial = serial;
    palette_dirty = 0;
}

static void push_video(void) {
    uint32_t serial = crank_copy_frame(frame_buffer, sizeof(frame_buffer));
    if (menu_active) {
        menu_refresh();
        cranklab_menu_draw(video_buffer, &menu_model, menu_selected,
                           menu_image_valid ? menu_image : frame_buffer, menu_image_valid ? menu_model.image_offset : 0,
                           palette_black[palette], palette_white[palette]);
        shown_serial = 0;
        palette_dirty = 0;
        video_cb(video_buffer, SCREEN_W, SCREEN_H, SCREEN_W * sizeof(uint32_t));
        return;
    }
    if (serial && (serial != shown_serial || palette_dirty)) convert_frame(serial);
    else if (!serial && palette_dirty) {
        for (int i = 0; i < SCREEN_W * SCREEN_H; i++) video_buffer[i] = palette_white[palette];
        palette_dirty = 0;
    }
    video_cb(video_buffer, SCREEN_W, SCREEN_H, SCREEN_W * sizeof(uint32_t));
}

static void ring_push(const int16_t *samples, size_t frames) {
    for (size_t i = 0; i < frames; i++) {
        if (ring_fill == AUDIO_RING_FRAMES) { ring_read = (ring_read + 1) % AUDIO_RING_FRAMES; ring_fill--; }
        size_t at = (ring_read + ring_fill) % AUDIO_RING_FRAMES;
        audio_ring[at * 2] = samples[i * 2];
        audio_ring[at * 2 + 1] = samples[i * 2 + 1];
        ring_fill++;
    }
}

static void push_audio(void) {
    int16_t chunk[4096];
    uint32_t count;
    const size_t per_frame = (size_t)(SAMPLE_RATE / current_fps + 0.5);
    while ((count = crank_copy_audio(chunk, sizeof(chunk) / sizeof(chunk[0]))) > 0) ring_push(chunk, count / 2);
    /* Keep latency bounded if the game thread ran ahead of the frontend. */
    if (ring_fill > per_frame * 4) {
        size_t drop = ring_fill - per_frame * 2;
        ring_read = (ring_read + drop) % AUDIO_RING_FRAMES;
        ring_fill -= drop;
    }
    int16_t out[2 * 2048];
    size_t remaining = per_frame;
    while (remaining) {
        size_t n = remaining < 2048 ? remaining : 2048, have = ring_fill < n ? ring_fill : n;
        for (size_t i = 0; i < have; i++) {
            size_t at = (ring_read + i) % AUDIO_RING_FRAMES;
            out[i * 2] = audio_ring[at * 2];
            out[i * 2 + 1] = audio_ring[at * 2 + 1];
        }
        ring_read = (ring_read + have) % AUDIO_RING_FRAMES;
        ring_fill -= have;
        if (have < n) memset(out + have * 2, 0, (n - have) * 2 * sizeof(int16_t));
        audio_batch_cb(out, n);
        remaining -= n;
    }
}

/* Games may change the display refresh rate (0 means manual updates; keep the last rate). */
static void sync_refresh_rate(void) {
    float rate = crank_refresh_rate();
    if (rate < 1.0f || rate > 60.0f || fabs((double)rate - current_fps) < 0.25) return;
    struct retro_system_av_info info;
    current_fps = rate;
    retro_get_system_av_info(&info);
    environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
}

void retro_run(void) {
    bool updated = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated) read_options();
    mark_run();
    poll_input();
    /* One game frame per call, so fast-forward and slow-motion scale the game with them. */
    if (session_active && !menu_active) crank_step();
    sync_refresh_rate();
    if (session_active && crank_status() == 3) {
        char error[512];
        crank_last_error(error, sizeof(error));
        say(RETRO_LOG_ERROR, "Runtime stopped: %s", error);
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
    }
    push_video();
    push_audio();
}
