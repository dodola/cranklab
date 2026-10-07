#include "pd_runtime.h"

static int display_scale(void) {
    int s = g_pd.display_scale;
    return (s == 2 || s == 4 || s == 8) ? s : 1;
}

static int lua_display_getWidth(lua_State *L) {
    lua_pushinteger(L, PD_SCREEN_WIDTH / display_scale());
    return 1;
}

static int lua_display_getHeight(lua_State *L) {
    lua_pushinteger(L, PD_SCREEN_HEIGHT / display_scale());
    return 1;
}

static int lua_display_getSize(lua_State *L) {
    lua_pushinteger(L, PD_SCREEN_WIDTH / display_scale());
    lua_pushinteger(L, PD_SCREEN_HEIGHT / display_scale());
    return 2;
}

static int lua_display_getInverted(lua_State *L) {
    lua_pushboolean(L, g_pd.inverted);
    return 1;
}

static int lua_display_getRect(lua_State *L) {
    lua_newtable(L);
    lua_pushinteger(L, 0); lua_setfield(L, -2, "x");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "y");
    lua_pushinteger(L, PD_SCREEN_WIDTH / display_scale()); lua_setfield(L, -2, "width");
    lua_pushinteger(L, PD_SCREEN_HEIGHT / display_scale()); lua_setfield(L, -2, "height");
    return 1;
}

static int lua_display_setRefreshRate(lua_State *L) {
    g_pd.refresh_rate = (float)luaL_checknumber(L, 1);
    return 0;
}

static int lua_display_setInverted(lua_State *L) {
    g_pd.inverted = lua_toboolean(L, 1);
    return 0;
}

static int lua_display_setScale(lua_State *L) {
    g_pd.display_scale = (int)luaL_checkinteger(L, 1);
    return 0;
}

static int lua_display_getScale(lua_State *L) {
    lua_pushinteger(L, display_scale());
    return 1;
}

static int lua_display_setMosaic(lua_State *L) {
    (void)luaL_checkinteger(L, 1);
    (void)luaL_checkinteger(L, 2);
    return 0;
}

static int lua_display_setFlipped(lua_State *L) {
    (void)luaL_checkinteger(L, 1);
    (void)luaL_checkinteger(L, 2);
    return 0;
}

static float g_display_off_x = 0, g_display_off_y = 0;

static int lua_display_setOffset(lua_State *L) {
    g_display_off_x = (float)luaL_checknumber(L, 1);
    g_display_off_y = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_display_getOffset(lua_State *L) {
    lua_pushnumber(L, g_display_off_x);
    lua_pushnumber(L, g_display_off_y);
    return 2;
}

static int lua_display_getRefreshRate(lua_State *L) {
    lua_pushnumber(L, g_pd.refresh_rate);
    return 1;
}

static int lua_display_getFPS(lua_State *L) {
    lua_pushnumber(L, g_pd.refresh_rate);
    return 1;
}

static int lua_display_flush(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_display_getMosaic(lua_State *L) {
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return 2;
}

static const luaL_Reg display_funcs[] = {
    {"getWidth", lua_display_getWidth},
    {"getHeight", lua_display_getHeight},
    {"getSize", lua_display_getSize},
    {"getRect", lua_display_getRect},
    {"getInverted", lua_display_getInverted},
    {"setRefreshRate", lua_display_setRefreshRate},
    {"setInverted", lua_display_setInverted},
    {"setScale", lua_display_setScale},
    {"getScale", lua_display_getScale},
    {"setMosaic", lua_display_setMosaic},
    {"setFlipped", lua_display_setFlipped},
    {"setOffset", lua_display_setOffset},
    {"getOffset", lua_display_getOffset},
    {"getRefreshRate", lua_display_getRefreshRate},
    {"getFPS", lua_display_getFPS},
    {"flush", lua_display_flush},
    {"getMosaic", lua_display_getMosaic},
    {NULL, NULL}
};

void pd_display_register(lua_State *L) {
    g_display_off_x=g_display_off_y=0;
    lua_getglobal(L, "playdate");
    lua_newtable(L);
    luaL_setfuncs(L, display_funcs, 0);
    lua_setfield(L, -2, "display");
    lua_pop(L, 1);
}
