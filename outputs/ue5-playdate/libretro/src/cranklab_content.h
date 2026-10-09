#pragma once
#include <stddef.h>

#define CRANKLAB_PATH_MAX 2048

/* mkdir -p. Returns 1 when the directory exists afterwards. */
int cranklab_mkdirs(const char *path);

/* Turns frontend content (main.pdz / pdxinfo inside a PDX folder, a .pdxl pointer
   file, a .zip archive or a folder) into the PDX directory the runtime loads.
   Archives are extracted below cache_root/cranklab/. Returns 1 on success;
   otherwise writes a message to error. */
int cranklab_resolve_content(const char *content, const char *cache_root,
                             char *pdx_dir, size_t capacity, char *error, size_t error_capacity);
