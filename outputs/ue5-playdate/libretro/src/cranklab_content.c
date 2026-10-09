#include "cranklab_content.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <zlib.h>

#define MAX_ARCHIVE_BYTES (1024u * 1024u * 1024u)
#define MAX_ENTRY_BYTES (512u * 1024u * 1024u)
#define MAX_ENTRIES 20000

static void fail(char *error, size_t capacity, const char *format, const char *detail) {
    if (error && capacity) snprintf(error, capacity, format, detail);
}

int cranklab_mkdirs(const char *path) {
    char buffer[CRANKLAB_PATH_MAX];
    if (!path || !path[0] || strlen(path) >= sizeof(buffer)) return 0;
    snprintf(buffer, sizeof(buffer), "%s", path);
    for (char *p = buffer + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(buffer, 0755);
        *p = '/';
    }
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST) return 0;
    struct stat st;
    return stat(buffer, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int exists_file(const char *directory, const char *name) {
    char path[CRANKLAB_PATH_MAX];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int looks_like_pdx(const char *directory) {
    return exists_file(directory, "pdxinfo") || exists_file(directory, "main.pdz") ||
           exists_file(directory, "pdex.bin") || exists_file(directory, "main.lua");
}

static int has_extension(const char *path, const char *extension) {
    size_t n = strlen(path), e = strlen(extension);
    return n > e && strcasecmp(path + n - e, extension) == 0;
}

static void parent_of(const char *path, char *out, size_t capacity) {
    snprintf(out, capacity, "%s", path);
    char *slash = strrchr(out, '/');
    if (slash && slash != out) *slash = 0;
    else if (slash) out[1] = 0;
    else snprintf(out, capacity, ".");
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ---- ZIP -------------------------------------------------------------- */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Accepts relative names only; rejects traversal, absolute paths and metadata. */
static int safe_entry_name(char *name) {
    for (char *p = name; *p; p++) if (*p == '\\') *p = '/';
    if (!name[0] || name[0] == '/' || name[1] == ':' || strstr(name, "__MACOSX/") == name) return 0;
    for (const char *p = name; *p; ) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == 2 && p[0] == '.' && p[1] == '.') return 0;
        p = end ? end + 1 : p + n;
    }
    return 1;
}

static int write_entry(const uint8_t *archive, size_t archive_size, size_t local_offset,
                       uint16_t method, uint32_t compressed, uint32_t size, uint32_t crc,
                       const char *destination) {
    if (local_offset + 30 > archive_size || le32(archive + local_offset) != 0x04034b50u) return 0;
    size_t data = local_offset + 30 + le16(archive + local_offset + 26) + le16(archive + local_offset + 28);
    if (data > archive_size || compressed > archive_size - data || size > MAX_ENTRY_BYTES) return 0;
    uint8_t *output = malloc(size ? size : 1);
    if (!output) return 0;
    int ok = 0;
    if (method == 0) {
        if (compressed == size) { memcpy(output, archive + data, size); ok = 1; }
    } else if (method == 8) {
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (inflateInit2(&stream, -15) == Z_OK) {
            stream.next_in = (Bytef *)(archive + data);
            stream.avail_in = compressed;
            stream.next_out = output;
            stream.avail_out = size;
            int result = inflate(&stream, Z_FINISH);
            ok = result == Z_STREAM_END && stream.total_out == size;
            inflateEnd(&stream);
        }
    }
    if (ok && (uint32_t)crc32(0, output, size) != crc) ok = 0;
    if (ok) {
        FILE *file = fopen(destination, "wb");
        ok = file && fwrite(output, 1, size, file) == size;
        if (file && fclose(file) != 0) ok = 0;
    }
    free(output);
    return ok;
}

static int extract_zip(const char *zip_path, const char *destination, char *error, size_t error_capacity) {
    FILE *file = fopen(zip_path, "rb");
    uint8_t *archive = NULL;
    long length;
    if (!file) { fail(error, error_capacity, "Cannot open %s", zip_path); return 0; }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 22 || (unsigned long)length > MAX_ARCHIVE_BYTES) {
        fclose(file); fail(error, error_capacity, "Unsupported archive size: %s", zip_path); return 0;
    }
    archive = malloc((size_t)length);
    if (!archive || fread(archive, 1, (size_t)length, file) != (size_t)length) {
        free(archive); fclose(file); fail(error, error_capacity, "Cannot read %s", zip_path); return 0;
    }
    fclose(file);

    size_t eocd = (size_t)length - 22;
    int found = 0;
    for (size_t scanned = 0; scanned <= 65557 && eocd + 22 <= (size_t)length; scanned++, eocd--) {
        if (le32(archive + eocd) == 0x06054b50u) { found = 1; break; }
        if (eocd == 0) break;
    }
    if (!found) { free(archive); fail(error, error_capacity, "Not a ZIP archive: %s", zip_path); return 0; }
    unsigned count = le16(archive + eocd + 10);
    size_t central = le32(archive + eocd + 16);
    if (count == 0xFFFFu || central == 0xFFFFFFFFu || count > MAX_ENTRIES) {
        free(archive); fail(error, error_capacity, "ZIP64 or oversized archive is unsupported: %s", zip_path); return 0;
    }
    int ok = cranklab_mkdirs(destination);
    for (unsigned i = 0; ok && i < count; i++) {
        if (central + 46 > (size_t)length || le32(archive + central) != 0x02014b50u) { ok = 0; break; }
        uint16_t flags = le16(archive + central + 8), method = le16(archive + central + 10);
        uint32_t crc = le32(archive + central + 16), compressed = le32(archive + central + 20), size = le32(archive + central + 24);
        uint16_t name_len = le16(archive + central + 28), extra_len = le16(archive + central + 30), comment_len = le16(archive + central + 32);
        size_t local = le32(archive + central + 42);
        size_t next = central + 46 + name_len + extra_len + comment_len;
        char name[CRANKLAB_PATH_MAX], path[CRANKLAB_PATH_MAX];
        if (next > (size_t)length || name_len == 0 || name_len >= 1024) { ok = 0; break; }
        memcpy(name, archive + central + 46, name_len);
        name[name_len] = 0;
        central = next;
        if (flags & 1) { fail(error, error_capacity, "Encrypted ZIP entries are unsupported: %s", zip_path); ok = 0; break; }
        if (!safe_entry_name(name)) continue;
        snprintf(path, sizeof(path), "%s/%s", destination, name);
        if (name[name_len - 1] == '/') { cranklab_mkdirs(path); continue; }
        char parent[CRANKLAB_PATH_MAX];
        parent_of(path, parent, sizeof(parent));
        if (!cranklab_mkdirs(parent) || !write_entry(archive, (size_t)length, local, method, compressed, size, crc, path)) {
            fail(error, error_capacity, "Corrupt or unsupported ZIP entry in %s", zip_path);
            ok = 0;
        }
    }
    free(archive);
    if (ok) return 1;
    if (error && !error[0]) fail(error, error_capacity, "Damaged ZIP archive: %s", zip_path);
    return 0;
}

/* The archive may hold the game directly, or inside a single top-level folder. */
static int find_pdx_root(const char *directory, char *out, size_t capacity, int depth) {
    if (looks_like_pdx(directory)) { snprintf(out, capacity, "%s", directory); return 1; }
    if (depth >= 2) return 0;
    DIR *d = opendir(directory);
    if (!d) return 0;
    struct dirent *entry;
    int found = 0;
    while (!found && (entry = readdir(d))) {
        char child[CRANKLAB_PATH_MAX];
        if (entry->d_name[0] == '.') continue;
        snprintf(child, sizeof(child), "%s/%s", directory, entry->d_name);
        if (is_dir(child)) found = find_pdx_root(child, out, capacity, depth + 1);
    }
    closedir(d);
    return found;
}

static uint32_t fnv1a(const char *text, uint32_t hash) {
    for (; *text; text++) { hash ^= (uint8_t)*text; hash *= 16777619u; }
    return hash;
}

static int resolve_zip(const char *zip_path, const char *cache_root, char *pdx_dir, size_t capacity,
                       char *error, size_t error_capacity) {
    struct stat st;
    char cache[CRANKLAB_PATH_MAX], stamp_path[CRANKLAB_PATH_MAX], stamp[128], existing[128] = "";
    if (stat(zip_path, &st) != 0) { fail(error, error_capacity, "Cannot open %s", zip_path); return 0; }
    uint32_t hash = fnv1a(zip_path, 2166136261u);
    snprintf(stamp, sizeof(stamp), "%lld:%lld", (long long)st.st_size, (long long)st.st_mtime);
    hash = fnv1a(stamp, hash);
    char title[96];
    snprintf(title, sizeof(title), "%s", base_name(zip_path));
    for (char *p = title; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_')) *p = '_';
    snprintf(cache, sizeof(cache), "%s/cranklab/cache/%.60s-%08x", cache_root, title, hash);
    snprintf(stamp_path, sizeof(stamp_path), "%s/.cranklab_extracted", cache);
    FILE *marker = fopen(stamp_path, "r");
    if (marker) { if (!fgets(existing, sizeof(existing), marker)) existing[0] = 0; fclose(marker); }
    if (strcmp(existing, stamp) != 0) {
        if (!extract_zip(zip_path, cache, error, error_capacity)) return 0;
        marker = fopen(stamp_path, "w");
        if (marker) { fputs(stamp, marker); fclose(marker); }
    }
    if (!find_pdx_root(cache, pdx_dir, capacity, 0)) {
        fail(error, error_capacity, "No Playdate game (pdxinfo or main.pdz) found in %s", zip_path);
        return 0;
    }
    return 1;
}

static int resolve_pointer(const char *pointer_path, char *pdx_dir, size_t capacity, char *error, size_t error_capacity) {
    FILE *file = fopen(pointer_path, "r");
    char line[CRANKLAB_PATH_MAX], base[CRANKLAB_PATH_MAX], target[CRANKLAB_PATH_MAX * 2];
    if (!file) { fail(error, error_capacity, "Cannot open %s", pointer_path); return 0; }
    if (!fgets(line, sizeof(line), file)) line[0] = 0;
    fclose(file);
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) { fail(error, error_capacity, "%s is empty", pointer_path); return 0; }
    parent_of(pointer_path, base, sizeof(base));
    if (line[0] == '/') snprintf(target, sizeof(target), "%s", line);
    else snprintf(target, sizeof(target), "%s/%s", base, line);
    if (!is_dir(target) || !looks_like_pdx(target)) {
        fail(error, error_capacity, "Pointer does not name a Playdate folder: %s", target);
        return 0;
    }
    snprintf(pdx_dir, capacity, "%s", target);
    return 1;
}

int cranklab_resolve_content(const char *content, const char *cache_root,
                             char *pdx_dir, size_t capacity, char *error, size_t error_capacity) {
    char parent[CRANKLAB_PATH_MAX];
    if (error && error_capacity) error[0] = 0;
    if (!content || !content[0] || strlen(content) >= CRANKLAB_PATH_MAX - 64) {
        fail(error, error_capacity, "Invalid content path%s", ""); return 0;
    }
    if (is_dir(content)) {
        if (!looks_like_pdx(content)) { fail(error, error_capacity, "Not a Playdate folder: %s", content); return 0; }
        snprintf(pdx_dir, capacity, "%s", content);
        return 1;
    }
    if (has_extension(content, ".zip")) return resolve_zip(content, cache_root, pdx_dir, capacity, error, error_capacity);
    if (has_extension(content, ".pdxl")) return resolve_pointer(content, pdx_dir, capacity, error, error_capacity);
    /* main.pdz, pdxinfo, pdex.bin and friends: the folder around the file is the game. */
    parent_of(content, parent, sizeof(parent));
    if (!looks_like_pdx(parent)) {
        fail(error, error_capacity, "No Playdate game files next to %s", content);
        return 0;
    }
    snprintf(pdx_dir, capacity, "%s", parent);
    return 1;
}
