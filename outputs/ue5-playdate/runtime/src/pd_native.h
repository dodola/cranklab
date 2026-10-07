#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct lua_State;
/* All execution, resources and callbacks are owned by the existing game thread. */
int pd_native_load(struct lua_State *L, const char *directory);
int pd_native_has_update(void);
int pd_native_update(void);
int pd_native_event(int event, uint32_t argument);
int pd_native_button(int button, int down);
void pd_native_publish(void);
void pd_native_present(void);
int pd_native_lifecycle(const char *callback);
void pd_native_shutdown(void);
#ifdef __cplusplus
}
#endif
