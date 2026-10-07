#include "pd_runtime.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int lua_getButtonState(lua_State *L) {
    lua_pushinteger(L, g_pd.buttons_current);
    lua_pushinteger(L, g_pd.buttons_pushed);
    lua_pushinteger(L, g_pd.buttons_released);
    return 3;
}

/* SDK also accepts button names ("a", "b", "up", ...) wherever masks go */
static int pd_button_mask(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char *s = lua_tostring(L, idx);
        if (strcasecmp(s, "a") == 0) return kButtonA;
        if (strcasecmp(s, "b") == 0) return kButtonB;
        if (strcasecmp(s, "up") == 0) return kButtonUp;
        if (strcasecmp(s, "down") == 0) return kButtonDown;
        if (strcasecmp(s, "left") == 0) return kButtonLeft;
        if (strcasecmp(s, "right") == 0) return kButtonRight;
        return 0;
    }
    return (int)luaL_checkinteger(L, idx);
}

static int lua_buttonJustPressed(lua_State *L) {
    int mask = pd_button_mask(L, 1);
    lua_pushboolean(L, (g_pd.buttons_pushed & mask) != 0);
    return 1;
}

static int lua_buttonJustReleased(lua_State *L) {
    int mask = pd_button_mask(L, 1);
    lua_pushboolean(L, (g_pd.buttons_released & mask) != 0);
    return 1;
}

static int lua_buttonIsPressed(lua_State *L) {
    int mask = pd_button_mask(L, 1);
    lua_pushboolean(L, (g_pd.buttons_current & mask) != 0);
    return 1;
}

/* SDK returns (change, acceleratedChange) */
static int lua_getCrankChange(lua_State *L) {
    lua_pushnumber(L, g_pd.crank_change);
    lua_pushnumber(L, g_pd.crank_change);
    return 2;
}

static int lua_getCrankPosition(lua_State *L) {
    lua_pushnumber(L, g_pd.crank_angle);
    return 1;
}

/* getCrankTicks(ticksPerRotation): number of tick boundaries crossed since
   the last call (CoreLibs/crank equivalent, needed when games don't import
   it). */
static float g_crank_tick_position = -1.0f;
static int lua_getCrankTicks(lua_State *L) {
    int per = (int)luaL_optinteger(L, 1, 1);
    if (per < 1) per = 1;
    float last_pos = g_crank_tick_position;
    float pos = g_pd.crank_angle;
    if (last_pos < 0) { g_crank_tick_position = pos; lua_pushinteger(L, 0); return 1; }
    float seg = 360.0f / (float)per;
    int prev_tick = (int)(last_pos / seg);
    int cur_tick = (int)(pos / seg);
    int ticks = cur_tick - prev_tick;
    /* handle wrap-around */
    float delta = pos - last_pos;
    if (delta > 180.0f) ticks -= per;
    else if (delta < -180.0f) ticks += per;
    g_crank_tick_position = pos;
    lua_pushinteger(L, ticks);
    return 1;
}

static int lua_isCrankDocked(lua_State *L) {
    lua_pushboolean(L, g_pd.crank_docked);
    return 1;
}

static int lua_setCrankSoundsDisabled(lua_State *L) {
    if (lua_isboolean(L, 1))
        lua_toboolean(L, 1);
    else
        (void)luaL_checknumber(L, 1);
    lua_pushboolean(L, 0);
    return 1;
}

#define PD_INPUT_STACK_KEY "pd.inputHandlers.stack"

static int lua_inputHandlers_push(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int masks = lua_toboolean(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, PD_INPUT_STACK_KEY);
    lua_Integer n = luaL_len(L, -1);
    lua_newtable(L);
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "handler");
    lua_pushboolean(L, masks);
    lua_setfield(L, -2, "masksOthers");
    lua_rawseti(L, -2, n + 1);
    lua_pop(L, 1);
    return 0;
}

static int lua_inputHandlers_pop(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, PD_INPUT_STACK_KEY);
    lua_Integer n = luaL_len(L, -1);
    if (n > 0) {
        lua_pushnil(L);
        lua_rawseti(L, -2, n);
    }
    lua_pop(L, 1);
    return 0;
}

int pd_dispatch_input_handler(lua_State *L, const char *handler) {
    lua_getfield(L, LUA_REGISTRYINDEX, PD_INPUT_STACK_KEY);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 0; }
    lua_Integer n = luaL_len(L, -1);
    int handled = 0;
    for (lua_Integer i = n; i >= 1 && !handled; i--) {
        lua_rawgeti(L, -1, i);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); continue; }
        lua_getfield(L, -1, "masksOthers");
        int masks = lua_toboolean(L, -1);
        lua_pop(L, 1);
        lua_getfield(L, -1, "handler");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, handler);
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[%s] %s\n", handler, lua_tostring(L, -1));
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
    lua_pop(L, 1);
    return handled;
}

static int g_accel_running = 0;

static int lua_startAccelerometer(lua_State *L) {
    (void)L;
    if (getenv("PD_TRACE")) fprintf(stderr, "[accel START]");
    g_accel_running = 1;
    return 0;
}

static int lua_stopAccelerometer(lua_State *L) {
    (void)L;
    if (getenv("PD_TRACE")) fprintf(stderr, "[accel STOP]");
    g_accel_running = 0;
    return 0;
}

static int lua_accelerometerIsRunning(lua_State *L) {
    if (getenv("PD_TRACE")) fprintf(stderr, "[accel? %d]", g_accel_running);
    lua_pushboolean(L, g_accel_running);
    return 1;
}

static int lua_readAccelerometer(lua_State *L) {
    float x = 0.0f, y = 0.0f, z = 1.0f;
    static const char *tilt = NULL;
    static int tilt_checked = 0;
    if (!tilt_checked) { tilt = getenv("PD_TILT"); tilt_checked = 1; }
    if (tilt) {
        static int reads = 0;
        reads++;
        if (reads > 400) sscanf(tilt, "%f,%f", &x, &y);
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushnumber(L, z);
        return 3;
    }
    if (g_pd.buttons_current & kButtonLeft) x = -0.5f;
    if (g_pd.buttons_current & kButtonRight) x = 0.5f;
    if (g_pd.buttons_current & kButtonUp) y = -0.5f;
    if (g_pd.buttons_current & kButtonDown) y = 0.5f;
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

void pd_input_register(lua_State *L) {
    g_crank_tick_position=-1; g_accel_running=0;
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, PD_INPUT_STACK_KEY);

    lua_getglobal(L, "playdate");

    lua_pushcfunction(L, lua_getButtonState);
    lua_setfield(L, -2, "getButtonState");

    lua_pushcfunction(L, lua_getCrankChange);
    lua_setfield(L, -2, "getCrankChange");

    lua_pushcfunction(L, lua_getCrankPosition);
    lua_setfield(L, -2, "getCrankPosition");

    lua_pushcfunction(L, lua_getCrankTicks);
    lua_setfield(L, -2, "getCrankTicks");

    lua_pushcfunction(L, lua_isCrankDocked);
    lua_setfield(L, -2, "isCrankDocked");

    lua_pushcfunction(L, lua_setCrankSoundsDisabled);
    lua_setfield(L, -2, "setCrankSoundsDisabled");

    lua_pushcfunction(L, lua_startAccelerometer);
    lua_setfield(L, -2, "startAccelerometer");
    lua_pushcfunction(L, lua_stopAccelerometer);
    lua_setfield(L, -2, "stopAccelerometer");
    lua_pushcfunction(L, lua_accelerometerIsRunning);
    lua_setfield(L, -2, "accelerometerIsRunning");
    lua_pushcfunction(L, lua_readAccelerometer);
    lua_setfield(L, -2, "readAccelerometer");
    lua_pushcfunction(L, lua_buttonIsPressed);
    lua_setfield(L, -2, "buttonIsPressed");
    lua_pushcfunction(L, lua_buttonJustPressed);
    lua_setfield(L, -2, "buttonJustPressed");
    lua_pushcfunction(L, lua_buttonJustReleased);
    lua_setfield(L, -2, "buttonJustReleased");

    lua_pushinteger(L, kButtonLeft);
    lua_setfield(L, -2, "kButtonLeft");
    lua_pushinteger(L, kButtonRight);
    lua_setfield(L, -2, "kButtonRight");
    lua_pushinteger(L, kButtonUp);
    lua_setfield(L, -2, "kButtonUp");
    lua_pushinteger(L, kButtonDown);
    lua_setfield(L, -2, "kButtonDown");
    lua_pushinteger(L, kButtonA);
    lua_setfield(L, -2, "kButtonA");
    lua_pushinteger(L, kButtonB);
    lua_setfield(L, -2, "kButtonB");

    lua_pushcfunction(L, lua_buttonIsPressed);
    lua_setfield(L, -2, "buttonIsPressed");

    lua_newtable(L);
    lua_pushcfunction(L, lua_buttonIsPressed);
    lua_setfield(L, -2, "isPressed");
    lua_setfield(L, -2, "button");

    lua_newtable(L);
    lua_pushcfunction(L, lua_inputHandlers_push);
    lua_setfield(L, -2, "push");
    lua_pushcfunction(L, lua_inputHandlers_pop);
    lua_setfield(L, -2, "pop");
    lua_setfield(L, -2, "inputHandlers");

    lua_pop(L, 1);
}
