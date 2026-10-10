#pragma once
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
void *micro_core_malloc(size_t size);
void *micro_core_calloc(size_t count, size_t size);
void *micro_core_realloc(void *ptr, size_t size);
char *micro_core_strdup(const char *value);
int micro_core_access(const char *path, int mode);
void micro_core_free(void *ptr);
void *micro_core_persistent_calloc(size_t count,size_t size);
FILE *micro_core_fopen(const char *path,const char *mode);
int micro_core_fclose(FILE *file);
/* Shared desktop code assumes asset allocations succeed. Turn MCU exhaustion
   into a protected Lua error instead of dereferencing a null pointer. */
#define malloc micro_core_malloc
#define calloc micro_core_calloc
#define realloc micro_core_realloc
#define strdup micro_core_strdup
#define free micro_core_free
#define fopen micro_core_fopen
#define fclose micro_core_fclose
/* LittleFS VFS provides stat but newlib access() returns ENOSYS. */
#define access micro_core_access
