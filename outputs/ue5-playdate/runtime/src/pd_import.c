#include "pd_runtime.h"
#include "pd_pdx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

extern PDRuntime g_pd;

#define IMPORT_CACHE_KEY "pd.import.cache"

/* Returns 1 if already imported (nothing pushed), else marks it and returns 0. */
static int import_seen(lua_State *L, const char *modname) {
    lua_getfield(L, LUA_REGISTRYINDEX, IMPORT_CACHE_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_CACHE_KEY);
    }
    lua_getfield(L, -1, modname);
    int seen = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!seen) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, modname);
    }
    lua_pop(L, 1);
    return seen;
}

int pd_import(lua_State *L) {
    const char *modname = luaL_checkstring(L, 1);

    /* Playdate's import() loads each module only once */
    if (import_seen(L, modname)) {
        lua_pushnil(L);
        return 1;
    }

    if (g_pd.pdz) {
        PDZFile *pdz = (PDZFile *)g_pd.pdz;
        PDZEntry *entry = pdz_find(pdz, modname);
        if (!entry) {
            char namebuf[512];
            snprintf(namebuf, sizeof(namebuf), "%s.luac", modname);
            entry = pdz_find(pdz, namebuf);
        }
        if (entry) {
            if (entry->entry_type != 1) {
                lua_newtable(L);
                return 1;
            }
            char chunkname[600];
            snprintf(chunkname, sizeof(chunkname), "@%s", entry->filename);
            int status = luaL_loadbuffer(L, (const char *)entry->data, entry->size, chunkname);
            if (status == LUA_OK) {
                status = lua_pcall(L, 0, 1, 0);
                if (status == LUA_OK && lua_gettop(L) > 0) return 1;
                if (status != LUA_OK) {
                    const char *msg = lua_tostring(L, -1);
                    fprintf(stderr, "[import] error loading %s: %s\n", modname, msg);
                    lua_error(L);
                }
            } else {
                const char *msg = lua_tostring(L, -1);
                fprintf(stderr, "[import] error loading %s: %s\n", modname, msg);
                lua_error(L);
            }
            return 0;
        }
    }

    char path[1024];
    if (strncmp(modname, "CoreLibs/", 9) == 0) {
        snprintf(path, sizeof(path), "%s/CoreLibs/%s.lua",
                 g_pd.pdx_dir ? g_pd.pdx_dir : ".", modname + 9);
        if (access(path, F_OK) != 0) {
            const char *corelibs_dir = getenv("PD_CORELIBS_DIR");
            if (!corelibs_dir) corelibs_dir = "./CoreLibs";
            snprintf(path, sizeof(path), "%s/%s.lua", corelibs_dir, modname + 9);
        }
    } else {
        snprintf(path, sizeof(path), "%s/%s.luac", g_pd.pdx_dir, modname);
        if (access(path, F_OK) != 0) {
            snprintf(path, sizeof(path), "%s/%s.lua", g_pd.pdx_dir, modname);
        }
    }

    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "@%s", path);

    int status = luaL_dofile(L, path);
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        fprintf(stderr, "[import] error loading %s: %s\n", modname, msg);
        lua_error(L);
        return 0;
    }
    if (lua_gettop(L) > 0) return 1;
    lua_pushnil(L);
    return 1;
}

void pd_setup_imports(lua_State *L) {
    lua_pushcfunction(L, pd_import);
    lua_setglobal(L, "import");
}
