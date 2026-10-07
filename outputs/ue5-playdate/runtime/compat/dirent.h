#pragma once
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct dirent { char d_name[1024]; };
typedef struct { intptr_t handle; struct _finddata_t data; int first; struct dirent entry; } DIR;
static DIR *opendir(const char *path) {
    DIR *dir = (DIR *)calloc(1, sizeof(DIR));
    if (!dir) return NULL;
    char pattern[1200];
    snprintf(pattern, sizeof(pattern), "%s/*", path);
    dir->handle = _findfirst(pattern, &dir->data);
    if (dir->handle == -1) { free(dir); return NULL; }
    dir->first = 1;
    return dir;
}
static struct dirent *readdir(DIR *dir) {
    if (!dir) return NULL;
    if (!dir->first && _findnext(dir->handle, &dir->data) != 0) return NULL;
    dir->first = 0;
    snprintf(dir->entry.d_name, sizeof(dir->entry.d_name), "%s", dir->data.name);
    return &dir->entry;
}
static int closedir(DIR *dir) {
    if (!dir) return -1;
    int result = _findclose(dir->handle);
    free(dir);
    return result;
}
