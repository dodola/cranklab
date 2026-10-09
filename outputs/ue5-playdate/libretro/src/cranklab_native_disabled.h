#pragma once
#include <stddef.h>

/* Reject C and mixed Lua/C bundles before starting the game thread. */
int cranklab_lua_content_supported(const char *directory, char *error, size_t capacity);
