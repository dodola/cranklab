#include "pd_runtime.h"
#include "crank_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static SDL_mutex *bridge_mutex = NULL;
static SDL_cond *suspend_condition = NULL;
static SDL_Thread *game_thread = NULL;
static SDL_atomic_t state, stop_requested, suspend_requested;
#ifdef __EMSCRIPTEN__
/* Web hosts must service filesystem proxy calls while termination saves run.
   Request stop, poll completion, and only then join the finished game thread. */
static SDL_atomic_t web_game_done;
void crank_web_request_stop(void) {
    if(bridge_mutex) SDL_LockMutex(bridge_mutex);
    SDL_AtomicSet(&stop_requested,1);
    if(suspend_condition) SDL_CondBroadcast(suspend_condition);
    if(bridge_mutex) SDL_UnlockMutex(bridge_mutex);
}
int crank_web_can_join(void) { return SDL_AtomicGet(&web_game_done); }
#endif
/* Owned by the game thread, including the clock used by Lua and players. */
static uint32_t applied_suspend = 0;
static uint64_t suspended_since = 0, suspended_duration = 0;
static int active = 0;
/* Lockstep: the host paces the game thread, and game time is counted in stepped frames. */
static int lockstep_requested = 0, lockstep = 0;
static SDL_sem *step_request = NULL, *step_done = NULL;
static SDL_atomic_t virtual_ms;
static uint64_t virtual_us = 0, audio_remainder = 0; /* owned by the stepping thread */
#ifdef CRANK_LOCKSTEP_AUDIO
extern void SDL_CrankAudioSetGate(int on, int credit);
extern void SDL_CrankAudioAddCredit(int frames);
#endif
static uint8_t pixels[400*240*4];
static uint32_t serial = 0, buttons = 0;
static float crank_delta = 0;
#define AUDIO_CAPACITY 44100
static int16_t audio_samples[AUDIO_CAPACITY];
static uint32_t audio_read = 0, audio_count = 0;
static int crank_docked = 1;
static char game_path[2048], save_path[2048], last_error[2048];
typedef struct { uint32_t id; int value; } MenuCommand;
static MenuCommand menu_commands[16];
static int menu_command_count=0, menu_requested=0, menu_applied=0;
static char *menu_json=NULL;
static char *native_json=NULL;
static uint8_t menu_image[400*240*4];
static uint32_t menu_image_serial=0;
static int menu_has_image=0;
static int launcher_enabled=0, launcher_activate=0;
static uint32_t launcher_session=0;
static char launcher_reply[262144];
typedef struct { int id; char action[16]; } LauncherCommand;
static LauncherCommand launcher_commands[16];
static int launcher_command_count=0;
#ifndef PD_LAUNCHER_SCRIPT_INCLUDE
#define PD_LAUNCHER_SCRIPT_INCLUDE "pd_launcher_script.inc"
#endif
#include PD_LAUNCHER_SCRIPT_INCLUDE

static int launcher_send(lua_State *L) {
    const char *action=luaL_checkstring(L,1); int id=(int)luaL_optinteger(L,2,0);
    if(strcmp(action,"inspect") && strcmp(action,"leave") && strcmp(action,"play") && strcmp(action,"cancel") && strcmp(action,"publisher") && strcmp(action,"library") && strcmp(action,"translate") && strcmp(action,"original") && strcmp(action,"trans_cancel") && strcmp(action,"model") && strcmp(action,"model_cancel") && strcmp(action,"lang_zh") && strcmp(action,"lang_en")) return luaL_error(L,"Invalid launcher operation");
    if(id<0 || id>1000000) return luaL_error(L,"Invalid catalog ID");
    SDL_LockMutex(bridge_mutex);
    int accepted=launcher_enabled && launcher_command_count<16;
    if(accepted) {
        LauncherCommand *command=&launcher_commands[launcher_command_count++];
        command->id=id;snprintf(command->action,sizeof(command->action),"%s",action);
    }
    SDL_UnlockMutex(bridge_mutex);lua_pushboolean(L,accepted);return 1;
}
static int launcher_status(lua_State *L) {
    char json[262144];SDL_LockMutex(bridge_mutex);
    snprintf(json,sizeof(json),"%s",launcher_reply);SDL_UnlockMutex(bridge_mutex);
    lua_pushstring(L,json);return 1;
}
int pd_host_launcher_install(lua_State *L) {
    if(!active || !launcher_enabled) return 1;
    if(strcmp(g_pd.game_bundle_id,"com.pdgamez.browser") || strcmp(g_pd.game_build,"18")) {
        pd_host_fail("The bundled launcher adapter requires PDGames build 18");return 0;
    }
    lua_newtable(L);lua_pushcfunction(L,launcher_send);lua_setfield(L,-2,"send");
    lua_pushcfunction(L,launcher_status);lua_setfield(L,-2,"status");lua_setglobal(L,"_crankLauncher");
    if(luaL_loadbuffer(L,pd_launcher_script,sizeof(pd_launcher_script)-1,"@CrankLab/PDGames")!=LUA_OK || lua_pcall(L,0,0,0)!=LUA_OK) {
        pd_host_fail(lua_tostring(L,-1));lua_pop(L,1);return 0;
    }
    return 1;
}
static int process_launcher(lua_State *L) {
    SDL_LockMutex(bridge_mutex);int activate=launcher_activate;launcher_activate=0;SDL_UnlockMutex(bridge_mutex);
    if(activate && launcher_enabled && !applied_suspend) {
        lua_getglobal(L,activate==2 ? "cranklabLauncherTranslate" : activate==3 ? "cranklabLauncherDetails" : "cranklabLauncherActivate");
        if(lua_isfunction(L,-1)) {
            if(lua_pcall(L,0,0,0)!=LUA_OK) {pd_host_fail(lua_tostring(L,-1));lua_pop(L,1);return 0;}
        } else lua_pop(L,1);
    }
    return 1;
}
uint32_t crank_launcher_request(char *destination,uint32_t capacity) {
    if(!bridge_mutex) return 0;SDL_LockMutex(bridge_mutex);
    if(!launcher_enabled || !launcher_command_count) {SDL_UnlockMutex(bridge_mutex);return 0;}
    char json[160];LauncherCommand command=launcher_commands[0];
    snprintf(json,sizeof(json),"{\"session\":%u,\"id\":%d,\"action\":\"%s\"}",launcher_session,command.id,command.action);
    uint32_t needed=(uint32_t)strlen(json)+1;
    if(destination && capacity>=needed) {
        memcpy(destination,json,needed);launcher_command_count--;
        memmove(launcher_commands,launcher_commands+1,launcher_command_count*sizeof(LauncherCommand));
    }
    SDL_UnlockMutex(bridge_mutex);return needed;
}
int crank_launcher_reply(uint32_t session,const char *json) {
    if(!bridge_mutex || !json || strlen(json)>=sizeof(launcher_reply)) return 0;
    SDL_LockMutex(bridge_mutex);int accepted=launcher_enabled && launcher_session==session;
    if(accepted) snprintf(launcher_reply,sizeof(launcher_reply),"%s",json);
    SDL_UnlockMutex(bridge_mutex);return accepted;
}
int crank_launcher_activate(void) {
    if(!bridge_mutex) return 0;SDL_LockMutex(bridge_mutex);
    int accepted=launcher_enabled && !SDL_AtomicGet(&suspend_requested) && !launcher_activate;
    if(accepted) launcher_activate=1;SDL_UnlockMutex(bridge_mutex);return accepted;
}
int crank_launcher_detail_action(uint32_t action) {
    if(!bridge_mutex || action<2 || action>3) return 0;
    SDL_LockMutex(bridge_mutex);
    int accepted=launcher_enabled && !SDL_AtomicGet(&suspend_requested) && !launcher_activate;
    if(accepted) launcher_activate=(int)action;
    SDL_UnlockMutex(bridge_mutex);return accepted;
}

static int process_menu(lua_State *L) {
    MenuCommand commands[16]; int count, open;
    SDL_LockMutex(bridge_mutex);
    count=menu_command_count; memcpy(commands,menu_commands,count*sizeof(MenuCommand)); menu_command_count=0;
    open=menu_requested;
    SDL_UnlockMutex(bridge_mutex);
    for(int i=0;i<count;i++) if(!pd_sdkmenu_select(L,commands[i].id,commands[i].value)) return 0;
    if(!open && (menu_applied || count)) if(!pd_sdkmenu_close(L)) return 0;
    menu_applied=open; return 1;
}
int pd_host_finish_menu(lua_State *L) {
    if(!active) return 1;
    SDL_LockMutex(bridge_mutex); menu_requested=0; SDL_UnlockMutex(bridge_mutex);
    return process_menu(L);
}


int pd_host_lockstep(void) { return active && lockstep; }
/* Called by the game loop after publishing a frame: hands the frame to the host and waits
   for the next step. Suspension callbacks still run while waiting. Returns 0 on stop. */
int pd_host_step_wait(lua_State *L) {
    SDL_SemPost(step_done);
    for (;;) {
        if (SDL_SemWaitTimeout(step_request, 10) == 0) return 1;
        if (pd_host_should_stop()) return 0;
        if (!pd_host_lifecycle(L)) return 0;
    }
}
void crank_set_lockstep(int enabled) { lockstep_requested = enabled != 0; }
int crank_step(void) {
    if (!bridge_mutex || !lockstep || !step_request || !active) return 0;
    if (SDL_AtomicGet(&suspend_requested) || SDL_AtomicGet(&state) == 3) return 0;
    while (SDL_SemTryWait(step_done) == 0) {}
    float rate = g_pd.refresh_rate;
    uint64_t frame_us = rate >= 1.0f ? (uint64_t)(1000000.0f / rate) : 33333u;
    virtual_us += frame_us;
    SDL_AtomicSet(&virtual_ms, (int)(uint32_t)(virtual_us / 1000));
#ifdef CRANK_LOCKSTEP_AUDIO
    audio_remainder += frame_us * 44100u;
    SDL_CrankAudioAddCredit((int)(audio_remainder / 1000000u));
    audio_remainder %= 1000000u;
#endif
    SDL_SemPost(step_request);
    return SDL_SemWaitTimeout(step_done, 2000) == 0;
}
float crank_refresh_rate(void) { return g_pd.refresh_rate; }

int pd_host_active(void) { return active; }
int pd_host_should_stop(void) { return active && SDL_AtomicGet(&stop_requested); }
uint64_t pd_time_milliseconds(void) {
    if (lockstep) return (uint32_t)SDL_AtomicGet(&virtual_ms);
    const uint64_t now = suspended_since ? suspended_since : SDL_GetTicks64();
    return now - suspended_duration;
}
int pd_host_lifecycle(lua_State *L) {
    if (!active) return 1;
    for (;;) {
        if (pd_host_should_stop()) return 0;
        const uint32_t next = (uint32_t)SDL_AtomicGet(&suspend_requested);
        if (next != applied_suspend) {
            if (!applied_suspend && next) {
                suspended_since = SDL_GetTicks64();
                /* Apply releases before callbacks so a held key cannot stick. */
                pd_host_input();
                if (!pd_call_lifecycle(L, "gameWillPause")) return 0;
                pd_sound_set_suspended(1);
            }
            if(!process_menu(L)) return 0;
            if ((next & 2) && !(applied_suspend & 2))
                if (!pd_call_lifecycle(L, "deviceWillLock")) return 0;
            if (!(next & 2) && (applied_suspend & 2))
                if (!pd_call_lifecycle(L, "deviceDidUnlock")) return 0;
            if (applied_suspend && !next) {
                suspended_duration += SDL_GetTicks64() - suspended_since;
                suspended_since = 0;
                if (!pd_call_lifecycle(L, "gameWillResume")) return 0;
                pd_sound_set_suspended(0);
            }
            applied_suspend = next;
            if (SDL_AtomicGet(&state) != 3) SDL_AtomicSet(&state, next ? 4 : 2);
        } else if(!process_menu(L)) return 0;
        if(!process_launcher(L)) return 0;
        SDL_LockMutex(bridge_mutex);
        if (pd_host_should_stop()) { SDL_UnlockMutex(bridge_mutex); return 0; }
        /* Recheck under the same mutex as the requester, avoiding a lost wake. */
        if ((uint32_t)SDL_AtomicGet(&suspend_requested) != applied_suspend || menu_command_count || menu_requested!=menu_applied) {
            SDL_UnlockMutex(bridge_mutex); continue;
        }
        if (!applied_suspend) { SDL_UnlockMutex(bridge_mutex); return 1; }
        SDL_CondWait(suspend_condition, bridge_mutex);
        SDL_UnlockMutex(bridge_mutex);
    }
}
const char *pd_host_save_path(void) { return active ? save_path : NULL; }
void pd_host_fail(const char *error) {
    if (!active) return;
    SDL_LockMutex(bridge_mutex);
    snprintf(last_error, sizeof(last_error), "%s", error ? error : "Unknown runtime error");
    SDL_UnlockMutex(bridge_mutex);
    SDL_AtomicSet(&state, 3);
}
void pd_host_input(void) {
    if (!active) return;
    SDL_LockMutex(bridge_mutex);
    uint32_t next = buttons;
    float delta = crank_delta; crank_delta = 0;
    int docked = crank_docked;
    SDL_UnlockMutex(bridge_mutex);
    uint32_t changed = next ^ g_pd.buttons_current;
    g_pd.buttons_pushed = changed & next;
    g_pd.buttons_released = changed & g_pd.buttons_current;
    g_pd.buttons_current = next;
    for (int bit=1;bit<=32;bit<<=1)
        if (changed & bit) pd_call_input_handlers(bit, (next & bit) != 0);
    g_pd.crank_change = delta;
    g_pd.crank_angle = fmodf(g_pd.crank_angle + delta, 360.0f);
    if (g_pd.crank_angle < 0) g_pd.crank_angle += 360;
    g_pd.crank_docked = docked;
}
void pd_host_publish(void) {
    if (!active || !pd_screen_bitmap) return;
    SDL_LockMutex(bridge_mutex);
    int scale = g_pd.display_scale;
    if (scale != 2 && scale != 4 && scale != 8) scale = 1;
    for (int y=0;y<240;y++) for(int x=0;x<400;x++) {
        int sx=x/scale, sy=y/scale;
        int black=(pd_screen_bitmap->data[sy*pd_screen_bitmap->rowbytes+sx/8]>>(7-sx%8))&1;
        if(g_pd.inverted) black=!black;
        int p=(y*400+x)*4;
        pixels[p]=pixels[p+1]=pixels[p+2]=black ? 0 : 255;
        pixels[p+3]=255;
    }
    serial++;
    SDL_UnlockMutex(bridge_mutex);
    if (SDL_AtomicGet(&state) != 3) SDL_AtomicSet(&state,2);
}
#ifdef PD_LIBRARY
extern int crank_game_entry(int argc, char **argv);
static int run(void *unused) {
    char *argv[]={"crank-core",game_path,NULL};
    int result=crank_game_entry(2,argv);
    if(result && SDL_AtomicGet(&state)!=3) pd_host_fail("Game initialization or execution failed; see runtime log");
    else if(SDL_AtomicGet(&state)!=3) SDL_AtomicSet(&state,0);
#ifdef __EMSCRIPTEN__
    SDL_AtomicSet(&web_game_done,1);
#endif
    return result;
}
#endif
static int start_session(const char *pdx, const char *save,int launcher) {
#ifdef PD_LIBRARY
    if(!pdx || !save || game_thread) return 0;
    if(strlen(pdx)>=sizeof(game_path) || strlen(save)>=sizeof(save_path)) return 0;
    if(!bridge_mutex) bridge_mutex=SDL_CreateMutex(); if(!bridge_mutex) return 0;
    if(!suspend_condition) suspend_condition=SDL_CreateCond(); if(!suspend_condition) return 0;
    SDL_LockMutex(bridge_mutex);
    serial=buttons=0; crank_delta=0; crank_docked=1; audio_read=audio_count=0;
    last_error[0]=0; memset(pixels,255,sizeof(pixels));
    menu_command_count=menu_requested=menu_applied=menu_has_image=0;
    menu_image_serial=0; free(menu_json); menu_json=NULL;
    free(native_json); native_json=NULL;
    launcher_enabled=launcher;launcher_activate=launcher_command_count=0;
    launcher_session++; if(!launcher_session) launcher_session++;
    snprintf(launcher_reply,sizeof(launcher_reply),"{}");
    SDL_UnlockMutex(bridge_mutex);
    SDL_AtomicSet(&stop_requested,0);
    SDL_AtomicSet(&suspend_requested,0);
    applied_suspend=0; suspended_since=suspended_duration=0;
    snprintf(game_path,sizeof(game_path),"%s",pdx);
    snprintf(save_path,sizeof(save_path),"%s",save);
    lockstep=lockstep_requested;
    if(lockstep) {
        if(!step_request) step_request=SDL_CreateSemaphore(0);
        if(!step_done) step_done=SDL_CreateSemaphore(0);
        if(!step_request || !step_done) {lockstep=0;return 0;}
        while(SDL_SemTryWait(step_request)==0) {}
        while(SDL_SemTryWait(step_done)==0) {}
        virtual_us=audio_remainder=0; SDL_AtomicSet(&virtual_ms,0);
    }
#ifdef CRANK_LOCKSTEP_AUDIO
    /* Two buffers of lead keep the frontend from underrunning between steps. */
    SDL_CrankAudioSetGate(lockstep,lockstep ? 2048 : 0);
#endif
    active=1; SDL_AtomicSet(&state,1);
#ifdef __EMSCRIPTEN__
    SDL_AtomicSet(&web_game_done,0);
#endif
#ifdef __SWITCH__
    /* PDGames snapshots alone use 256 KiB of stack; libnx's SDL default is
       smaller. Keep Lua/SDK callbacks on an explicitly sized game stack. */
    game_thread=SDL_CreateThreadWithStackSize(run,"crank-game",1024*1024,NULL);
#else
    game_thread=SDL_CreateThread(run,"crank-game",NULL);
#endif
    if(!game_thread) { pd_host_fail(SDL_GetError()); active=0; return 0; }
    return 1;
#else
    return 0;
#endif
}
int crank_start(const char *pdx,const char *save) {return start_session(pdx,save,0);}
int crank_start_launcher(const char *pdx,const char *save) {return start_session(pdx,save,1);}
void crank_stop(void) {
    if(!game_thread) return;
    SDL_LockMutex(bridge_mutex);
    SDL_AtomicSet(&stop_requested,1);
    SDL_CondBroadcast(suspend_condition);
    SDL_UnlockMutex(bridge_mutex);
    SDL_WaitThread(game_thread,NULL); game_thread=NULL;
    active=0; SDL_AtomicSet(&state,0); lockstep=0;
#ifdef CRANK_LOCKSTEP_AUDIO
    SDL_CrankAudioSetGate(0,0);
#endif
    SDL_LockMutex(bridge_mutex);launcher_enabled=launcher_activate=launcher_command_count=0;SDL_UnlockMutex(bridge_mutex);
}
int crank_status(void) { return SDL_AtomicGet(&state); }
void pd_host_native_info(const char *json) {
    if(!active || !bridge_mutex) return;
    char *copy=strdup(json ? json : "{}");
    if(!copy) return;
    SDL_LockMutex(bridge_mutex); free(native_json); native_json=copy; SDL_UnlockMutex(bridge_mutex);
}
uint32_t crank_copy_native_info(char *destination,uint32_t capacity) {
    if(!bridge_mutex) return 0;
    SDL_LockMutex(bridge_mutex);
    const char *json=native_json ? native_json : "{}";
    uint32_t needed=(uint32_t)strlen(json)+1;
    if(destination && capacity>=needed) memcpy(destination,json,needed);
    SDL_UnlockMutex(bridge_mutex); return needed;
}
void crank_set_suspended(uint32_t reasons) {
    if (!bridge_mutex) return;
    SDL_LockMutex(bridge_mutex);
    reasons &= 3;
    if ((uint32_t)SDL_AtomicGet(&suspend_requested) != reasons) {
        SDL_AtomicSet(&suspend_requested, (int)reasons);
        buttons=0; crank_delta=0; audio_read=audio_count=0;
        SDL_CondBroadcast(suspend_condition);
    }
    SDL_UnlockMutex(bridge_mutex);
}
void pd_host_audio(const int16_t *samples, int count) {
    if (!active || !bridge_mutex || count <= 0) return;
    count &= ~1;
    SDL_LockMutex(bridge_mutex);
    if (SDL_AtomicGet(&suspend_requested)) { SDL_UnlockMutex(bridge_mutex); return; }
    for (int i = 0; i < count; i++) {
        if (audio_count == AUDIO_CAPACITY) {
            audio_read = (audio_read + 2) % AUDIO_CAPACITY;
            audio_count -= 2;
        }
        audio_samples[(audio_read + audio_count) % AUDIO_CAPACITY] = samples[i];
        audio_count++;
    }
    SDL_UnlockMutex(bridge_mutex);
}
uint32_t crank_copy_audio(int16_t *destination, uint32_t capacity) {
    if (!destination || !bridge_mutex) return 0;
    capacity &= ~1u;
    SDL_LockMutex(bridge_mutex);
    uint32_t count = audio_count < capacity ? audio_count : capacity;
    for (uint32_t i = 0; i < count; i++) destination[i] = audio_samples[(audio_read + i) % AUDIO_CAPACITY];
    audio_read = (audio_read + count) % AUDIO_CAPACITY; audio_count -= count;
    SDL_UnlockMutex(bridge_mutex);
    return count;
}
void crank_set_buttons(uint32_t mask) {
    if(!bridge_mutex) return;
    SDL_LockMutex(bridge_mutex); buttons=SDL_AtomicGet(&suspend_requested) ? 0 : mask & 63; SDL_UnlockMutex(bridge_mutex);
}
void crank_add_crank(float delta, int docked) {
    if(!bridge_mutex || !isfinite(delta)) return;
    SDL_LockMutex(bridge_mutex);
    if (!SDL_AtomicGet(&suspend_requested)) { crank_delta+=delta; crank_docked=docked!=0; }
    SDL_UnlockMutex(bridge_mutex);
}
uint32_t crank_copy_frame(uint8_t *destination, uint32_t capacity) {
    if(!bridge_mutex || !destination || capacity<sizeof(pixels)) return 0;
    SDL_LockMutex(bridge_mutex); memcpy(destination,pixels,sizeof(pixels)); uint32_t value=serial; SDL_UnlockMutex(bridge_mutex);
    return value;
}
void crank_last_error(char *destination,uint32_t capacity) {
    if(!destination || !capacity) return;
    destination[0]=0; if(!bridge_mutex) return;
    SDL_LockMutex(bridge_mutex); snprintf(destination,capacity,"%s",last_error); SDL_UnlockMutex(bridge_mutex);
}
void pd_host_menu(const char *json,const uint8_t *image) {
    if(!active || !bridge_mutex || !json) return;
    char *copy=strdup(json); if(!copy) return;
    SDL_LockMutex(bridge_mutex);
    free(menu_json); menu_json=copy; menu_has_image=image!=NULL;
    if(image) memcpy(menu_image,image,sizeof(menu_image));
    menu_image_serial++;
    SDL_UnlockMutex(bridge_mutex);
}
uint32_t crank_copy_menu(char *destination,uint32_t capacity) {
    if(!bridge_mutex) {if(destination && capacity) destination[0]=0; return 0;}
    SDL_LockMutex(bridge_mutex);
    const char *json=menu_json ? menu_json : "{}"; uint32_t needed=(uint32_t)strlen(json)+1;
    if(destination && capacity>=needed) memcpy(destination,json,needed);
    else if(destination && capacity) destination[0]=0;
    SDL_UnlockMutex(bridge_mutex); return needed;
}
uint32_t crank_copy_menu_image(uint8_t *destination,uint32_t capacity) {
    if(!bridge_mutex || !destination || capacity<sizeof(menu_image)) return 0;
    SDL_LockMutex(bridge_mutex);
    uint32_t result=menu_has_image ? menu_image_serial : 0;
    if(result) memcpy(destination,menu_image,sizeof(menu_image));
    SDL_UnlockMutex(bridge_mutex); return result;
}
void crank_set_menu_open(int open) {
    if(!bridge_mutex) return;
    SDL_LockMutex(bridge_mutex); menu_requested=open!=0; SDL_CondBroadcast(suspend_condition); SDL_UnlockMutex(bridge_mutex);
}
int crank_menu_select(uint32_t id,int value) {
    if(!bridge_mutex || !id) return 0;
    SDL_LockMutex(bridge_mutex);
    int accepted=menu_requested && !SDL_AtomicGet(&stop_requested) && SDL_AtomicGet(&state)!=3 && menu_command_count<16;
    if(accepted) {menu_commands[menu_command_count++]=(MenuCommand){id,value}; SDL_CondBroadcast(suspend_condition);}
    SDL_UnlockMutex(bridge_mutex); return accepted;
}
