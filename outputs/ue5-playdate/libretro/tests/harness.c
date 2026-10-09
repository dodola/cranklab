/* Minimal libretro frontend used to test the core without RetroArch.
   Usage: harness CORE CONTENT [--frames N] [--realtime] [--out DIR]
          [--opt key=value] [--hold FRAME:COUNT:BUTTON] [--stall FRAME:MS] [--ppm FILE]
   Buttons: left right up down b a l r l3 start select. Prints one summary line. */
#include "libretro.h"
#include <dlfcn.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct { int frame, count, id; } Hold;
static Hold holds[32]; static int hold_count;
/* Right stick scripts: spin FRAME:COUNT:MAGNITUDE:DEG_PER_FRAME[:START_DEG] turns clockwise on screen. */
static struct { int frame, count; double magnitude, step, start; } spins[8]; static int spin_count;
static int stick_x, stick_y;
static struct { int frame, ms; } stalls[8]; static int stall_count;
static char options[16][2][128]; static int option_count;
static int frame_no, pressed_now[16];
static uint64_t video_calls, audio_frames, changed_frames, black_pixels;
static int silent_batches; static double final_fps; static int audio_peak; static uint32_t last_hash; static unsigned last_w, last_h;
static uint32_t *last_frame; static int shutdown_requested;
static const char *out_dir = "/tmp/cranklab-harness", *ppm_path;
static char system_dir[512], save_dir[512];
static const int dump_ids[] = { 0 };

static void log_printf(enum retro_log_level level, const char *fmt, ...) {
    va_list args; va_start(args, fmt); fprintf(stderr, "[core %d] ", level); vfprintf(stderr, fmt, args); va_end(args);
}
static bool environment(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = system_dir; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = save_dir; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_printf; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        for (int i = 0; i < option_count; i++) if (!strcmp(options[i][0], v->key)) { v->value = options[i][1]; return true; }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: final_fps = ((struct retro_system_av_info *)data)->timing.fps; return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: shutdown_requested = 1; return true;
    }
    return false;
}
static void video(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;
    video_calls++; last_w = w; last_h = h;
    uint32_t hash = 2166136261u, black = 0;
    if (!last_frame) last_frame = malloc(w * h * 4);
    for (unsigned y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)((const char *)data + y * pitch);
        memcpy(last_frame + y * w, row, w * 4);
        for (unsigned x = 0; x < w; x++) { hash = (hash ^ row[x]) * 16777619u; if ((row[x] & 0xFF) < 0x80) black++; }
    }
    if (hash != last_hash) changed_frames++;
    last_hash = hash; black_pixels = black;
}
static size_t audio_batch(const int16_t *data, size_t frames) {
    int batch_peak = 0;
    for (size_t i = 0; i < frames * 2; i++) { int v = data[i] < 0 ? -data[i] : data[i]; if (v > batch_peak) batch_peak = v; }
    if (batch_peak > audio_peak) audio_peak = batch_peak;
    if (frame_no >= 6 && batch_peak == 0) silent_batches++;
    audio_frames += frames; return frames;
}
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; audio_frames++; }
static void poll(void) {
    memset(pressed_now, 0, sizeof(pressed_now));
    stick_x = stick_y = 0;
    for (int i = 0; i < spin_count; i++) {
        if (frame_no < spins[i].frame || frame_no >= spins[i].frame + spins[i].count) continue;
        double angle = (spins[i].start + (frame_no - spins[i].frame) * spins[i].step) * M_PI / 180.0;
        stick_x = (int)lround(spins[i].magnitude * cos(angle));
        stick_y = (int)lround(spins[i].magnitude * sin(angle));
    }
    for (int i = 0; i < hold_count; i++)
        if (frame_no >= holds[i].frame && frame_no < holds[i].frame + holds[i].count) pressed_now[holds[i].id] = 1;
}
static int16_t input(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port) return 0;
    if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
        return id == RETRO_DEVICE_ID_ANALOG_X ? stick_x : id == RETRO_DEVICE_ID_ANALOG_Y ? stick_y : 0;
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    return id < 16 ? pressed_now[id] : 0;
}
static int button_id(const char *n) {
    static const struct { const char *name; int id; } map[] = {
        {"left", RETRO_DEVICE_ID_JOYPAD_LEFT}, {"right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
        {"up", RETRO_DEVICE_ID_JOYPAD_UP}, {"down", RETRO_DEVICE_ID_JOYPAD_DOWN},
        {"b", RETRO_DEVICE_ID_JOYPAD_B}, {"a", RETRO_DEVICE_ID_JOYPAD_A},
        {"l", RETRO_DEVICE_ID_JOYPAD_L}, {"r", RETRO_DEVICE_ID_JOYPAD_R},
        {"l3", RETRO_DEVICE_ID_JOYPAD_L3}, {"start", RETRO_DEVICE_ID_JOYPAD_START},
        {"select", RETRO_DEVICE_ID_JOYPAD_SELECT} };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) if (!strcmp(map[i].name, n)) return map[i].id;
    return -1;
}
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, NULL); }

int main(int argc, char **argv) {
    int frames = 60, realtime = 0, cycles = 1, reset_at = -1;
    if (argc < 3) { fprintf(stderr, "usage: harness CORE CONTENT [options]\n"); return 2; }
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--realtime")) realtime = 1;
        else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) cycles = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--reset-at") && i + 1 < argc) reset_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_dir = argv[++i];
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm_path = argv[++i];
        else if (!strcmp(argv[i], "--opt") && i + 1 < argc && option_count < 16) {
            char *eq = strchr(argv[++i], '=');
            if (eq) { *eq = 0; snprintf(options[option_count][0], 128, "%s", argv[i]); snprintf(options[option_count][1], 128, "%s", eq + 1); option_count++; }
        } else if (!strcmp(argv[i], "--hold") && i + 1 < argc && hold_count < 32) {
            int f, c; char name[16];
            if (sscanf(argv[++i], "%d:%d:%15s", &f, &c, name) == 3 && button_id(name) >= 0) holds[hold_count++] = (Hold){ f, c, button_id(name) };
            else { fprintf(stderr, "bad --hold\n"); return 2; }
        } else if (!strcmp(argv[i], "--spin") && i + 1 < argc && spin_count < 8) {
            double start = 0;
            if (sscanf(argv[++i], "%d:%d:%lf:%lf:%lf", &spins[spin_count].frame, &spins[spin_count].count,
                       &spins[spin_count].magnitude, &spins[spin_count].step, &start) < 4) { fprintf(stderr, "bad --spin\n"); return 2; }
            spins[spin_count].start = start; spin_count++;
        } else if (!strcmp(argv[i], "--stall") && i + 1 < argc && stall_count < 8) {
            sscanf(argv[++i], "%d:%d", &stalls[stall_count].frame, &stalls[stall_count].ms); stall_count++;
        } else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    (void)dump_ids;
    snprintf(system_dir, sizeof(system_dir), "%s/system", out_dir);
    snprintf(save_dir, sizeof(save_dir), "%s/saves", out_dir);
    char cmd[1200]; snprintf(cmd, sizeof(cmd), "mkdir -p '%s' '%s'", system_dir, save_dir);
    if (system(cmd)) return 2;
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
#define SYM(name) __typeof__(&name) p_##name = (__typeof__(&name))dlsym(lib, #name); if (!p_##name) { fprintf(stderr, "missing %s\n", #name); return 2; }
    SYM(retro_set_environment)
    SYM(retro_set_video_refresh)
    SYM(retro_set_audio_sample)
    SYM(retro_set_audio_sample_batch)
    SYM(retro_set_input_poll)
    SYM(retro_set_input_state)
    SYM(retro_init)
    SYM(retro_deinit)
    SYM(retro_run)
    SYM(retro_api_version)
    SYM(retro_get_system_info)
    SYM(retro_get_system_av_info)
    SYM(retro_load_game)
    SYM(retro_unload_game)
    SYM(retro_reset)
    SYM(retro_serialize_size)
    if (p_retro_api_version() != RETRO_API_VERSION) { fprintf(stderr, "API mismatch\n"); return 2; }
    p_retro_set_environment(environment); p_retro_set_video_refresh(video);
    p_retro_set_audio_sample(audio_sample); p_retro_set_audio_sample_batch(audio_batch);
    p_retro_set_input_poll(poll); p_retro_set_input_state(input);
    p_retro_init();
    struct retro_system_info info; p_retro_get_system_info(&info);
    struct retro_game_info game = { argv[2], NULL, 0, NULL };
    struct retro_system_av_info av;
    for (int cycle = 0; cycle < cycles; cycle++) {
    if (!p_retro_load_game(&game)) { printf("RESULT load=failed cycle=%d\n", cycle); return 1; }
    p_retro_get_system_av_info(&av);
    final_fps = av.timing.fps;
    for (frame_no = 0; frame_no < frames && !shutdown_requested; frame_no++) {
        if (frame_no == reset_at) p_retro_reset();
        for (int i = 0; i < stall_count; i++) if (stalls[i].frame == frame_no) sleep_ms(stalls[i].ms);
        p_retro_run();
        if (realtime) sleep_ms((int)(1000.0 / av.timing.fps));
    }
    if (cycle + 1 < cycles) p_retro_unload_game();
    }
    if (ppm_path && last_frame) {
        FILE *f = fopen(ppm_path, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", last_w, last_h);
            for (unsigned i = 0; i < last_w * last_h; i++) { unsigned char px[3] = { last_frame[i] >> 16, last_frame[i] >> 8, last_frame[i] }; fwrite(px, 1, 3, f); }
            fclose(f);
        }
    }
    p_retro_unload_game(); p_retro_deinit();
    printf("RESULT load=ok name=%s %s fps=%.2f rate=%.0f geometry=%ux%u frames=%d video=%llu changed=%llu black=%llu audio_frames=%llu audio_peak=%d final_fps=%.2f silent=%d serialize=%zu shutdown=%d\n",
           info.library_name, info.library_version, av.timing.fps, av.timing.sample_rate, last_w, last_h, frame_no,
           (unsigned long long)video_calls, (unsigned long long)changed_frames, (unsigned long long)black_pixels,
           (unsigned long long)audio_frames, audio_peak, final_fps, silent_batches, p_retro_serialize_size(), shutdown_requested);
    return 0;
}
