#include "pd_runtime.h"
#include "crank_host.h"
#include "pd_pdx.h"
#include "pd_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

PDRuntime g_pd;
static int runtime_failed = 0;

static void init_pd_globals(void) {
    runtime_failed = 0;
    memset(&g_pd, 0, sizeof(g_pd));
    g_pd.refresh_rate = PD_REFRESH_RATE;
    g_pd.clip_enabled = 0;
    g_pd.bg_color = kColorWhite;
    g_pd.draw_mode = kDrawModeCopy;
    g_pd.crank_angle = 0.0f;
    g_pd.crank_change = 0.0f;
    g_pd.crank_docked = 1;
    g_pd.text_tracking = 0;
    g_pd.text_leading = 0;
    g_pd.start_time = pd_time_milliseconds();
    g_pd.running = 1;
}

static void register_all(lua_State *L) {
    lua_newtable(L);
    lua_setglobal(L, "playdate");

    lua_getglobal(L, "playdate");
    lua_newtable(L);
    lua_pushstring(L, g_pd.game_name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, g_pd.game_author);
    lua_setfield(L, -2, "author");
    lua_setfield(L, -2, "metadata");
    lua_pop(L, 1);

    pd_graphics_register(L);
    pd_display_register(L);
    pd_input_register(L);
    pd_system_register(L);
    pd_file_register(L);
    pd_sound_register(L);
    pd_network_register(L);
    pd_geometry_register(L);
    pd_sprite_register(L);
    pd_json_register(L);
    pd_string_register(L);
    pd_math_register(L);
    pd_table_register(L);

    lua_getglobal(L, "playdate");
    lua_newtable(L);
    lua_pushcfunction(L, pd_import);
    lua_setfield(L, -2, "import");
    lua_pop(L, 1);

    pd_setup_imports(L);
    pd_install_api_stubs(L);

    /* playdate.metadata: pdxinfo fields (overwrites the generated stub) */
    lua_getglobal(L, "playdate");
    lua_newtable(L);
    lua_pushstring(L, g_pd.game_name);
    lua_setfield(L, -2, "name");
    lua_pushstring(L, g_pd.game_version);
    lua_setfield(L, -2, "version");
    lua_pushstring(L, g_pd.game_author);
    lua_setfield(L, -2, "author");
    lua_pushstring(L, g_pd.game_bundle_id);
    lua_setfield(L, -2, "bundleID");
    lua_pushstring(L, g_pd.game_build);
    lua_setfield(L, -2, "buildNumber");
    lua_setfield(L, -2, "metadata");
    lua_pop(L, 1);
}

static lua_State *g_lua_state = NULL;

int pd_call_lifecycle(lua_State *L, const char *callback) {
    if (!pd_native_lifecycle(callback)) return 0;
    const int base = lua_gettop(L);
    lua_getglobal(L, "playdate");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, callback);
        if (lua_isfunction(L, -1) && lua_pcall(L, 0, 0, 0) != LUA_OK) {
            luaL_traceback(L, L, lua_tostring(L, -1), 1);
            fprintf(stderr, "[%s] %s\n", callback, lua_tostring(L, -1));
            pd_host_fail(lua_tostring(L, -1));
            lua_settop(L, base);
            return 0;
        }
    }
    lua_settop(L, base);
    return 1;
}

static int pd_msgh(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    luaL_traceback(L, L, msg ? msg : "(non-string error)", 1);
    return 1;
}

void pd_call_input_handlers(int button, int pressed) {
    if (!pd_native_button(button,pressed)) {runtime_failed=1;g_pd.running=0;return;}
    if (!g_lua_state) return;
    lua_State *L = g_lua_state;
    const char *handler = NULL;
    switch (button) {
        case kButtonLeft:  handler = pressed ? "leftButtonDown" : "leftButtonUp"; break;
        case kButtonRight: handler = pressed ? "rightButtonDown" : "rightButtonUp"; break;
        case kButtonUp:    handler = pressed ? "upButtonDown" : "upButtonUp"; break;
        case kButtonDown:  handler = pressed ? "downButtonDown" : "downButtonUp"; break;
        case kButtonA:     handler = pressed ? "AButtonDown" : "AButtonUp"; break;
        case kButtonB:     handler = pressed ? "BButtonDown" : "BButtonUp"; break;
    }
    if (!handler) return;
    if (pd_dispatch_input_handler(L, handler)) return;
    lua_getglobal(L, "playdate");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return; }
    lua_getfield(L, -1, handler);
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "[%s] %s\n", handler, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

/* Dispatch playdate.cranked(change, acceleratedChange), preferring the
   topmost input-handler stack entry like the SDK does. */
static void pd_call_crank_handler(float change) {
    if (!g_lua_state) return;
    lua_State *L = g_lua_state;
    int handled = 0;
    lua_getfield(L, LUA_REGISTRYINDEX, "pd.inputHandlers.stack");
    if (lua_istable(L, -1)) {
        lua_Integer n = luaL_len(L, -1);
        for (lua_Integer i = n; i >= 1 && !handled; i--) {
            lua_rawgeti(L, -1, i);
            if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
            lua_getfield(L, -1, "masksOthers");
            int masks = lua_toboolean(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, -1, "handler");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "cranked");
                if (lua_isfunction(L, -1)) {
                    lua_pushnumber(L, change);
                    lua_pushnumber(L, change);
                    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[cranked] %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                    handled = 1;
                } else {
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 2);
            if (masks) handled = 1;
        }
    }
    lua_pop(L, 1);
    if (handled) return;
    lua_getglobal(L, "playdate");
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "cranked");
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, change);
            lua_pushnumber(L, change);
            if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                fprintf(stderr, "[cranked] %s\n", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static int g_kb_crank_dir = 0;

static void process_input(void) {
    g_pd.buttons_pushed = 0;
    g_pd.buttons_released = 0;
    g_pd.crank_change = 0.0f;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                fprintf(stderr, "[quit] SDL_QUIT ");
                g_pd.running = 0;
                break;
            case SDL_KEYDOWN: {
                int mask = 0;
                switch (event.key.keysym.sym) {
                    case SDLK_LEFT:  mask = kButtonLeft; break;
                    case SDLK_RIGHT: mask = kButtonRight; break;
                    case SDLK_UP:    mask = kButtonUp; break;
                    case SDLK_DOWN:  mask = kButtonDown; break;
                    case SDLK_z:     mask = kButtonA; break;
                    case SDLK_x:     mask = kButtonB; break;
                    case SDLK_a:     mask = kButtonA; break;
                    case SDLK_s:     mask = kButtonB; break;
                    case SDLK_j:     mask = kButtonA; break;
                    case SDLK_k:     mask = kButtonB; break;
                    case SDLK_LEFTBRACKET:  g_kb_crank_dir = -1; break; /* crank ccw */
                    case SDLK_RIGHTBRACKET: g_kb_crank_dir = 1; break;  /* crank cw */
                    case SDLK_q: g_kb_crank_dir = -1; break;
                    case SDLK_e: g_kb_crank_dir = 1; break;
                    case SDLK_ESCAPE: g_pd.running = 0; break;
                    default: break;
                }
                if (mask) {
                    if (!(g_pd.buttons_current & mask)) {
                        g_pd.buttons_pushed |= mask;
                        pd_call_input_handlers(mask, 1);
                    }
                    g_pd.buttons_current |= mask;
                }
                break;
            }
            case SDL_KEYUP: {
                int mask = 0;
                switch (event.key.keysym.sym) {
                    case SDLK_LEFT:  mask = kButtonLeft; break;
                    case SDLK_RIGHT: mask = kButtonRight; break;
                    case SDLK_UP:    mask = kButtonUp; break;
                    case SDLK_DOWN:  mask = kButtonDown; break;
                    case SDLK_z:     mask = kButtonA; break;
                    case SDLK_x:     mask = kButtonB; break;
                    case SDLK_a:     mask = kButtonA; break;
                    case SDLK_s:     mask = kButtonB; break;
                    case SDLK_j:     mask = kButtonA; break;
                    case SDLK_k:     mask = kButtonB; break;
                    case SDLK_LEFTBRACKET:
                    case SDLK_RIGHTBRACKET: g_kb_crank_dir = 0; break;
                    case SDLK_q:
                    case SDLK_e: g_kb_crank_dir = 0; break;
                    default: break;
                }
                if (mask) {
                    g_pd.buttons_released |= mask;
                    g_pd.buttons_current &= ~mask;
                    pd_call_input_handlers(mask, 0);
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN: {
                int mask = 0;
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  mask = kButtonLeft; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: mask = kButtonRight; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:    mask = kButtonUp; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  mask = kButtonDown; break;
                    case SDL_CONTROLLER_BUTTON_A:          mask = kButtonA; break;
                    case SDL_CONTROLLER_BUTTON_B:          mask = kButtonB; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  g_kb_crank_dir = -1; break; /* L1 = crank ccw */
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: g_kb_crank_dir = 1; break;  /* R1 = crank cw */
                    case SDL_CONTROLLER_BUTTON_START:
                        /* Select(Back)+Start = exit (PortMaster convention) */
                        if (SDL_GameControllerGetButton(
                                SDL_GameControllerFromInstanceID(event.cbutton.which),
                                SDL_CONTROLLER_BUTTON_BACK))
                            g_pd.running = 0;
                        break;
                    case SDL_CONTROLLER_BUTTON_BACK:
                        if (SDL_GameControllerGetButton(
                                SDL_GameControllerFromInstanceID(event.cbutton.which),
                                SDL_CONTROLLER_BUTTON_START))
                            g_pd.running = 0;
                        break;
                    default: break;
                }
                if (mask) {
                    if (!(g_pd.buttons_current & mask)) {
                        g_pd.buttons_pushed |= mask;
                        pd_call_input_handlers(mask, 1);
                    }
                    g_pd.buttons_current |= mask;
                }
                break;
            }
            case SDL_CONTROLLERBUTTONUP: {
                int mask = 0;
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  mask = kButtonLeft; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: mask = kButtonRight; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:    mask = kButtonUp; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  mask = kButtonDown; break;
                    case SDL_CONTROLLER_BUTTON_A:          mask = kButtonA; break;
                    case SDL_CONTROLLER_BUTTON_B:          mask = kButtonB; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: g_kb_crank_dir = 0; break;
                    default: break;
                }
                if (mask) {
                    g_pd.buttons_released |= mask;
                    g_pd.buttons_current &= ~mask;
                    pd_call_input_handlers(mask, 0);
                }
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_IsGameController(event.cdevice.which))
                    SDL_GameControllerOpen(event.cdevice.which);
                break;
            default:
                break;
        }
    }

    /* Crank: rotate the left or right analog stick like a dial. The stick's
       angle (atan2) maps directly to the crank angle; deflection past a dead
       zone undocks the crank, centering re-docks it after a short delay. */
    {
        static SDL_GameController *pad = NULL;
        static int centered_frames = 0;
        if (!pad || !SDL_GameControllerGetAttached(pad)) {
            pad = NULL;
            for (int i = 0; i < SDL_NumJoysticks(); i++) {
                if (SDL_IsGameController(i)) {
                    pad = SDL_GameControllerFromInstanceID(
                        SDL_JoystickGetDeviceInstanceID(i));
                    if (pad) break;
                }
            }
        }
        if (pad) {
            float lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
            float ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
            float rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
            float ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
            float mag_l = sqrtf(lx * lx + ly * ly);
            float mag_r = sqrtf(rx * rx + ry * ry);
            /* Use whichever stick has more deflection; right stick takes
               priority if both are pushed (it's the dedicated crank stick). */
            float sx, sy, mag;
            if (mag_r >= mag_l && mag_r > 0.5f) {
                sx = rx; sy = ry; mag = mag_r;
            } else if (mag_l > 0.5f) {
                sx = lx; sy = ly; mag = mag_l;
            } else {
                mag = 0.0f;
            }
            if (mag > 0.5f) {
                /* 0 deg = stick up, clockwise positive (Playdate convention) */
                float angle = atan2f(sx, -sy) * 180.0f / (float)M_PI;
                if (angle < 0) angle += 360.0f;
                float prev = g_pd.crank_angle;
                float change = angle - prev;
                if (change > 180.0f) change -= 360.0f;
                if (change < -180.0f) change += 360.0f;
                if (g_pd.crank_docked) { change = 0.0f; g_pd.crank_docked = 0; }
                g_pd.crank_angle = angle;
                g_pd.crank_change += change;
                centered_frames = 0;
            } else if (!g_pd.crank_docked && ++centered_frames > 90) {
                g_pd.crank_docked = 1; /* idle 3s at center = docked */
            }
        }
    }

    /* keyboard crank: [ and ] rotate 6 degrees per frame while held */
    if (g_kb_crank_dir != 0) {
        float change = (float)g_kb_crank_dir * 6.0f;
        g_pd.crank_angle += change;
        while (g_pd.crank_angle >= 360.0f) g_pd.crank_angle -= 360.0f;
        while (g_pd.crank_angle < 0.0f) g_pd.crank_angle += 360.0f;
        g_pd.crank_change += change;
        g_pd.crank_docked = 0;
    }
}

static void run_game_loop(lua_State *L) {
    lua_State *update_co = NULL;
    int update_co_ref = LUA_NOREF;
    uint32_t frame_start, frame_time;

    int dbg_frame = 0;
    const char *limit_env = getenv("PD_MAX_FRAMES");
    const int max_frames = limit_env ? atoi(limit_env) : 0;
    while (g_pd.running && !pd_host_should_stop()) {
        if (!pd_host_lifecycle(L)) {
            if (crank_status()==3) runtime_failed=1;
            break;
        }
        if (max_frames > 0 && dbg_frame >= max_frames) break;
        frame_start = SDL_GetTicks();

        if (!pd_host_active()) process_input();
        pd_host_input();
        if (pd_network_pump(L)) { runtime_failed = 1; break; }
        if (g_pd.crank_change != 0.0f)
            pd_call_crank_handler(g_pd.crank_change);
        g_sprites_drawn_this_frame = 0;
        pd_frame_draw_ops = 0;
        pd_frame_serial++;
        pd_damage_reset();

        /* PD_SCRIPT="frame:button,frame:button,..." scripted input for
           headless testing (buttons: a b up down left right). Presses are
           held for 3 frames. */
        {
            static struct { int frame; int mask; } script[64];
            static int script_n = -2;
            if (script_n == -2) {
                script_n = 0;
                const char *sc = getenv("PD_SCRIPT");
                if (sc) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "%s", sc);
                    for (char *tok = strtok(buf, ","); tok && script_n < 64;
                         tok = strtok(NULL, ",")) {
                        char name[16] = "";
                        int fr = 0;
                        if (sscanf(tok, "%d:%15s", &fr, name) == 2) {
                            int m = 0;
                            if (!strcmp(name, "a")) m = kButtonA;
                            else if (!strcmp(name, "b")) m = kButtonB;
                            else if (!strcmp(name, "up")) m = kButtonUp;
                            else if (!strcmp(name, "down")) m = kButtonDown;
                            else if (!strcmp(name, "left")) m = kButtonLeft;
                            else if (!strcmp(name, "right")) m = kButtonRight;
                            else if (!strcmp(name, "crank")) m = -1; /* 60 frames of cranking */
                            if (m) { script[script_n].frame = fr; script[script_n].mask = m; script_n++; }
                        }
                    }
                }
            }
            for (int si = 0; si < script_n; si++) {
                int fr = script[si].frame, m = script[si].mask;
                if (m == -1) {
                    if (dbg_frame >= fr && dbg_frame < fr + 60) {
                        g_pd.crank_change += 6.0f;
                        g_pd.crank_angle += 6.0f;
                        while (g_pd.crank_angle >= 360.0f) g_pd.crank_angle -= 360.0f;
                        g_pd.crank_docked = 0;
                        if (dbg_frame == fr + 59) g_pd.crank_docked = 0;
                        pd_call_crank_handler(6.0f);
                    }
                    continue;
                }
                if (dbg_frame == fr) {
                    g_pd.buttons_pushed |= m;
                    g_pd.buttons_current |= m;
                    pd_call_input_handlers(m, 1);
                } else if (dbg_frame == fr + 3 && (g_pd.buttons_current & m)) {
                    g_pd.buttons_released |= m;
                    g_pd.buttons_current &= ~m;
                    pd_call_input_handlers(m, 0);
                }
            }
        }

        {
            static int auto_press = -2;
            if (auto_press == -2) {
                const char *ap = getenv("PD_AUTO_A");
                auto_press = ap ? atoi(ap) : -1;
            }
            if (auto_press > 0 && dbg_frame > 0 && dbg_frame % auto_press == 0) {
                g_pd.buttons_pushed |= kButtonA;
                g_pd.buttons_current |= kButtonA;
                pd_call_input_handlers(kButtonA, 1);
            } else if (auto_press > 0 && dbg_frame % auto_press == 1 && (g_pd.buttons_current & kButtonA)) {
                g_pd.buttons_released |= kButtonA;
                g_pd.buttons_current &= ~kButtonA;
                pd_call_input_handlers(kButtonA, 0);
            }
        }

        lua_getglobal(L, "playdate");
        lua_getfield(L, -1, "update");
        if (dbg_frame % 30 == 0) {
            int has_update = lua_isfunction(L, -1);
            int blacks = 0;
            if (g_pd.display_bitmap) {
                for (int by = 0; by < PD_SCREEN_HEIGHT; by++)
                    for (int bx = 0; bx < PD_SCREEN_WIDTH; bx += 8)
                        if (g_pd.display_bitmap->data[by * g_pd.display_bitmap->rowbytes + bx / 8]) { blacks = 1; goto dbg_done; }
            }
            dbg_done:
            fprintf(stderr, "[dbg] frame=%d update=%d fb=%d ops=%d ", dbg_frame, has_update, blacks, pd_frame_draw_ops);
        }
        dbg_frame++;

        /* Run playdate.update inside a coroutine, like the real firmware:
           games may coroutine.yield() from update (loading screens) and get
           resumed on the next frame. */
        int redraw=1;
        {
            int native_updated=pd_native_update();
            redraw=native_updated!=2;
            if(native_updated<0) {runtime_failed=1;lua_pop(L,2);break;}
            if (!native_updated && !update_co && lua_isfunction(L, -1)) {
                update_co = lua_newthread(L);
                update_co_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                lua_pushvalue(L, -1);        /* the update function */
                lua_xmove(L, update_co, 1);  /* move it into the thread */
            }
            lua_pop(L, 2); /* update function (or non-function) + playdate */

            if (update_co && pd_time_milliseconds() >= g_pd.wait_until) {
                int nres = 0;
                int st = lua_resume(update_co, L, 0, &nres);
                if (st == LUA_YIELD) {
                    lua_pop(update_co, nres); /* resume again next frame */
                } else {
                    if (st != LUA_OK && !pd_host_should_stop()) {
                        runtime_failed = 1;
                        if (getenv("PD_DEBUG_LOCALS")) {
                            lua_Debug ar;
                            for (int level = 0; lua_getstack(update_co, level, &ar); level++) {
                                lua_getinfo(update_co, "Sl", &ar);
                                fprintf(stderr, "[locals] %s:%d\n", ar.short_src, ar.currentline);
                                for (int index=1; ; index++) {
                                    const char *name=lua_getlocal(update_co,&ar,index);
                                    if(!name) break;
                                    fprintf(stderr," %s=%s",name,lua_typename(update_co,lua_type(update_co,-1)));
                                    if(lua_isnumber(update_co,-1)) fprintf(stderr,"(%g)",lua_tonumber(update_co,-1));
                                    if(lua_isstring(update_co,-1) && !lua_isnumber(update_co,-1)) fprintf(stderr,"(%s)",lua_tostring(update_co,-1));
                                    if(lua_istable(update_co,-1)) fprintf(stderr,"(length=%zu)",(size_t)lua_rawlen(update_co,-1));
                                    fprintf(stderr,"\n"); lua_pop(update_co,1);
                                }
                                lua_getinfo(update_co,"f",&ar);
                                for(int index=1; ;index++) {
                                    const char *name=lua_getupvalue(update_co,-1,index);
                                    if(!name) break;
                                    fprintf(stderr," upvalue %s=%s",name,lua_typename(update_co,lua_type(update_co,-1)));
                                    if(lua_isnumber(update_co,-1)) fprintf(stderr,"(%g)",lua_tonumber(update_co,-1));
                                    fprintf(stderr,"\n"); lua_pop(update_co,1);
                                }
                                lua_pop(update_co,1);
                            }
                        }
                        luaL_traceback(L, update_co, lua_tostring(update_co, -1), 0);
                        pd_host_fail(lua_tostring(L, -1));
                        fprintf(stderr, "[update error] %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                    luaL_unref(L, LUA_REGISTRYINDEX, update_co_ref);
                    update_co = NULL;
                    update_co_ref = LUA_NOREF;
                    if (st != LUA_OK) break;
                }
            }
        }

        pd_gfx_reset_focus();
        pd_sprite_native_collect(L);

        // If the game didn't call sprite.update() itself this frame, force a
        // sprite draw so image-based games still render (the damage mask
        // keeps it from painting over the game's own drawing).
        lua_getglobal(L, "playdate");
        if (!g_sprites_drawn_this_frame && lua_istable(L, -1)) {
            lua_getfield(L, -1, "graphics");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "sprite");
                if (lua_istable(L, -1)) {
                    lua_getfield(L, -1, "updateAndDrawSprites");
                    if (lua_isfunction(L, -1)) {
                        extern int pd_sprite_forced_draw;
                        pd_sprite_forced_draw = 1;
                        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                            fprintf(stderr, "[sprites] %s", lua_tostring(L, -1));
                            lua_pop(L, 1);
                        }
                        pd_sprite_forced_draw = 0;
                    } else {
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);

        pd_gfx_reset_focus();
        {
            static int dump_frame = -1;
            if (dump_frame == -1) {
                const char *df = getenv("PD_DUMP_FB");
                dump_frame = df ? atoi(df) : 0;
            }
            if (dump_frame > 0 && dbg_frame == dump_frame && g_pd.display_bitmap) {
                const char *dump_path = getenv("PD_FB_PATH");
                if (!dump_path) dump_path = "pd_fb.pbm";
                FILE *pf = fopen(dump_path, "wb");
                if (pf) {
                    fprintf(pf, "P1\n%d %d\n", PD_SCREEN_WIDTH, PD_SCREEN_HEIGHT);
                    for (int fy = 0; fy < PD_SCREEN_HEIGHT; fy++) {
                        for (int fx = 0; fx < PD_SCREEN_WIDTH; fx++) {
                            int byte_i = fy * g_pd.display_bitmap->rowbytes + fx / 8;
                            int v = (g_pd.display_bitmap->data[byte_i] >> (7 - fx % 8)) & 1;
                            fputc(v ? '1' : '0', pf);
                        }
                        fputc('\n', pf);
                    }
                    fclose(pf);
                    fprintf(stderr, "[dump] wrote %s at frame %d\n", dump_path, dbg_frame);
                }
            }
        }
        pd_native_publish();
        if(redraw) {pd_native_present();pd_host_publish();pd_present_frame();}
        if (redraw && g_pd.display_bitmap && g_pd.display_bitmap->texture) {
            SDL_RenderClear(g_pd.renderer);
            int scale = g_pd.display_scale;
            if (scale == 2 || scale == 4 || scale == 8) {
                SDL_Rect src = {0, 0, PD_SCREEN_WIDTH / scale, PD_SCREEN_HEIGHT / scale};
                SDL_RenderCopy(g_pd.renderer, g_pd.display_bitmap->texture, &src, NULL);
            } else {
                SDL_RenderCopy(g_pd.renderer, g_pd.display_bitmap->texture, NULL, NULL);
            }
            SDL_RenderPresent(g_pd.renderer);
        }

        uint64_t now = SDL_GetTicks64();
        uint32_t frame_time = (uint32_t)(now - frame_start);
        uint32_t frame_delay=g_pd.refresh_rate>0 ? (uint32_t)(1000.0f/g_pd.refresh_rate) : 0;
        if (pd_host_lockstep()) {
            if (!pd_host_step_wait(L)) {
                if (crank_status()==3) runtime_failed=1;
                break;
            }
        } else if (!getenv("PD_FAST") && frame_time < frame_delay)
            SDL_Delay(frame_delay - frame_time);
    }
}

static void copy_corelibs(const char *corelibs_source, const char *corelibs_dest) {
    char mkdir_cmd[2048];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "%s", corelibs_dest);
    mkdir(corelibs_dest, 0755);
    DIR *d = opendir(corelibs_source);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char srcpath[1024], dstpath[1024];
        snprintf(srcpath, sizeof(srcpath), "%s/%s", corelibs_source, entry->d_name);
        snprintf(dstpath, sizeof(dstpath), "%s/%s", corelibs_dest, entry->d_name);
        struct stat st;
        if (stat(srcpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                copy_corelibs(srcpath, dstpath);
            } else {
                FILE *src = fopen(srcpath, "rb");
                FILE *dst = fopen(dstpath, "wb");
                if (src && dst) {
                    char buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                        fwrite(buf, 1, n, dst);
                }
                if (src) fclose(src);
                if (dst) fclose(dst);
            }
        }
    }
    closedir(d);
}

static void print_usage(void) {
    fprintf(stderr, "Usage: playdate_runtime [options] <game.pdx | game_dir>\n");
    fprintf(stderr, "  If given a .pdx.zip, extract it first.\n");
    fprintf(stderr, "  If given a directory, it should contain pdxinfo and the game\n");
    fprintf(stderr, "  files, or a roms/ folder of .pdx games (selection menu).\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --scale N          window size = 400x240 * N (default 2)\n");
    fprintf(stderr, "  --integer-scale    letterbox output to an integer multiple\n");
    fprintf(stderr, "  --fullscreen       fullscreen window\n");
}

#ifdef PD_LIBRARY
int crank_game_entry(int argc, char *argv[]) {
#else
int main(int argc, char *argv[]) {
#endif
    const char *game_arg = NULL;
    int win_scale = 2;
    int integer_scale = 0;
    int fullscreen = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--scale") == 0 || strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) win_scale = atoi(argv[++i]);
            if (win_scale < 1) win_scale = 1;
            if (win_scale > 8) win_scale = 8;
        } else if (strcmp(argv[i], "--integer-scale") == 0 || strcmp(argv[i], "-i") == 0) {
            integer_scale = 1;
        } else if (strcmp(argv[i], "--fullscreen") == 0 || strcmp(argv[i], "-f") == 0) {
            fullscreen = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage();
            return 1;
        } else {
            game_arg = argv[i];
        }
    }
    if (getenv("PD_INTEGER_SCALE")) integer_scale = 1;
    if (!game_arg) {
        print_usage();
        return 1;
    }

    init_pd_globals();
    char pdx_dir[1024];
    char save_dir[1024];

    struct stat st;
    if (stat(game_arg, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncpy(pdx_dir, game_arg, sizeof(pdx_dir) - 1);
    } else {
        fprintf(stderr, "Error: %s is not a directory. Extract the .pdx.zip first.\n", game_arg);
        return 1;
    }

    g_pd.pdx_dir = strdup(pdx_dir);

    /* Save location priority: PD_SAVE_DIR, then XDG_DATA_HOME (PortMaster
       launcher points this at the port's conf/ dir so saves live on the SD
       card), then ~/.playdate_runtime_saves. */
    const char *env_save = getenv("PD_SAVE_DIR");
    if (pd_host_save_path()) env_save = pd_host_save_path();
    const char *xdg = getenv("XDG_DATA_HOME");
    if (env_save && env_save[0]) {
        snprintf(save_dir, sizeof(save_dir), "%s", env_save);
    } else if (xdg && xdg[0]) {
        snprintf(save_dir, sizeof(save_dir), "%s/playdate_runtime_saves", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) home = "/tmp";
        snprintf(save_dir, sizeof(save_dir), "%s/.playdate_runtime_saves", home);
    }
    mkdir(save_dir, 0755);
    g_pd.save_dir = strdup(save_dir);

    PDXInfo info;
    if (pdx_read_info(pdx_dir, &info) == 0) {
        strncpy(g_pd.game_name, info.game_name, sizeof(g_pd.game_name) - 1);
        strncpy(g_pd.game_author, info.author, sizeof(g_pd.game_author) - 1);
        strncpy(g_pd.game_bundle_id, info.bundle_id, sizeof(g_pd.game_bundle_id) - 1);
        strncpy(g_pd.game_build, info.build_number, sizeof(g_pd.game_build) - 1);
        strncpy(g_pd.game_version, info.version, sizeof(g_pd.game_version) - 1);
        fprintf(stderr, "[playdate] Game: %s v%s by %s\n",
                g_pd.game_name, g_pd.game_version, g_pd.game_author);
    }

    SDL_SetMainReady();
#ifndef CRANK_EXTERNAL_SDL
    if (pd_host_active()) SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
#define PD_SDL_INIT SDL_Init
#else
    /* A native host owns its window/controllers and shares SDL with the game.
       Mixer uses the real device; stopping a game must keep the host alive. */
#define PD_SDL_INIT SDL_InitSubSystem
#endif
    if (PD_SDL_INIT(pd_host_active() ? SDL_INIT_TIMER | SDL_INIT_AUDIO :
                 SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (!pd_host_active()) {
    /* Open all connected game controllers so we get native gamepad input
       without needing gptokeyb (hotplug handled in process_input) */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            if (SDL_GameControllerOpen(i))
                fprintf(stderr, "[input] controller: %s\n", SDL_GameControllerNameForIndex(i));
        }
    }

    int window_w = PD_SCREEN_WIDTH * win_scale;
    int window_h = PD_SCREEN_HEIGHT * win_scale;

    g_pd.window = SDL_CreateWindow(
        g_pd.game_name[0] ? g_pd.game_name : "Playdate Runtime",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_w, window_h,
        ((pd_host_active() || getenv("PD_HEADLESS")) ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN) | SDL_WINDOW_RESIZABLE |
            (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)
    );
    if (!g_pd.window) {
        fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    g_pd.renderer = SDL_CreateRenderer(g_pd.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_pd.renderer) {
        g_pd.renderer = SDL_CreateRenderer(g_pd.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_pd.renderer) {
        fprintf(stderr, "Renderer creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetRenderDrawColor(g_pd.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_pd.renderer);
    }

    g_pd.display_bitmap = calloc(1, sizeof(LCDBitmap));
    pd_screen_bitmap = g_pd.display_bitmap;
    g_pd.display_bitmap->width = PD_SCREEN_WIDTH;
    g_pd.display_bitmap->height = PD_SCREEN_HEIGHT;
    g_pd.display_bitmap->rowbytes = (PD_SCREEN_WIDTH + 7) / 8;
    g_pd.display_bitmap->data = calloc(g_pd.display_bitmap->rowbytes * PD_SCREEN_HEIGHT, 1);
    g_pd.display_bitmap->mask = malloc(g_pd.display_bitmap->rowbytes * PD_SCREEN_HEIGHT);
    memset(g_pd.display_bitmap->mask, 0xFF, g_pd.display_bitmap->rowbytes * PD_SCREEN_HEIGHT);
    if (g_pd.renderer) g_pd.display_bitmap->texture = SDL_CreateTexture(g_pd.renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        PD_SCREEN_WIDTH, PD_SCREEN_HEIGHT);

    if (g_pd.renderer) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(g_pd.renderer, PD_SCREEN_WIDTH, PD_SCREEN_HEIGHT);
    if (integer_scale)
        SDL_RenderSetIntegerScale(g_pd.renderer, SDL_TRUE);
    }

    /* ROM browser mode: <dir>/roms/*.pdx — pick one, then run it */
    {
        char roms_dir[1200];
        snprintf(roms_dir, sizeof(roms_dir), "%s/roms", pdx_dir);
        struct stat rst;
        if (stat(roms_dir, &rst) == 0 && S_ISDIR(rst.st_mode)) {
            char chosen[1024];
            int r = pd_rom_menu(pdx_dir, chosen, sizeof(chosen));
            if (r == 1) return 0; /* user quit */
            if (r < 0) {
                fprintf(stderr, "[playdate] no games found in %s\n", roms_dir);
                return 1;
            }
            free(g_pd.pdx_dir);
            g_pd.pdx_dir = strdup(chosen);
            strncpy(pdx_dir, chosen, sizeof(pdx_dir) - 1);
            if (pdx_read_info(pdx_dir, &info) == 0) {
                strncpy(g_pd.game_name, info.game_name, sizeof(g_pd.game_name) - 1);
                strncpy(g_pd.game_author, info.author, sizeof(g_pd.game_author) - 1);
                strncpy(g_pd.game_bundle_id, info.bundle_id, sizeof(g_pd.game_bundle_id) - 1);
                strncpy(g_pd.game_version, info.version, sizeof(g_pd.game_version) - 1);
                strncpy(g_pd.game_build, info.build_number, sizeof(g_pd.game_build) - 1);
                fprintf(stderr, "[playdate] Game: %s v%s by %s\n",
                        g_pd.game_name, g_pd.game_version, g_pd.game_author);
                SDL_SetWindowTitle(g_pd.window, g_pd.game_name);
            }
        }
    }

    /* Per-game save directory so games don't clobber each other's data.
     * Uses the bundle id when available, else the game directory name. */
    {
        char gamekey[256];
        if (g_pd.game_bundle_id[0]) {
            snprintf(gamekey, sizeof(gamekey), "%s", g_pd.game_bundle_id);
        } else {
            const char *base = strrchr(pdx_dir, '/');
            snprintf(gamekey, sizeof(gamekey), "%s", base ? base + 1 : pdx_dir);
        }
        for (char *p = gamekey; *p; p++)
            if (*p == '/' || *p == ' ') *p = '_';
        if (gamekey[0]) {
            char newsave[1200];
            snprintf(newsave, sizeof(newsave), "%s/%s", g_pd.save_dir, gamekey);
            mkdir(newsave, 0755);
            free(g_pd.save_dir);
            g_pd.save_dir = strdup(newsave);
        }
    }

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    g_lua_state = L;
    g_pd.L = L;

    register_all(L);

    char corelibs_src[1024];
    snprintf(corelibs_src, sizeof(corelibs_src), "%s/CoreLibs", g_pd.pdx_dir);
    if (access(corelibs_src, F_OK) == 0) {
        // CoreLibs already in game directory
    } else if (access("CoreLibs", F_OK) == 0 && strcmp(g_pd.pdx_dir, ".") != 0) {
        // Shared ./corelibs exists; import() falls back to it, no copy needed
    }

    char main_path[1024];
    snprintf(main_path, sizeof(main_path), "%s/main.pdz", g_pd.pdx_dir);
    int have_pdz = 0;
    if (access(main_path, F_OK) == 0) {
        g_pd.pdz = (struct PDZFile *)pdz_load(main_path);
        if (g_pd.pdz) have_pdz = 1;
    }

    /* PD_EVAL_PRE: run arbitrary Lua before main loads (debugging aid) */
    {
        const char *ev = getenv("PD_EVAL_PRE");
        if (ev && luaL_dostring(L, ev) != LUA_OK) {
            fprintf(stderr, "[eval-pre error] %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    int native_status=pd_native_load(L,g_pd.pdx_dir);
    if(native_status<0) runtime_failed=1;
    int status=LUA_OK;
    int base = lua_gettop(L);
    lua_pushcfunction(L, pd_msgh);
    if(runtime_failed || pd_native_has_update()) {
        /* A native update callback selects the pure-C path, like the SDK. */
    } else if (have_pdz) {
        PDZFile *pdz = (PDZFile *)g_pd.pdz;
        PDZEntry *entry = pdz_find(pdz, "main");
        if (!entry) entry = pdz_find(pdz, "main.luac");
        if (!entry) entry = pdz_find(pdz, "Main");
        if (!entry) {
            fprintf(stderr, "[playdate] main.pdz has no main entry\n");
            pd_host_fail("main.pdz has no main entry");runtime_failed=1;
        } else {
            fprintf(stderr, "[playdate] Loading %s (entry: %s)\n", main_path, entry->filename);
            status = luaL_loadbuffer(L, (const char *)entry->data, entry->size, "@main");
            if (status == LUA_OK) status = lua_pcall(L, 0, 0, base + 1);
        }
    } else {
        snprintf(main_path, sizeof(main_path), "%s/main.luac", g_pd.pdx_dir);
        if (access(main_path, F_OK) != 0) {
            snprintf(main_path, sizeof(main_path), "%s/main.lua", g_pd.pdx_dir);
        }
        fprintf(stderr, "[playdate] Loading %s\n", main_path);
        status = luaL_loadfile(L, main_path);
        if (status == LUA_OK) status = lua_pcall(L, 0, 0, base + 1);
    }
    if (status != LUA_OK) {
        runtime_failed = 1;
        const char *err = lua_tostring(L, -1);
        pd_host_fail(err);
        fprintf(stderr, "[playdate] Error loading main: %s\n", err);
        lua_pop(L, 1);
    }
    lua_remove(L, base + 1);

    if (!runtime_failed && !pd_host_launcher_install(L)) runtime_failed = 1;

    /* PD_EVAL: run arbitrary Lua after main loads (debugging aid) */
    {
        const char *ev = getenv("PD_EVAL");
        if (ev && luaL_dostring(L, ev) != LUA_OK) {
            fprintf(stderr, "[eval error] %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }

    fprintf(stderr, "[playdate] entering game loop, running=%d ", g_pd.running);
    if (!runtime_failed) run_game_loop(L);
    fprintf(stderr, "[playdate] game loop exited ");

    if (!runtime_failed && !pd_host_finish_menu(L)) runtime_failed=1;
    if (!runtime_failed && !pd_call_lifecycle(L, "gameWillTerminate")) runtime_failed=1;

    pd_gfx_reset_focus();
    pd_network_shutdown(L);
    LCDBitmap *screen = pd_screen_bitmap;
    pd_native_shutdown();
    lua_close(L); g_lua_state = NULL; g_pd.L = NULL;
    pd_sprite_shutdown();
    pd_sound_shutdown();
    SDL_DestroyTexture(screen->texture);
    free(screen->data); free(screen->mask); free(screen);
    pd_graphics_shutdown();
    pdz_free((PDZFile*)g_pd.pdz); g_pd.pdz=NULL;
    free(g_pd.pdx_dir); free(g_pd.save_dir); g_pd.pdx_dir=g_pd.save_dir=NULL;
    SDL_DestroyRenderer(g_pd.renderer);
    SDL_DestroyWindow(g_pd.window);
#ifdef CRANK_EXTERNAL_SDL
    SDL_QuitSubSystem(SDL_INIT_TIMER | SDL_INIT_AUDIO);
#else
    SDL_Quit();
#endif

    return runtime_failed ? 2 : 0;
}
