#include "pd_runtime.h"
#include "pd_pdx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static int lua_file_open(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int mode = (int)luaL_optinteger(L, 2, 0);
    const int writing = mode & ((1 << 2) | (2 << 2));
    char fullpath[1024];
    if (writing)
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
    else
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    const char *fmode = "rb";
    if (writing) {
        if (mode & (2 << 2))
            fmode = "ab";
        else
            fmode = "wb";
    }
    FILE *f = fopen(fullpath, fmode);
    if (!f && !writing) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
        f = fopen(fullpath, fmode);
    }
    if (!f) { lua_pushnil(L); return 1; }
    FILE **ud = lua_newuserdata(L, sizeof(FILE *));
    *ud = f;
    luaL_getmetatable(L, "pd.file");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_file_close(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    if (*ud) { fclose(*ud); *ud = NULL; }
    return 0;
}

static int lua_file_read(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    int len = (int)luaL_checkinteger(L, 2);
    char *buf = malloc(len);
    int n = fread(buf, 1, len, *ud);
    lua_pushlstring(L, buf, n);
    free(buf);
    return 1;
}

static int lua_file_write(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    size_t len;
    lua_getglobal(L, "tostring");
    lua_pushvalue(L, 2);
    lua_call(L, 1, 1);
    const char *data = lua_isstring(L, -1) ? lua_tolstring(L, -1, &len) : NULL;
    int n = data ? (int)fwrite(data, 1, len, *ud) : 0;
    lua_pop(L, 1);
    lua_pushinteger(L, n);
    return 1;
}

static int lua_file_flush(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    fflush(*ud);
    return 0;
}

static int lua_file_tell(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    lua_pushinteger(L, (int)ftell(*ud));
    return 1;
}

static int lua_file_seek(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    int pos = (int)luaL_checkinteger(L, 2);
    int whence = (int)luaL_optinteger(L, 3, 0);
    fseek(*ud, pos, whence);
    return 0;
}

/* playdate.file.load(path) -> function (like loadfile; supports .pdz) */
static int lua_file_load(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char full[1200];

    /* try <path>.pdz (or literal path if it already ends in .pdz) */
    size_t plen = strlen(path);
    if (plen > 4 && strcmp(path + plen - 4, ".pdz") == 0)
        snprintf(full, sizeof(full), "%s/%s", g_pd.pdx_dir, path);
    else
        snprintf(full, sizeof(full), "%s/%s.pdz", g_pd.pdx_dir, path);
    if (access(full, F_OK) == 0) {
        PDZFile *pdz = pdz_load(full);
        if (!pdz) {
            lua_pushnil(L);
            lua_pushstring(L, "could not read pdz");
            return 2;
        }
        PDZEntry *entry = NULL;
        for (int i = 0; i < pdz->entry_count; i++) {
            if (pdz->entries[i].entry_type == 1) {
                entry = &pdz->entries[i];
                break;
            }
        }
        if (!entry) {
            lua_pushnil(L);
            lua_pushstring(L, "pdz has no lua entry");
            return 2;
        }
        char chunkname[600];
        snprintf(chunkname, sizeof(chunkname), "@%s", entry->filename);
        if (luaL_loadbuffer(L, (const char *)entry->data, entry->size, chunkname) != LUA_OK) {
            lua_pushnil(L);
            lua_insert(L, -2);
            return 2;
        }
        return 1;
    }

    /* fall back to .luac / .lua source */
    snprintf(full, sizeof(full), "%s/%s.luac", g_pd.pdx_dir, path);
    if (access(full, F_OK) != 0)
        snprintf(full, sizeof(full), "%s/%s.lua", g_pd.pdx_dir, path);
    if (access(full, F_OK) != 0)
        snprintf(full, sizeof(full), "%s/%s", g_pd.pdx_dir, path);
    if (luaL_loadfile(L, full) != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    return 1;
}

/* playdate.file.run(path) -> executes the loaded chunk, returning its results */
static int lua_file_run(lua_State *L) {
    int n = lua_file_load(L);
    if (n != 1 || !lua_isfunction(L, -1)) {
        return luaL_error(L, "file.run: %s",
                          n == 2 && lua_isstring(L, -1) ? lua_tostring(L, -1) : "load failed");
    }
    int base = lua_gettop(L) - 1;
    lua_call(L, 0, LUA_MULTRET);
    return lua_gettop(L) - base;
}

static int lua_file_listfiles(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    /* strip trailing slash for the case-fix walk */
    size_t fpl = strlen(fullpath);
    while (fpl > 1 && fullpath[fpl - 1] == '/') fullpath[--fpl] = 0;
    pd_fix_path_case(fullpath);
    DIR *d = opendir(fullpath);
    if (!d) {
        /* also look in the save dir (games list their own saved files) */
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
        d = opendir(fullpath);
    }
    if (!d) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    int i = 1;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char childpath[1200];
        snprintf(childpath, sizeof(childpath), "%s/%s", fullpath, entry->d_name);
        struct stat st;
        if (stat(childpath, &st) == 0 && S_ISDIR(st.st_mode)) {
            char name[300];
            snprintf(name, sizeof(name), "%s/", entry->d_name);
            lua_pushstring(L, name);
        } else {
            lua_pushstring(L, entry->d_name);
        }
        lua_rawseti(L, -2, i++);
    }
    closedir(d);
    return 1;
}

static int lua_file_getSize(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    struct stat st;
    if (stat(fullpath, &st) != 0) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
        if (stat(fullpath, &st) != 0) {
            /* SDK reports 0 for missing files; games compare it numerically */
            lua_pushinteger(L, 0);
            return 1;
        }
    }
    lua_pushinteger(L, (lua_Integer)st.st_size);
    return 1;
}

static int lua_file_exists(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    int exists = access(fullpath, F_OK) == 0;
    if (!exists) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
        exists = access(fullpath, F_OK) == 0;
    }
    lua_pushboolean(L, exists);
    return 1;
}

static int lua_file_isdir(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    struct stat st;
    lua_pushboolean(L, stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode));
    return 1;
}

static int lua_file_stat(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    struct stat st;
    if (stat(fullpath, &st) != 0) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    lua_pushboolean(L, S_ISDIR(st.st_mode));
    lua_setfield(L, -2, "isdir");
    lua_pushinteger(L, st.st_size);
    lua_setfield(L, -2, "size");
    return 1;
}

static int lua_file_mkdir(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
    int result = mkdir(fullpath, 0755);
    struct stat st;
    lua_pushboolean(L, result == 0 || (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)));
    return 1;
}

static int lua_file_unlink(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    unlink(fullpath);
    return 0;
}

static int lua_file_rename(lua_State *L) {
    const char *from = luaL_checkstring(L, 1);
    const char *to = luaL_checkstring(L, 2);
    char fullfrom[1024], fullto[1024];
    snprintf(fullfrom, sizeof(fullfrom), "%s/%s", g_pd.pdx_dir, from);
    snprintf(fullto, sizeof(fullto), "%s/%s", g_pd.pdx_dir, to);
    rename(fullfrom, fullto);
    return 0;
}

static int lua_file_geterr(lua_State *L) {
    (void)L;
    lua_pushstring(L, "");
    return 1;
}

static int lua_datastore_write(lua_State *L) {
    // playdate.datastore.write(table_or_name, [name]) — if first arg is a table, second is the name
    const char *name;
    int table_arg = 1;
    if (lua_istable(L, 1)) {
        name = luaL_optstring(L, 2, "data");
    } else {
        name = luaL_optstring(L, 1, "data");
        table_arg = 0;
    }
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s.json", g_pd.save_dir, name);
    FILE *f = fopen(fullpath, "wb");
    if (!f) { lua_pushboolean(L, 0); return 1; }
    if (table_arg) {
        lua_getglobal(L, "playdate");
        lua_getfield(L, -1, "json");
        lua_getfield(L, -1, "encode");
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        const char *json = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
        if (json) fputs(json, f);
        lua_pop(L, 3);
    } else {
        fputs("{}", f);
    }
    fclose(f);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_datastore_delete(lua_State *L) {
    const char *name = luaL_optstring(L, 1, "data");
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s.json", g_pd.save_dir, name);
    lua_pushboolean(L, unlink(fullpath) == 0);
    return 1;
}

static int lua_datastore_writeImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = ud ? *ud : NULL;
    const char *path = luaL_checkstring(L, 2);
    if (!bm || !path) { lua_pushboolean(L, 0); return 1; }
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
    FILE *f = fopen(fullpath, "wb");
    if (!f) { lua_pushboolean(L, 0); return 1; }
    /* PDI header: "Playdate IMG" + flags (0 = uncompressed) */
    fwrite("Playdate IMG", 1, 12, f);
    uint32_t flags = 0;
    fwrite(&flags, 4, 1, f);
    /* Cell header: width, height, stride, left, right, top, bottom, flags */
    uint16_t cw = (uint16_t)bm->width;
    uint16_t ch = (uint16_t)bm->height;
    uint16_t stride = (uint16_t)((bm->width + 7) / 8);
    uint16_t cl = 0, cr = 0, ct = 0, cb = 0;
    uint16_t cflags = bm->mask ? 0x3 : 0;
    fwrite(&cw, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&stride, 2, 1, f);
    fwrite(&cl, 2, 1, f);
    fwrite(&cr, 2, 1, f);
    fwrite(&ct, 2, 1, f);
    fwrite(&cb, 2, 1, f);
    fwrite(&cflags, 2, 1, f);
    /* Pixel data: PDI stores 1 = white, 0 = black; we store 1 = black */
    for (int y = 0; y < bm->height; y++) {
        for (int x = 0; x < stride; x++) {
            uint8_t v = bm->data[y * bm->rowbytes + x];
            fputc(~v, f); /* invert */
        }
    }
    /* Mask data if present */
    if (bm->mask) {
        for (int y = 0; y < bm->height; y++) {
            for (int x = 0; x < stride; x++)
                fputc(bm->mask[y * bm->rowbytes + x], f);
        }
    }
    fclose(f);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_datastore_readImage(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
    extern LCDBitmap *pd_load_pdi(const char *path);
    LCDBitmap *bm = pd_load_pdi(fullpath);
    if (!bm) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s.pdi", g_pd.save_dir, path);
        bm = pd_load_pdi(fullpath);
    }
    if (!bm) { lua_pushnil(L); return 1; }
    LCDBitmap **out = lua_newuserdata(L, sizeof(LCDBitmap *));
    *out = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_file_delete(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
    lua_pushboolean(L, unlink(fullpath) == 0 || rmdir(fullpath) == 0);
    return 1;
}

static int lua_datastore_read(lua_State *L) {
    const char *name = luaL_optstring(L, 1, "data");
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s.json", g_pd.save_dir, name);
    FILE *f = fopen(fullpath, "rb");
    if (!f) {
        /* fall back to files bundled inside the pdx (SDK resolves both) */
        snprintf(fullpath, sizeof(fullpath), "%s/%s.json", g_pd.pdx_dir, name);
        f = fopen(fullpath, "rb");
    }
    if (!f) { lua_pushnil(L); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    lua_getglobal(L, "playdate");
    lua_getfield(L, -1, "json");
    lua_getfield(L, -1, "decode");
    lua_pushstring(L, buf);
    lua_call(L, 1, 1);
    lua_remove(L, -3);
    lua_remove(L, -2);
    free(buf);
    return 1;
}

static int lua_file_readline(lua_State *L) {
    FILE **ud = luaL_checkudata(L, 1, "pd.file");
    if (!*ud) { lua_pushnil(L); return 1; }
    char line[4096];
    if (!fgets(line, sizeof(line), *ud)) {
        lua_pushnil(L);
        return 1;
    }
    line[strcspn(line, "\r\n")] = 0;
    lua_pushstring(L, line);
    return 1;
}

static const luaL_Reg file_methods[] = {
    {"close", lua_file_close},
    {"read", lua_file_read},
    {"readline", lua_file_readline},
    {"write", lua_file_write},
    {"flush", lua_file_flush},
    {"tell", lua_file_tell},
    {"seek", lua_file_seek},
    {NULL, NULL}
};

static const luaL_Reg file_funcs[] = {
    {"open", lua_file_open},
    {"listfiles", lua_file_listfiles},
    {"listFiles", lua_file_listfiles},
    {"load", lua_file_load},
    {"run", lua_file_run},
    {"getSize", lua_file_getSize},
    {"delete", lua_file_delete},
    {"stat", lua_file_stat},
    {"exists", lua_file_exists},
    {"isdir", lua_file_isdir},
    {"mkdir", lua_file_mkdir},
    {"unlink", lua_file_unlink},
    {"rename", lua_file_rename},
    {"geterr", lua_file_geterr},
    {NULL, NULL}
};

void pd_file_register(lua_State *L) {
    luaL_newmetatable(L, "pd.file");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, file_methods, 0);
    lua_pushcfunction(L, lua_file_close);
    lua_setfield(L, -2, "__gc");
    lua_pop(L, 1);

    lua_getglobal(L, "playdate");

    lua_newtable(L);
    luaL_setfuncs(L, file_funcs, 0);
    lua_pushinteger(L, (1 << 0)); lua_setfield(L, -2, "kFileRead");
    lua_pushinteger(L, (1 << 1)); lua_setfield(L, -2, "kFileReadData");
    lua_pushinteger(L, (1 << 2)); lua_setfield(L, -2, "kFileWrite");
    lua_pushinteger(L, (2 << 2)); lua_setfield(L, -2, "kFileAppend");
    lua_setfield(L, -2, "file");

    lua_newtable(L);
    lua_pushcfunction(L, lua_datastore_write);
    lua_setfield(L, -2, "write");
    lua_pushcfunction(L, lua_datastore_read);
    lua_setfield(L, -2, "read");
    lua_pushcfunction(L, lua_datastore_delete);
    lua_setfield(L, -2, "delete");
    lua_pushcfunction(L, lua_datastore_writeImage);
    lua_setfield(L, -2, "writeImage");
    lua_pushcfunction(L, lua_datastore_readImage);
    lua_setfield(L, -2, "readImage");
    lua_setfield(L, -2, "datastore");

    lua_pop(L, 1);
}
