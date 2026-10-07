#pragma once
#include <stdint.h>
#ifdef _WIN32
#define CRANK_EXPORT __declspec(dllexport)
#else
#define CRANK_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* One game session at a time. status: 0 stopped, 1 starting, 2 running,
   3 failed, 4 suspended (acknowledged by the game thread). */
CRANK_EXPORT int crank_start(const char *pdx_directory, const char *save_directory);
/* Explicit host-owned PDGames session. Regular games never receive this bridge. */
CRANK_EXPORT int crank_start_launcher(const char *pdx_directory, const char *save_directory);
/* FIFO request snapshot; consumes only when the caller's buffer fits. Every
   request includes a session token. Replies from previous sessions are rejected. */
CRANK_EXPORT uint32_t crank_launcher_request(char *destination, uint32_t capacity);
CRANK_EXPORT int crank_launcher_reply(uint32_t session, const char *json);
CRANK_EXPORT int crank_launcher_activate(void);
CRANK_EXPORT int crank_launcher_detail_action(uint32_t action);
CRANK_EXPORT void crank_stop(void);
CRANK_EXPORT int crank_status(void);
/* Asynchronous request: bit 0 = paused/menu, bit 1 = app background/locked.
   Lua lifecycle callbacks run on the game thread, once per transition.
   Suspension freezes game-relative time and PCM; stop also works suspended. */
CRANK_EXPORT void crank_set_suspended(uint32_t reasons);
CRANK_EXPORT void crank_set_buttons(uint32_t mask);
CRANK_EXPORT void crank_add_crank(float degrees, int docked);
/* Copies latest 400x240 BGRA8 frame; returns serial (0 before first frame). */
CRANK_EXPORT uint32_t crank_copy_frame(uint8_t *destination, uint32_t capacity);
/* Pulls signed 16-bit interleaved stereo at 44100 Hz; returns sample count (two per frame). */
CRANK_EXPORT uint32_t crank_copy_audio(int16_t *destination, uint32_t sample_capacity);
CRANK_EXPORT void crank_last_error(char *destination, uint32_t capacity);
/* Thread-safe JSON snapshot: mode, ARM/API calls, guest heap and resources.
   Required bytes include NUL. This reports execution, not blanket compatibility. */
CRANK_EXPORT uint32_t crank_copy_native_info(char *destination, uint32_t capacity);
/* SDK menu snapshot: required UTF-8 bytes including NUL; copies only if it fits.
   Commands are accepted only for an open menu, queued to the Lua game thread.
   Check/options values use boolean 0/1 or one-based option index. */
CRANK_EXPORT uint32_t crank_copy_menu(char *destination, uint32_t capacity);
CRANK_EXPORT uint32_t crank_copy_menu_image(uint8_t *destination, uint32_t capacity);
CRANK_EXPORT void crank_set_menu_open(int open);
CRANK_EXPORT int crank_menu_select(uint32_t item_id, int value);
#ifdef __ANDROID__
/* JavaVM, JNIEnv and the CrankHttp helper class, passed by the embedding application. */
CRANK_EXPORT int crank_android_init(void *java_vm, void *jni_env, void *http_class);
#endif
#ifdef __cplusplus
}
#endif
