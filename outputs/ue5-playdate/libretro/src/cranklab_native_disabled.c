/* Linux/libretro-only replacement for pd_native.cpp. Android keeps its ARM backend. */
#include "cranklab_native_disabled.h"
#include "cranklab_content.h"
#include "pd_native.h"
#include "pd_runtime.h"
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

int cranklab_lua_content_supported(const char *directory, char *error, size_t capacity) {
    char path[CRANKLAB_PATH_MAX];
    struct stat st;
    int length = snprintf(path, sizeof(path), "%s/pdex.bin", directory);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        snprintf(error, capacity, "Game path is too long");
        return 0;
    }
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode) && st.st_size == 0) return 1;
        snprintf(error, capacity, "Lua-only core: C and Lua+C games with pdex.bin are unsupported.");
        return 0;
    }
    if (errno == ENOENT) return 1;
    snprintf(error, capacity, "Cannot inspect pdex.bin in the game folder");
    return 0;
}

int pd_native_load(struct lua_State *L, const char *directory) {
    char error[256];
    (void)L;
    pd_host_native_info("{\"mode\":\"Lua\",\"armCalls\":0,\"apiCalls\":0,\"heapBytes\":0,\"resources\":0,\"usedApis\":[]}");
    if (cranklab_lua_content_supported(directory, error, sizeof(error))) return 0;
    pd_host_fail(error);
    return -1;
}
int pd_native_has_update(void) { return 0; }
int pd_native_update(void) { return 0; }
int pd_native_event(int event, uint32_t argument) { (void)event; (void)argument; return 1; }
int pd_native_button(int button, int down) { (void)button; (void)down; return 1; }
int pd_native_lifecycle(const char *callback) { (void)callback; return 1; }
void pd_native_publish(void) {}
void pd_native_present(void) {}
void pd_native_shutdown(void) {}
