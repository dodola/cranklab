#include "pd_pdx.h"
#include "pd_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <dirent.h>
#include <strings.h>
#include <unistd.h>

/* Fix the case of a path in place by matching each component
 * case-insensitively against the filesystem (Playdate's FS is
 * case-insensitive; Linux is not). Returns 1 if the fixed path exists. */
int pd_fix_path_case(char *path) {
    if (access(path, F_OK) == 0) return 1;
    char fixed[1200];
    size_t fl = 0;
    fixed[0] = 0;
    char work[1200];
    snprintf(work, sizeof(work), "%s", path);
    char *save = NULL;
    int absolute = work[0] == '/';
    if (absolute) fixed[fl++] = '/';
    fixed[fl] = 0;
    char *tok = strtok_r(work + (absolute ? 1 : 0), "/", &save);
    while (tok) {
        char candidate[1200];
        snprintf(candidate, sizeof(candidate), "%s%s%s",
                 fixed, fl > (size_t)absolute ? "/" : "", tok);
        if (access(candidate, F_OK) != 0) {
            /* scan parent for a case-insensitive match */
            const char *parent = fl > 0 ? fixed : ".";
            DIR *d = opendir(parent[0] ? parent : ".");
            int found = 0;
            if (d) {
                struct dirent *e;
                while ((e = readdir(d)) != NULL) {
                    if (strcasecmp(e->d_name, tok) == 0) {
                        snprintf(candidate, sizeof(candidate), "%s%s%s",
                                 fixed, fl > (size_t)absolute ? "/" : "", e->d_name);
                        found = 1;
                        break;
                    }
                }
                closedir(d);
            }
            if (!found) return 0;
        }
        snprintf(fixed, sizeof(fixed), "%s", candidate);
        fl = strlen(fixed);
        tok = strtok_r(NULL, "/", &save);
    }
    /* path buffer in callers is >= 1024; only copy if it fits */
    strcpy(path, fixed);
    return 1;
}

static char *file_read_all(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    if (out_size) *out_size = sz;
    return buf;
}

static void trim(char *s) {
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    p = s + strlen(s) - 1;
    while (p >= s && (*p == '\r' || *p == '\n')) *p-- = 0;
}

int pdx_read_info(const char *pdx_dir, PDXInfo *info) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/pdxinfo", pdx_dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(info, 0, sizeof(*info));
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        trim(key);
        trim(val);
        if (!strcmp(key, "name")) strncpy(info->game_name, val, sizeof(info->game_name) - 1);
        else if (!strcmp(key, "author")) strncpy(info->author, val, sizeof(info->author) - 1);
        else if (!strcmp(key, "description")) strncpy(info->description, val, sizeof(info->description) - 1);
        else if (!strcmp(key, "bundleID")) strncpy(info->bundle_id, val, sizeof(info->bundle_id) - 1);
        else if (!strcmp(key, "version")) strncpy(info->version, val, sizeof(info->version) - 1);
        else if (!strcmp(key, "buildNumber")) strncpy(info->build_number, val, sizeof(info->build_number) - 1);
        else if (!strcmp(key, "imagePath")) strncpy(info->image_path, val, sizeof(info->image_path) - 1);
        else if (!strcmp(key, "launchSoundPath")) strncpy(info->launch_sound_path, val, sizeof(info->launch_sound_path) - 1);
        else if (!strcmp(key, "contentWarning")) strncpy(info->content_warning, val, sizeof(info->content_warning) - 1);
        else if (!strcmp(key, "contentWarning2")) strncpy(info->content_warning2, val, sizeof(info->content_warning2) - 1);
    }
    fclose(f);
    return 0;
}

PDZFile *pdz_load(const char *path) {
    size_t file_size;
    char *raw = file_read_all(path, &file_size);
    if (!raw) return NULL;
    if (file_size < 16 || memcmp(raw, "Playdate PDZ", 12) != 0) {
        free(raw);
        return NULL;
    }
    uint32_t flags = *(uint32_t *)(raw + 12);
    (void)flags;
    PDZFile *pdz = calloc(1, sizeof(PDZFile));
    if (!pdz) { free(raw); return NULL; }
    size_t offset = 16;
    int capacity = 16;
    pdz->entries = calloc(capacity, sizeof(PDZEntry));
    while (offset < file_size) {
        if (offset + 4 > file_size) break;
        uint8_t entry_flags = (uint8_t)raw[offset++];
        uint8_t entry_type = entry_flags & 0x7F;
        int is_compressed = (entry_flags & 0x80) ? 1 : 0;
        uint8_t b0 = (uint8_t)raw[offset++];
        uint8_t b1 = (uint8_t)raw[offset++];
        uint8_t b2 = (uint8_t)raw[offset++];
        size_t data_len = (size_t)b0 | ((size_t)b1 << 8) | ((size_t)b2 << 16);
        char namebuf[256];
        int ni = 0;
        while (offset < file_size && raw[offset] != 0) {
            if (ni < 255) namebuf[ni++] = raw[offset];
            offset++;
        }
        if (offset < file_size) offset++;
        namebuf[ni] = 0;
        while (offset % 4 != 0 && offset < file_size) offset++;
        int audio_sample_rate = 0;
        int audio_format = 0;
        if (entry_type == 5 && offset + 4 <= file_size) {
            uint8_t sr0 = (uint8_t)raw[offset++];
            uint8_t sr1 = (uint8_t)raw[offset++];
            uint8_t sr2 = (uint8_t)raw[offset++];
            audio_sample_rate = sr0 | (sr1 << 8) | (sr2 << 16);
            audio_format = (uint8_t)raw[offset++];
            /* the entry length includes this 4-byte audio header */
            if (data_len >= 4) data_len -= 4;
        }
        size_t actual_data_len = data_len;
        uint8_t *data = NULL;
        if (is_compressed) {
            if (offset + 4 > file_size) break;
            uint32_t decompressed_size = *(uint32_t *)(raw + offset);
            offset += 4;
            size_t compressed_remaining = data_len - 4;
            if (offset + compressed_remaining > file_size) compressed_remaining = file_size - offset;
            data = malloc(decompressed_size);
            if (!data) break;
            uLongf dst_len = decompressed_size;
            if (uncompress(data, &dst_len, (const Bytef *)(raw + offset), compressed_remaining) != Z_OK) {
                free(data);
                data = NULL;
            }
            actual_data_len = dst_len;
            offset += compressed_remaining;
        } else {
            if (offset + data_len > file_size) data_len = file_size - offset;
            data = malloc(data_len);
            if (data) {
                memcpy(data, raw + offset, data_len);
                actual_data_len = data_len;
            }
            offset += data_len;
        }
        if (pdz->entry_count >= capacity) {
            capacity *= 2;
            pdz->entries = realloc(pdz->entries, capacity * sizeof(PDZEntry));
        }
        PDZEntry *e = &pdz->entries[pdz->entry_count++];
        e->filename = strdup(namebuf);
        e->data = data;
        e->size = actual_data_len;
        e->entry_type = entry_type;
        e->is_compressed = is_compressed;
        e->audio_sample_rate = audio_sample_rate;
        e->audio_format = audio_format;
    }
    free(raw);
    return pdz;
}

void pdz_free(PDZFile *pdz) {
    if (!pdz) return;
    for (int i = 0; i < pdz->entry_count; i++) {
        free(pdz->entries[i].filename);
        free(pdz->entries[i].data);
    }
    free(pdz->entries);
    free(pdz);
}

PDZEntry *pdz_find(PDZFile *pdz, const char *name) {
    for (int i = 0; i < pdz->entry_count; i++) {
        if (strcmp(pdz->entries[i].filename, name) == 0)
            return &pdz->entries[i];
    }
    return NULL;
}
