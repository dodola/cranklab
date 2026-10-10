#include "micro_runtime.h"
#define MICRO_ALLOW_SYSTEM_ALLOC
#include "pd_runtime.h"
#include "pd_pdx.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "save_identity.h"
#include <esp_heap_caps.h>
int micro_core_access(const char *path,int mode) {
    struct stat st;
    if(mode!=F_OK) {errno=ENOTSUP;return -1;}
    return stat(path,&st);
}
#define access micro_core_access

PDRuntime g_pd;
static char last_error[512];
static unsigned lua_bytes;
static lua_State *update_co;
static lua_State *active_lua;
static int update_ref = LUA_NOREF;
static void *core_allocations[1536];
static FILE *core_files[16];

static void *track_core(void *ptr) {
    if(!ptr)return NULL;
    for(unsigned i=0;i<1536;i++) if(!core_allocations[i]) {core_allocations[i]=ptr;return ptr;}
    free(ptr);
    if(active_lua)luaL_error(active_lua,"Too many game asset allocations");
    pd_host_fail("Too many game asset allocations");return NULL;
}
void micro_core_free(void *ptr) {
    for(unsigned i=0;i<1536;i++)if(core_allocations[i]==ptr){core_allocations[i]=NULL;break;}
    free(ptr);
}
FILE *micro_core_fopen(const char *path,const char *mode) {
    FILE *file=fopen(path,mode);if(!file)return NULL;
    for(unsigned i=0;i<16;i++)if(!core_files[i]){core_files[i]=file;return file;}
    fclose(file);errno=EMFILE;return NULL;
}
int micro_core_fclose(FILE *file) {
    if(!file)return 0;
    for(unsigned i=0;i<16;i++)if(core_files[i]==file){core_files[i]=NULL;break;}
    return fclose(file);
}

extern uint64_t micro_board_millis(void);
uint64_t pd_time_milliseconds(void) { return micro_board_millis(); }
void pd_host_fail(const char *error) {
    snprintf(last_error, sizeof(last_error), "%s", error ? error : "Unknown runtime error");
    g_pd.running = 0;
}
const char *micro_error(void) { return last_error; }
unsigned micro_lua_bytes(void) { return lua_bytes; }
void micro_asset_stats(unsigned *count,unsigned *bytes) {
    *count=0;*bytes=0;
    for(unsigned i=0;i<1536;i++)if(core_allocations[i]) {
        (*count)++;*bytes+=heap_caps_get_allocated_size(core_allocations[i]);
    }
}
const uint8_t *micro_frame(void) { return pd_screen_bitmap ? pd_screen_bitmap->data : NULL; }
float micro_refresh_rate(void) { return g_pd.refresh_rate; }
void *micro_core_malloc(size_t size) {
    void *p=malloc(size);
    if(!p && size && active_lua){lua_gc(active_lua,LUA_GCCOLLECT,0);p=malloc(size);}
    if(!p && size && active_lua) luaL_error(active_lua,"Not enough RAM for game assets (%d bytes)",(int)size);
    return track_core(p);
}
void *micro_core_calloc(size_t count,size_t size) {
    void *p=calloc(count,size);
    if(!p && count && size && active_lua){lua_gc(active_lua,LUA_GCCOLLECT,0);p=calloc(count,size);}
    if(!p && count && size && active_lua) luaL_error(active_lua,"Not enough RAM for game assets");
    return track_core(p);
}
void *micro_core_realloc(void *ptr,size_t size) {
    void *p=realloc(ptr,size);
    if(!p && size && active_lua){lua_gc(active_lua,LUA_GCCOLLECT,0);p=realloc(ptr,size);}
    if(!p && size && active_lua) luaL_error(active_lua,"Not enough RAM for game assets");
    if(p || !size) {
        int found=0;
        if(ptr)for(unsigned i=0;i<1536;i++)if(core_allocations[i]==ptr){core_allocations[i]=p;found=1;break;}
        if(!found)p=track_core(p);
    }
    return p;
}
void *micro_core_persistent_calloc(size_t count,size_t size) {
    void *p=calloc(count,size);
    if(!p && count && size && active_lua)luaL_error(active_lua,"Not enough RAM for system font");
    return p;
}
char *micro_core_strdup(const char *value) {
    size_t size=strlen(value)+1;
    char *copy=micro_core_malloc(size);
    if(copy)memcpy(copy,value,size);
    return copy;
}
int pd_host_active(void) { return 0; } /* Avoid desktop menu image expansion to RGBA. */
void pd_host_menu(const char *json, const uint8_t *image) {}

static void *lua_alloc(void *ud, void *ptr, size_t old, size_t size) {
    if (!ptr) old = 0;
    if (!size) { free(ptr); lua_bytes -= old; return NULL; }
    void *p = realloc(ptr, size);
    if (p) lua_bytes = lua_bytes - old + size;
    return p;
}

int pd_call_lifecycle(lua_State *L, const char *name) {
    int top = lua_gettop(L);
    lua_getglobal(L, "playdate"); lua_getfield(L, -1, name);
    int ok = 1;
    if (lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) != LUA_OK) {
        pd_host_fail(lua_tostring(L, -1)); ok = 0;
    }
    lua_settop(L, top); return ok;
}
void pd_call_input_handlers(int button, int down) {
    static const char *names[6][2] = {{"leftButtonUp","leftButtonDown"},
        {"rightButtonUp","rightButtonDown"},{"upButtonUp","upButtonDown"},
        {"downButtonUp","downButtonDown"},{"BButtonUp","BButtonDown"},{"AButtonUp","AButtonDown"}};
    int bit = 0; while (bit < 6 && button != (1 << bit)) bit++;
    if (bit == 6 || !g_pd.L) return;
    const char *name = names[bit][down != 0];
    if (!pd_dispatch_input_handler(g_pd.L, name)) pd_call_lifecycle(g_pd.L, name);
}

static void call_crank(lua_State *L, float delta) {
    int top = lua_gettop(L);
    lua_getfield(L,LUA_REGISTRYINDEX,"pd.inputHandlers.stack");
    int handled = 0;
    if (lua_istable(L,-1)) {
        for (int i = (int)lua_rawlen(L,-1); i > 0 && !handled; i--) {
            lua_rawgeti(L,-1,i); lua_getfield(L,-1,"masksOthers");
            int masks = lua_toboolean(L,-1); lua_pop(L,1);
            lua_getfield(L,-1,"handler"); lua_getfield(L,-1,"cranked");
            if (lua_isfunction(L,-1)) {
                lua_pushnumber(L,delta); lua_pushnumber(L,delta);
                if (lua_pcall(L,2,0,0)!=LUA_OK) { pd_host_fail(lua_tostring(L,-1)); lua_pop(L,1); }
                handled = 1;
            } else lua_pop(L,1);
            lua_pop(L,2); if (masks) handled=1;
        }
    }
    lua_settop(L,top);
    if (!handled) {
        lua_getglobal(L,"playdate"); lua_getfield(L,-1,"cranked");
        if (lua_isfunction(L,-1)) {
            lua_pushnumber(L,delta); lua_pushnumber(L,delta);
            if (lua_pcall(L,2,0,0)!=LUA_OK) pd_host_fail(lua_tostring(L,-1));
        }
        lua_settop(L,top);
    }
}

static int bootstrap(lua_State *L) {
    /* Keep temporary collision tables and per-frame coroutines from growing
       to the desktop collector's much larger default high-water mark. */
    lua_gc(L,LUA_GCINC,110,200,11);
    luaL_openlibs(L);
    lua_newtable(L); lua_setglobal(L,"playdate");
    pd_graphics_register(L); pd_display_register(L); pd_input_register(L);
    pd_system_register(L); pd_file_register(L); pd_sound_register(L);
    pd_geometry_register(L); pd_sprite_register(L); pd_json_register(L);
    pd_string_register(L); pd_math_register(L); pd_table_register(L);
    pd_setup_imports(L);
    lua_getglobal(L,"playdate"); lua_newtable(L);
    lua_pushstring(L,g_pd.game_name); lua_setfield(L,-2,"name");
    lua_pushstring(L,g_pd.game_author); lua_setfield(L,-2,"author");
    lua_pushstring(L,g_pd.game_bundle_id); lua_setfield(L,-2,"bundleID");
    lua_pushstring(L,g_pd.game_version); lua_setfield(L,-2,"version");
    lua_setfield(L,-2,"metadata"); lua_pop(L,1);
    char path[512]; snprintf(path,sizeof(path),"%s/main.pdz",g_pd.pdx_dir);
    if (access(path,F_OK)==0) {
        g_pd.pdz = pdz_load(path);
        if (!g_pd.pdz) return luaL_error(L,"Cannot read main.pdz");
        PDZEntry *entry = pdz_find(g_pd.pdz,"main");
        if (!entry) entry = pdz_find(g_pd.pdz,"main.luac");
        if (!entry || !entry->data) return luaL_error(L,"main.pdz has no readable main entry");
        int status = luaL_loadbuffer(L,(const char*)entry->data,entry->size,"@main");
        micro_pdz_release(g_pd.pdz);
        if (status != LUA_OK) return lua_error(L);
    } else {
        snprintf(path,sizeof(path),"%s/main.luac",g_pd.pdx_dir);
        if (access(path,F_OK)!=0) snprintf(path,sizeof(path),"%s/main.lua",g_pd.pdx_dir);
        if (luaL_loadfile(L,path)!=LUA_OK) return lua_error(L);
    }
    lua_call(L,0,0);
    return 0;
}

void micro_stop(void) {
    LCDBitmap *screen=pd_screen_bitmap;
    if (g_pd.L) {
        if (g_pd.running) pd_call_lifecycle(g_pd.L,"gameWillTerminate");
        pd_gfx_reset_focus();
        active_lua=NULL;lua_close(g_pd.L);g_pd.L=NULL;
        pd_sprite_shutdown(); pd_sound_shutdown();
    }
    if (screen) {
        free(screen->data); free(screen->mask); free(screen);
    }
    pd_graphics_shutdown();
    pd_screen_bitmap=NULL;
    pdz_free(g_pd.pdz);micro_corelibs_close();free(g_pd.pdx_dir);free(g_pd.save_dir);
    /* Lua can longjmp from a failed asset loader. Reclaim its transient
       buffers and open files after all normal resource finalizers ran. */
    for(unsigned i=0;i<16;i++)if(core_files[i]){fclose(core_files[i]);core_files[i]=NULL;}
    for(unsigned i=0;i<1536;i++)if(core_allocations[i]){free(core_allocations[i]);core_allocations[i]=NULL;}
    memset(&g_pd,0,sizeof(g_pd)); lua_bytes=0;
    update_co=NULL; update_ref=LUA_NOREF;
}

int micro_start(const char *directory) {
    micro_stop(); last_error[0]=0;
    char native[512]; struct stat st;
    snprintf(native,sizeof(native),"%s/pdex.bin",directory);
    if (stat(native,&st)==0 && st.st_size>0) {
        pd_host_fail("ESP32 currently supports Lua PDX; native ARM games need a larger platform."); return 0;
    }
    g_pd.pdx_dir=strdup(directory); g_pd.start_time=pd_time_milliseconds();
    g_pd.running=1; g_pd.refresh_rate=30; g_pd.bg_color=kColorWhite;
    g_pd.crank_docked=0; g_pd.display_scale=1;
    PDXInfo *info=calloc(1,sizeof(PDXInfo));
    if (info && pdx_read_info(directory,info)==0) {
        snprintf(g_pd.game_name,sizeof(g_pd.game_name),"%s",info->game_name);
        snprintf(g_pd.game_author,sizeof(g_pd.game_author),"%s",info->author);
        snprintf(g_pd.game_bundle_id,sizeof(g_pd.game_bundle_id),"%s",info->bundle_id);
        snprintf(g_pd.game_version,sizeof(g_pd.game_version),"%s",info->version);
    }
    free(info);
    char component[32];micro_save_identity(g_pd.game_bundle_id,directory,component);
    char save[512];snprintf(save,sizeof(save),"/littlefs/saves/%s",component);
    mkdir("/littlefs/saves",0755);
    if(mkdir(save,0755)!=0 && (stat(save,&st)!=0 || !S_ISDIR(st.st_mode))) {
        pd_host_fail("Cannot create game save directory");return 0;
    }
    g_pd.save_dir=strdup(save);
    pd_screen_bitmap=calloc(1,sizeof(LCDBitmap));
    if (!pd_screen_bitmap) { pd_host_fail("Not enough memory for screen"); return 0; }
    g_pd.display_bitmap=pd_screen_bitmap;
    pd_screen_bitmap->width=400; pd_screen_bitmap->height=240; pd_screen_bitmap->rowbytes=50;
    pd_screen_bitmap->data=calloc(12000,1); pd_screen_bitmap->mask=malloc(12000);
    if (!pd_screen_bitmap->data || !pd_screen_bitmap->mask) {pd_host_fail("Not enough memory for screen");return 0;}
    memset(pd_screen_bitmap->mask,255,12000);
    g_pd.L=lua_newstate(lua_alloc,NULL);
    if (!g_pd.L) {pd_host_fail("Not enough memory for Lua");return 0;}
    active_lua=g_pd.L;
    lua_pushcfunction(g_pd.L,bootstrap);
    if (lua_pcall(g_pd.L,0,0,0)!=LUA_OK) {
        pd_host_fail(lua_tostring(g_pd.L,-1));lua_pop(g_pd.L,1);return 0;
    }
    lua_gc(g_pd.L,LUA_GCCOLLECT,0);
    return 1;
}

static int frame_update(lua_State *L) {
    for(int bit=0;bit<6;bit++) {
        if(g_pd.buttons_pushed&(1<<bit))pd_call_input_handlers(1<<bit,1);
        if(g_pd.buttons_released&(1<<bit))pd_call_input_handlers(1<<bit,0);
    }
    if(g_pd.crank_change)call_crank(L,g_pd.crank_change);
    if(!g_pd.running)return 0;
    if (!update_co) {
        lua_getglobal(L,"playdate"); lua_getfield(L,-1,"update");
        if (lua_isfunction(L,-1)) {
            update_co=lua_newthread(L); update_ref=luaL_ref(L,LUA_REGISTRYINDEX);
            lua_pushvalue(L,-1); lua_xmove(L,update_co,1);
        }
        lua_pop(L,2);
    }
    if (update_co && pd_time_milliseconds()>=g_pd.wait_until) {
        int nres=0;active_lua=update_co;
        int result=lua_resume(update_co,L,0,&nres);active_lua=L;
        if (result==LUA_YIELD) lua_pop(update_co,nres);
        else {
            if(result!=LUA_OK) {
                luaL_traceback(L,update_co,lua_tostring(update_co,-1),0);
                pd_host_fail(lua_tostring(L,-1));lua_pop(L,1);
            }
            luaL_unref(L,LUA_REGISTRYINDEX,update_ref);update_co=NULL;update_ref=LUA_NOREF;
        }
    }
    pd_gfx_reset_focus();
    lua_gc(L,LUA_GCSTEP,8);
    pd_sprite_native_collect(L);
    return 0;
}

int micro_step(uint32_t buttons,float delta,int docked) {
    if (!g_pd.running || !g_pd.L) return 0;
    g_pd.buttons_pushed=buttons & ~g_pd.buttons_current;
    g_pd.buttons_released=g_pd.buttons_current & ~buttons;
    g_pd.buttons_current=buttons;
    g_pd.crank_change=delta; g_pd.crank_angle=fmodf(g_pd.crank_angle+delta+360,360);
    g_pd.crank_docked=docked;
    pd_frame_serial++; pd_frame_draw_ops=0;g_sprites_drawn_this_frame=0;pd_damage_reset();
    lua_pushcfunction(g_pd.L,frame_update);
    if(lua_pcall(g_pd.L,0,0,0)!=LUA_OK) {
        pd_host_fail(lua_tostring(g_pd.L,-1));lua_pop(g_pd.L,1);
    }
    return g_pd.running;
}
