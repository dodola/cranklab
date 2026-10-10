#include "pd_native.h"
#include "pd_runtime.h"
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

int pd_native_load(struct lua_State *L, const char *directory) {
    (void)L;
    char path[2048];
    struct stat st;
    int n = snprintf(path, sizeof(path), "%s/pdex.bin", directory);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        pd_host_fail("Game path is too long");
        return -1;
    }
    if (stat(path, &st) == 0) {
        if (!S_ISREG(st.st_mode) || st.st_size > 0) {
            pd_host_fail("This web version supports Lua games only; C and Lua+C games are unsupported.");
            return -1;
        }
    } else if (errno != ENOENT) {
        pd_host_fail("Cannot inspect pdex.bin");
        return -1;
    }
    pd_host_native_info("{\"mode\":\"Lua\",\"armCalls\":0,\"apiCalls\":0}");
    return 0;
}
int pd_native_has_update(void) { return 0; }
int pd_native_update(void) { return 0; }
int pd_native_event(int e, uint32_t a) { (void)e; (void)a; return 1; }
int pd_native_button(int b, int d) { (void)b; (void)d; return 1; }
int pd_native_lifecycle(const char *s) { (void)s; return 1; }
void pd_native_publish(void) {}
void pd_native_present(void) {}
void pd_native_shutdown(void) {}
