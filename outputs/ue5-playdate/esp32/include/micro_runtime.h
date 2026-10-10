#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int micro_start(const char *directory);
int micro_step(uint32_t buttons, float crank_delta, int docked);
void micro_stop(void);
const char *micro_error(void);
unsigned micro_lua_bytes(void);
const uint8_t *micro_frame(void);
float micro_refresh_rate(void);
#ifdef __cplusplus
}
#endif
