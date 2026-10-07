#include "pd_runtime.h"
#include "pd_http_backend.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define HTTP_META "pd.http"
typedef struct Http {
    char *server;
    int port, ssl, timeout_ms;
    int callbacks[4], self_ref;
    SDL_Thread *worker;
    SDL_atomic_t ready;
    PDHttpRequest request;
    PDHttpResponse response;
    size_t cursor;
    int canceled;
    struct Http *next;
} Http;
static Http *pending = NULL;
static int enabled = 1;
static Http *check(lua_State *L) { return luaL_checkudata(L, 1, HTTP_META); }
static int worker(void *arg) {
    Http *h = arg;
    pd_http_fetch(&h->request, &h->response);
    SDL_AtomicSet(&h->ready, 1);
    return 0;
}
static void free_request(Http *h) {
    free((void *)h->request.path); free((void *)h->request.method);
    free((void *)h->request.headers); free((void *)h->request.body);
    memset(&h->request, 0, sizeof(h->request));
}
static int callback(lua_State *L, Http *h, int index) {
    if (h->callbacks[index] == LUA_NOREF) return 0;
    lua_rawgeti(L, LUA_REGISTRYINDEX, h->callbacks[index]);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "[network callback error] %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    return 0;
}
int pd_network_pump(lua_State *L) {
    int failed = 0;
    /* Detach each completed request before calling Lua; callbacks can queue another. */
    Http **link = &pending;
    while (*link) {
        Http *h = *link;
        if (!SDL_AtomicGet(&h->ready)) { link = &h->next; continue; }
        *link = h->next;
        SDL_WaitThread(h->worker, NULL); h->worker = NULL;
        int ref = h->self_ref;
        h->self_ref = LUA_NOREF;
        free_request(h);
        if (!h->canceled) {
            fprintf(stderr, "[http response] %s status=%d bytes=%zu error=%s\n",
                    h->server, h->response.status, h->response.size, h->response.error);
            if (h->response.status) failed |= callback(L, h, 0);
            while (!h->canceled && !h->worker && h->cursor < h->response.size) {
                size_t before = h->cursor;
                failed |= callback(L, h, 1);
                if (h->cursor <= before) break;
            }
            if (!h->canceled && !h->worker) failed |= callback(L, h, 2);
            if (!h->canceled && !h->worker) failed |= callback(L, h, 3);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
    return failed;
}
void pd_network_shutdown(lua_State *L) {
    while (pending) {
        Http *h = pending; pending = h->next;
        SDL_WaitThread(h->worker, NULL); h->worker = NULL;
        free_request(h);
        luaL_unref(L, LUA_REGISTRYINDEX, h->self_ref);
        h->self_ref = LUA_NOREF;
    }
}
static int create(lua_State *L) {
    const char *server = luaL_checkstring(L, 1);
    int ssl = lua_toboolean(L, 3);
    int port = (int)luaL_optinteger(L, 2, ssl ? 443 : 80);
    Http *h = lua_newuserdatauv(L, sizeof(Http), 0);
    memset(h, 0, sizeof(*h));
    h->server = strdup(server);
    h->ssl = ssl;
    h->port = port;
    h->timeout_ms = 10000; h->self_ref = LUA_NOREF;
    for (int i=0; i<4; i++) h->callbacks[i] = LUA_NOREF;
    luaL_setmetatable(L, HTTP_META);
    return 1;
}
static char *request_headers(lua_State *L, int index) {
    if (lua_type(L, index) == LUA_TSTRING) return strdup(lua_tostring(L, index));
    luaL_Buffer b; luaL_buffinit(L, &b);
    if (lua_istable(L, index)) {
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                luaL_addstring(&b, lua_tostring(L, -2)); luaL_addstring(&b, ": ");
            }
            if (lua_type(L, -1) == LUA_TSTRING) luaL_addstring(&b, lua_tostring(L, -1));
            luaL_addstring(&b, "\r\n"); lua_pop(L, 1);
        }
    }
    luaL_pushresult(&b);
    char *headers = strdup(lua_tostring(L, -1)); lua_pop(L, 1);
    return headers;
}
static int get(lua_State *L) {
    Http *h = check(L);
    if (h->worker || !enabled) {
        lua_pushboolean(L, 0); lua_pushstring(L, h->worker ? "Request already active" : "Network disabled"); return 2;
    }
    free(h->response.body); free(h->response.headers);
    memset(&h->response, 0, sizeof(h->response)); h->cursor = 0; h->canceled = 0;
    h->request.server = h->server; h->request.port = h->port; h->request.ssl = h->ssl;
    h->request.timeout_ms = h->timeout_ms;
    h->request.path = strdup(luaL_checkstring(L, 2)); h->request.method = strdup("GET");
    h->request.headers = request_headers(L, 3);
    fprintf(stderr, "[http request] GET %s://%s:%d%s\n", h->ssl ? "https" : "http", h->server, h->port, h->request.path);
    SDL_AtomicSet(&h->ready, 0);
    lua_pushvalue(L, 1); h->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    h->worker = SDL_CreateThread(worker, "pd-http", h);
    if (!h->worker) {
        free_request(h); luaL_unref(L, LUA_REGISTRYINDEX, h->self_ref); h->self_ref = LUA_NOREF;
        lua_pushboolean(L, 0); lua_pushstring(L, SDL_GetError()); return 2;
    }
    h->next = pending; pending = h;
    lua_pushboolean(L, 1); return 1;
}
static int close_http(lua_State *L) { check(L)->canceled = 1; return 0; }
static int gc(lua_State *L) {
    Http *h = check(L);
    free(h->server); free(h->response.body); free(h->response.headers);
    for(int i=0;i<4;i++) luaL_unref(L,LUA_REGISTRYINDEX,h->callbacks[i]);
    return 0;
}
static int error(lua_State *L) { Http *h=check(L); if(h->response.error[0]) lua_pushstring(L,h->response.error); else lua_pushnil(L); return 1; }
static int status(lua_State *L) { Http *h=check(L); lua_pushinteger(L,h->worker ? 0 : h->response.status); return 1; }
static int available(lua_State *L) { Http *h=check(L); lua_pushinteger(L,h->worker ? 0 : h->response.size-h->cursor); return 1; }
static int progress(lua_State *L) { Http *h=check(L); lua_pushinteger(L,h->worker ? 0 : h->response.size); lua_pushinteger(L,h->worker ? 0 : h->response.size); return 2; }
static int read_http(lua_State *L) {
    Http *h=check(L);
    size_t available = h->worker ? 0 : h->response.size-h->cursor;
    lua_Integer wanted=luaL_optinteger(L,2,65536);
    size_t count=wanted<0 ? 0 : (size_t)wanted;
    if(count>65536) count=65536; if(count>available) count=available;
    lua_pushlstring(L,count ? (const char *)h->response.body+h->cursor : "",count);
    h->cursor+=count; lua_pushinteger(L,count); return 2;
}
static int headers(lua_State *L) {
    Http *h=check(L); if(h->worker || !h->response.headers) { lua_pushnil(L); return 1; }
    lua_newtable(L); char *copy=strdup(h->response.headers), *save=NULL;
    for(char *line=strtok_r(copy,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)) {
        char *colon=strchr(line,':'); if(!colon) continue; *colon=0;
        char *value=colon+1; while(*value==' ') value++;
        lua_pushstring(L,value); lua_setfield(L,-2,line);
    }
    free(copy); return 1;
}
static int set_callback(lua_State *L,int i) {
    Http *h=check(L); luaL_unref(L,LUA_REGISTRYINDEX,h->callbacks[i]);
    if(lua_isnoneornil(L,2)) h->callbacks[i]=LUA_NOREF;
    else { luaL_checktype(L,2,LUA_TFUNCTION); lua_pushvalue(L,2); h->callbacks[i]=luaL_ref(L,LUA_REGISTRYINDEX); }
    return 0;
}
static int cb_headers(lua_State *L) { return set_callback(L,0); }
static int cb_data(lua_State *L) { return set_callback(L,1); }
static int cb_complete(lua_State *L) { return set_callback(L,2); }
static int cb_closed(lua_State *L) { return set_callback(L,3); }
static int timeout(lua_State *L) { check(L)->timeout_ms=(int)(luaL_checknumber(L,2)*1000); return 0; }
static int unsupported(lua_State *L) { return luaL_error(L,"HTTP option not implemented in desktop bootstrap"); }
static int keepalive(lua_State *L) { (void)check(L); return 0; }
static int grant(lua_State *L) { lua_pushboolean(L,1); return 1; }
static int net_status(lua_State *L) { lua_pushinteger(L,enabled ? 1 : 0); return 1; }
static int set_enabled(lua_State *L) {
    enabled=lua_toboolean(L,1);
    if(lua_isfunction(L,2)) { lua_pushvalue(L,2); lua_pushnil(L); lua_call(L,1,0); }
    return 0;
}
void pd_network_register(lua_State *L) {
    enabled=1;
    const luaL_Reg methods[]={
        {"get",get},{"close",close_http},{"getError",error},{"getProgress",progress},
        {"getBytesAvailable",available},{"getResponseStatus",status},{"getResponseHeaders",headers},
        {"read",read_http},{"setHeadersReadCallback",cb_headers},{"setRequestCallback",cb_data},
        {"setRequestCompleteCallback",cb_complete},{"setConnectionClosedCallback",cb_closed},
        {"setConnectTimeout",timeout},{"setReadTimeout",timeout},{"setKeepAlive",keepalive},
        {"setReadBufferSize",keepalive},{"setByteRange",unsupported},{"post",unsupported},{"__gc",gc},{NULL,NULL}};
    luaL_newmetatable(L,HTTP_META); luaL_setfuncs(L,methods,0);
    lua_pushvalue(L,-1); lua_setfield(L,-2,"__index"); lua_pop(L,1);
    lua_getglobal(L,"playdate"); lua_newtable(L);
    lua_pushcfunction(L,set_enabled); lua_setfield(L,-2,"setEnabled");
    lua_pushcfunction(L,net_status); lua_setfield(L,-2,"getStatus");
    lua_pushinteger(L,0); lua_setfield(L,-2,"kStatusNotConnected");
    lua_pushinteger(L,1); lua_setfield(L,-2,"kStatusConnected");
    lua_pushinteger(L,2); lua_setfield(L,-2,"kStatusNotAvailable");
    lua_newtable(L); lua_pushcfunction(L,create); lua_setfield(L,-2,"new");
    lua_pushcfunction(L,grant); lua_setfield(L,-2,"requestAccess");
    lua_setfield(L,-2,"http"); lua_setfield(L,-2,"network"); lua_pop(L,1);
}
