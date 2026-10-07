#ifndef PD_PDX_H
#define PD_PDX_H

#include <stdint.h>
#include <stddef.h>

typedef struct PDZEntry {
    char *filename;
    uint8_t *data;
    size_t size;
    int entry_type;
    int is_compressed;
    int audio_sample_rate;
    int audio_format;
} PDZEntry;

typedef struct PDZFile {
    PDZEntry *entries;
    int entry_count;
    int encrypted;
} PDZFile;

typedef struct {
    char game_name[256];
    char author[256];
    char description[1024];
    char bundle_id[256];
    char version[64];
    char build_number[32];
    char image_path[256];
    char launch_sound_path[256];
    char content_warning[256];
    char content_warning2[256];
} PDXInfo;

int pdx_read_info(const char *pdx_dir, PDXInfo *info);
int pd_fix_path_case(char *path);
PDZFile *pdz_load(const char *path);
void pdz_free(PDZFile *pdz);
PDZEntry *pdz_find(PDZFile *pdz, const char *name);

#endif
