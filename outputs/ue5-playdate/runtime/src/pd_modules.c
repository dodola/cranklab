#include "pd_runtime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_skip_ws(const char *json, size_t len, int *pos) {
    while (*pos < (int)len && (json[*pos] == ' ' || json[*pos] == '\t' ||
                               json[*pos] == '\n' || json[*pos] == '\r'))
        (*pos)++;
}

static int json_parse_value(lua_State *L, const char *json, size_t len, int *pos);

static void json_parse_string(lua_State *L, const char *json, size_t len, int *pos) {
    (*pos)++;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while (*pos < (int)len && json[*pos] != '"') {
        char c = json[*pos];
        if (c == '\\' && *pos + 1 < (int)len) {
            (*pos)++;
            char e = json[*pos];
            switch (e) {
                case 'n': luaL_addchar(&b, '\n'); break;
                case 'r': luaL_addchar(&b, '\r'); break;
                case 't': luaL_addchar(&b, '\t'); break;
                case 'b': luaL_addchar(&b, '\b'); break;
                case 'f': luaL_addchar(&b, '\f'); break;
                case 'u': {
                    unsigned int cp = 0;
                    if (*pos + 4 < (int)len) {
                        char hex[5] = {json[*pos + 1], json[*pos + 2], json[*pos + 3], json[*pos + 4], 0};
                        cp = (unsigned int)strtoul(hex, NULL, 16);
                        *pos += 4;
                    }
                    if (cp < 0x80) luaL_addchar(&b, (char)cp);
                    else if (cp < 0x800) {
                        luaL_addchar(&b, (char)(0xC0 | (cp >> 6)));
                        luaL_addchar(&b, (char)(0x80 | (cp & 0x3F)));
                    } else {
                        luaL_addchar(&b, (char)(0xE0 | (cp >> 12)));
                        luaL_addchar(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                        luaL_addchar(&b, (char)(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: luaL_addchar(&b, e); break;
            }
        } else {
            luaL_addchar(&b, c);
        }
        (*pos)++;
    }
    if (*pos < (int)len) (*pos)++;
    luaL_pushresult(&b);
}

static int json_parse_value(lua_State *L, const char *json, size_t len, int *pos) {
    json_skip_ws(json, len, pos);
    if (*pos >= (int)len) { lua_pushnil(L); return 0; }
    char c = json[*pos];
    if (c == '{') {
        lua_newtable(L);
        (*pos)++;
        json_skip_ws(json, len, pos);
        if (*pos < (int)len && json[*pos] == '}') { (*pos)++; return 1; }
        while (*pos < (int)len) {
            json_skip_ws(json, len, pos);
            if (*pos >= (int)len || json[*pos] != '"') return 0;
            json_parse_string(L, json, len, pos);
            json_skip_ws(json, len, pos);
            if (*pos >= (int)len || json[*pos] != ':') { lua_pop(L, 1); return 0; }
            (*pos)++;
            if (!json_parse_value(L, json, len, pos)) { lua_pop(L, 2); return 0; }
            lua_rawset(L, -3);
            json_skip_ws(json, len, pos);
            if (*pos < (int)len && json[*pos] == ',') { (*pos)++; continue; }
            if (*pos < (int)len && json[*pos] == '}') { (*pos)++; return 1; }
            return 0;
        }
        return 0;
    } else if (c == '[') {
        lua_newtable(L);
        (*pos)++;
        json_skip_ws(json, len, pos);
        if (*pos < (int)len && json[*pos] == ']') { (*pos)++; return 1; }
        int idx = 1;
        while (*pos < (int)len) {
            if (!json_parse_value(L, json, len, pos)) { lua_pop(L, 1); return 0; }
            lua_rawseti(L, -2, idx++);
            json_skip_ws(json, len, pos);
            if (*pos < (int)len && json[*pos] == ',') { (*pos)++; continue; }
            if (*pos < (int)len && json[*pos] == ']') { (*pos)++; return 1; }
            return 0;
        }
        return 0;
    } else if (c == '"') {
        json_parse_string(L, json, len, pos);
        return 1;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        char *end;
        double val = strtod(&json[*pos], &end);
        int isint = 1;
        for (const char *p = &json[*pos]; p < end; p++) {
            if (*p == '.' || *p == 'e' || *p == 'E') { isint = 0; break; }
        }
        if (isint && val >= -2147483648.0 && val <= 2147483647.0)
            lua_pushinteger(L, (lua_Integer)val);
        else
            lua_pushnumber(L, (lua_Number)val);
        *pos = (int)(end - json);
        return 1;
    } else if (*pos + 4 <= (int)len && strncmp(&json[*pos], "true", 4) == 0) {
        lua_pushboolean(L, 1);
        *pos += 4;
        return 1;
    } else if (*pos + 5 <= (int)len && strncmp(&json[*pos], "false", 5) == 0) {
        lua_pushboolean(L, 0);
        *pos += 5;
        return 1;
    } else if (*pos + 4 <= (int)len && strncmp(&json[*pos], "null", 4) == 0) {
        lua_pushnil(L);
        *pos += 4;
        return 1;
    }
    lua_pushnil(L);
    return 0;
}

static int lua_json_decode(lua_State *L) {
    size_t len;
    const char *json = luaL_checklstring(L, 1, &len);
    int pos = 0;
    lua_settop(L, 1);
    if (!json_parse_value(L, json, len, &pos)) {
        lua_pushnil(L);
        return 1;
    }
    return 1;
}

typedef struct {
    char *data;
    size_t len, cap;
} JsonBuf;

static void jb_grow(JsonBuf *b, size_t need) {
    if (b->len + need <= b->cap) return;
    while (b->cap < b->len + need) b->cap *= 2;
    b->data = realloc(b->data, b->cap);
}

static void jb_addch(JsonBuf *b, char c) {
    jb_grow(b, 1);
    b->data[b->len++] = c;
}

static void jb_adds(JsonBuf *b, const char *s) {
    size_t n = strlen(s);
    jb_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static void jb_add_escaped(JsonBuf *b, const char *s, size_t len) {
    jb_addch(b, '"');
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == 34) jb_adds(b, "\\\"");          /* " */
        else if (c == 92) jb_adds(b, "\\\\");    /* backslash */
        else if (c == 10) jb_adds(b, "\\n");
        else if (c == 13) jb_adds(b, "\\r");
        else if (c == 9) jb_adds(b, "\\t");
        else jb_addch(b, c);
    }
    jb_addch(b, '"');
}

static void json_encode_value(lua_State *L, int idx, JsonBuf *buf) {
    idx = lua_absindex(L, idx);
    char num[48];
    switch (lua_type(L, idx)) {
        case LUA_TNIL:
            jb_adds(buf, "null");
            break;
        case LUA_TBOOLEAN:
            jb_adds(buf, lua_toboolean(L, idx) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx))
                snprintf(num, sizeof(num), "%lld", (long long)lua_tointeger(L, idx));
            else
                snprintf(num, sizeof(num), "%.9g", (double)lua_tonumber(L, idx));
            jb_adds(buf, num);
            break;
        case LUA_TSTRING: {
            size_t len;
            const char *s = lua_tolstring(L, idx, &len);
            jb_add_escaped(buf, s, len);
            break;
        }
        case LUA_TTABLE: {
            int is_array = 1;
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                if (lua_type(L, -2) != LUA_TNUMBER) is_array = 0;
                lua_pop(L, 1);
            }
            if (is_array) {
                lua_Integer n = luaL_len(L, idx);
                jb_addch(buf, '[');
                for (lua_Integer i = 1; i <= n; i++) {
                    if (i > 1) jb_addch(buf, ',');
                    lua_rawgeti(L, idx, i);
                    json_encode_value(L, -1, buf);
                    lua_pop(L, 1);
                }
                jb_addch(buf, ']');
            } else {
                jb_addch(buf, '{');
                int first = 1;
                lua_pushnil(L);
                while (lua_next(L, idx)) {
                    if (!first) jb_addch(buf, ',');
                    first = 0;
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        size_t klen;
                        const char *k = lua_tolstring(L, -2, &klen);
                        jb_add_escaped(buf, k, klen);
                    } else {
                        snprintf(num, sizeof(num), "\"%lld\"",
                                 (long long)lua_tointeger(L, -2));
                        jb_adds(buf, num);
                    }
                    jb_addch(buf, ':');
                    json_encode_value(L, -1, buf);
                    lua_pop(L, 1);
                }
                jb_addch(buf, '}');
            }
            break;
        }
        default:
            jb_adds(buf, "null");
            break;
    }
}

static int lua_json_encode(lua_State *L) {
    JsonBuf buf;
    buf.cap = 256;
    buf.len = 0;
    buf.data = malloc(buf.cap);
    json_encode_value(L, 1, &buf);
    lua_pushlstring(L, buf.data, buf.len);
    free(buf.data);
    return 1;
}

/* Internal serializer: never call a global that a game can replace. */
void pd_json_encode_table(lua_State *L, int index) {
    JsonBuf buf = {0};
    buf.cap = 256; buf.data = malloc(buf.cap);
    json_encode_value(L, index, &buf);
    lua_pushlstring(L, buf.data, buf.len);
    free(buf.data);
}

static int lua_json_encodeLimited(lua_State *L) {
    return lua_json_encode(L);
}

static int lua_json_decodeFile(lua_State *L) {
    FILE *f = NULL;
    int own = 0;
    FILE **ud = luaL_testudata(L, 1, "pd.file");
    if (ud) {
        f = *ud;
    } else {
        const char *path = luaL_checkstring(L, 1);
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
        f = fopen(fullpath, "rb");
        if (!f) {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.save_dir, path);
            f = fopen(fullpath, "rb");
        }
        own = 1;
    }
    if (!f) { lua_pushnil(L); return 1; }
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f) - cur;
    fseek(f, cur, SEEK_SET);
    if (sz < 0) sz = 0;
    char *buf = malloc(sz + 1);
    size_t n = fread(buf, 1, sz, f);
    buf[n] = 0;
    if (own) { fclose(f); }
    lua_pushlstring(L, buf, n);
    free(buf);
    lua_replace(L, 1);
    lua_settop(L, 1);
    return lua_json_decode(L);
}

void pd_json_register(lua_State *L) {
    lua_getglobal(L, "playdate");
    lua_newtable(L);
    lua_pushcfunction(L, lua_json_decode);
    lua_setfield(L, -2, "decode");
    lua_pushcfunction(L, lua_json_encode);
    lua_setfield(L, -2, "encode");
    lua_pushcfunction(L, lua_json_encodeLimited);
    lua_setfield(L, -2, "encodeLimited");
    lua_pushcfunction(L, lua_json_decodeFile);
    lua_setfield(L, -2, "decodeFile");
    lua_setfield(L, -2, "json");
    lua_pop(L, 1);

    // Alias as a global table too (games use json.encode directly)
    lua_getglobal(L, "playdate");
    lua_getfield(L, -1, "json");
    lua_setglobal(L, "json");
    lua_pop(L, 1);
}

#define META_STRING "pd.string"

static int lua_string_trimWhitespace(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    size_t start = 0, end = len;
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) start++;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\n' || s[end-1] == '\r')) end--;
    lua_pushlstring(L, s + start, end - start);
    return 1;
}

static int lua_string_trimTrailingWhitespace(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    size_t end = len;
    while (end > 0 && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\n' || s[end-1] == '\r')) end--;
    lua_pushlstring(L, s, end);
    return 1;
}

static int lua_string_split(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    const char *sep = luaL_checkstring(L, 2);
    lua_newtable(L);
    int idx = 1;
    const char *p = s;
    int sep_len = (int)strlen(sep);
    if (sep_len == 0) {
        lua_pushstring(L, s);
        lua_rawseti(L, -2, 1);
        return 1;
    }
    while (*p) {
        const char *found = strstr(p, sep);
        if (found) {
            lua_pushlstring(L, p, found - p);
            lua_rawseti(L, -2, idx++);
            p = found + sep_len;
        } else {
            lua_pushstring(L, p);
            lua_rawseti(L, -2, idx++);
            break;
        }
    }
    return 1;
}

static int lua_string_join(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char *sep = luaL_optstring(L, 2, "");
    luaL_Buffer buf;
    luaL_buffinit(L, &buf);
    int n = (int)lua_rawlen(L, 1);
    for (int i = 1; i <= n; i++) {
        if (i > 1) luaL_addstring(&buf, sep);
        lua_rawgeti(L, 1, i);
        size_t len;
        const char *s = lua_tolstring(L, -1, &len);
        if (s) luaL_addlstring(&buf, s, len);
        lua_pop(L, 1);
    }
    luaL_pushresult(&buf);
    return 1;
}

static int lua_string_endsWith(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    const char *suffix = luaL_checkstring(L, 2);
    size_t slen = strlen(s), suflen = strlen(suffix);
    if (suflen > slen) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, strcmp(s + slen - suflen, suffix) == 0);
    return 1;
}

static int lua_string_startsWith(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    const char *prefix = luaL_checkstring(L, 2);
    size_t slen = strlen(s), prelen = strlen(prefix);
    if (prelen > slen) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, strncmp(s, prefix, prelen) == 0);
    return 1;
}

void pd_string_register(lua_State *L) {
    lua_getglobal(L, "playdate");
    lua_newtable(L);
    lua_pushcfunction(L, lua_string_trimWhitespace);
    lua_setfield(L, -2, "trimWhitespace");
    lua_pushcfunction(L, lua_string_trimTrailingWhitespace);
    lua_setfield(L, -2, "trimTrailingWhitespace");
    lua_pushcfunction(L, lua_string_split);
    lua_setfield(L, -2, "split");
    lua_pushcfunction(L, lua_string_join);
    lua_setfield(L, -2, "join");
    lua_pushcfunction(L, lua_string_endsWith);
    lua_setfield(L, -2, "endsWith");
    lua_pushcfunction(L, lua_string_startsWith);
    lua_setfield(L, -2, "startsWith");
    lua_setfield(L, -2, "string");
    lua_pop(L, 1);
}

static int lua_math_pow(lua_State *L) {
    lua_pushnumber(L, pow(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

static int lua_math_atan2(lua_State *L) {
    lua_pushnumber(L, atan2(luaL_checknumber(L, 1), luaL_checknumber(L, 2)));
    return 1;
}

void pd_math_register(lua_State *L) {
    lua_getglobal(L, "playdate");
    lua_newtable(L);
    lua_setfield(L, -2, "math");
    lua_pop(L, 1);

    /* Lua 5.4 removed math.pow/math.atan2 but Playdate keeps them */
    lua_getglobal(L, "math");
    lua_pushcfunction(L, lua_math_pow);
    lua_setfield(L, -2, "pow");
    lua_pushcfunction(L, lua_math_atan2);
    lua_setfield(L, -2, "atan2");
    lua_pop(L, 1);
}

static int lua_table_create(lua_State *L) {
    int narr = (int)luaL_optinteger(L, 1, 0);
    int nrec = (int)luaL_optinteger(L, 2, 0);
    lua_createtable(L, narr, nrec);
    return 1;
}

static int lua_table_getsize(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer arr = luaL_len(L, 1);
    lua_Integer hash = 0;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        lua_pop(L, 1);
        if (!(lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 1 && lua_tointeger(L, -1) <= arr))
            hash++;
    }
    lua_pushinteger(L, arr);
    lua_pushinteger(L, hash);
    return 2;
}

static int lua_table_indexOfElement(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_gettop(L) < 2 || lua_isnil(L, 2)) { lua_pushnil(L); return 1; }
    lua_Integer n = luaL_len(L, 1);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_geti(L, 1, i);
        if (lua_compare(L, 2, -1, LUA_OPEQ)) {
            lua_pop(L, 1);
            lua_pushinteger(L, i);
            return 1;
        }
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    return 1;
}

static int lua_table_shallowcopy(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    if (lua_istable(L, 2)) lua_pushvalue(L, 2);
    else lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_settable(L, -4);
    }
    return 1;
}

static void deepcopy_table(lua_State *L, int src) {
    src = lua_absindex(L, src);
    lua_newtable(L);
    int dst = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, src) != 0) {
        lua_pushvalue(L, -2);
        if (lua_istable(L, -2)) {
            deepcopy_table(L, -2);
            lua_remove(L, -3);
        } else {
            lua_insert(L, -2);
        }
        lua_settable(L, dst);
    }
    if (lua_getmetatable(L, src)) lua_setmetatable(L, dst);
}

static int lua_table_deepcopy(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    deepcopy_table(L, 1);
    return 1;
}

void pd_table_register(lua_State *L) {
    lua_getglobal(L, "table");
    lua_pushcfunction(L, lua_table_create);
    lua_setfield(L, -2, "create");
    lua_pushcfunction(L, lua_table_getsize);
    lua_setfield(L, -2, "getsize");
    lua_pushcfunction(L, lua_table_getsize);
    lua_setfield(L, -2, "getSize");
    lua_pushcfunction(L, lua_table_indexOfElement);
    lua_setfield(L, -2, "indexOfElement");
    lua_pushcfunction(L, lua_table_shallowcopy);
    lua_setfield(L, -2, "shallowcopy");
    lua_pushcfunction(L, lua_table_deepcopy);
    lua_setfield(L, -2, "deepcopy");
    lua_pop(L, 1);
}
