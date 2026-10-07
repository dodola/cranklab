#include "pd_geometry.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define META_POINT   "pd.point"
#define META_RECT    "pd.rect"
#define META_SIZE    "pd.size"
#define META_POLYGON "pd.polygon"
#define META_TRANSFORM "pd.transform"

static int lua_point_new(lua_State *L) {
    float x = (float)luaL_optnumber(L, 1, 0);
    float y = (float)luaL_optnumber(L, 2, 0);
    PDPoint *p = lua_newuserdata(L, sizeof(PDPoint));
    p->x = x; p->y = y;
    luaL_getmetatable(L, META_POINT);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_point_index(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "x") == 0) { lua_pushnumber(L, p->x); return 1; }
    if (strcmp(key, "y") == 0) { lua_pushnumber(L, p->y); return 1; }
    if (strcmp(key, "dx") == 0) { lua_pushnumber(L, p->x); return 1; }
    if (strcmp(key, "dy") == 0) { lua_pushnumber(L, p->y); return 1; }
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, key);
    return 1;
}

static int lua_point_newindex(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "x") == 0 || strcmp(key, "dx") == 0) { p->x = (float)luaL_checknumber(L, 3); return 0; }
    if (strcmp(key, "y") == 0 || strcmp(key, "dy") == 0) { p->y = (float)luaL_checknumber(L, 3); return 0; }
    return 0;
}

static int lua_point_unpack(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    lua_pushnumber(L, p->x);
    lua_pushnumber(L, p->y);
    return 2;
}

static int lua_point_toString(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    lua_pushfstring(L, "point(%f, %f)", p->x, p->y);
    return 1;
}

static int lua_rect_new(lua_State *L) {
    float x = (float)luaL_optnumber(L, 1, 0);
    float y = (float)luaL_optnumber(L, 2, 0);
    float w = (float)luaL_optnumber(L, 3, 0);
    float h = (float)luaL_optnumber(L, 4, 0);
    PDRectInternal *r = lua_newuserdata(L, sizeof(PDRectInternal));
    r->x = x; r->y = y; r->width = w; r->height = h;
    luaL_getmetatable(L, META_RECT);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_rect_fast_intersection(lua_State *L) {
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float w1 = (float)luaL_checknumber(L, 3);
    float h1 = (float)luaL_checknumber(L, 4);
    float x2 = (float)luaL_checknumber(L, 5);
    float y2 = (float)luaL_checknumber(L, 6);
    float w2 = (float)luaL_checknumber(L, 7);
    float h2 = (float)luaL_checknumber(L, 8);
    float left = x1 > x2 ? x1 : x2;
    float top = y1 > y2 ? y1 : y2;
    float right = (x1 + w1) < (x2 + w2) ? (x1 + w1) : (x2 + w2);
    float bottom = (y1 + h1) < (y2 + h2) ? (y1 + h1) : (y2 + h2);
    if (right <= left || bottom <= top) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 4;
    }
    lua_pushnumber(L, left);
    lua_pushnumber(L, top);
    lua_pushnumber(L, right - left);
    lua_pushnumber(L, bottom - top);
    return 4;
}

static int lua_rect_fast_union(lua_State *L) {
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float w1 = (float)luaL_checknumber(L, 3);
    float h1 = (float)luaL_checknumber(L, 4);
    float x2 = (float)luaL_checknumber(L, 5);
    float y2 = (float)luaL_checknumber(L, 6);
    float w2 = (float)luaL_checknumber(L, 7);
    float h2 = (float)luaL_checknumber(L, 8);
    float left = x1 < x2 ? x1 : x2;
    float top = y1 < y2 ? y1 : y2;
    float right = (x1 + w1) > (x2 + w2) ? (x1 + w1) : (x2 + w2);
    float bottom = (y1 + h1) > (y2 + h2) ? (y1 + h1) : (y2 + h2);
    lua_pushnumber(L, left);
    lua_pushnumber(L, top);
    lua_pushnumber(L, right - left);
    lua_pushnumber(L, bottom - top);
    return 4;
}

static int lua_rect_index(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "x") == 0) { lua_pushnumber(L, r->x); return 1; }
    if (strcmp(key, "y") == 0) { lua_pushnumber(L, r->y); return 1; }
    if (strcmp(key, "width") == 0 || strcmp(key, "w") == 0) { lua_pushnumber(L, r->width); return 1; }
    if (strcmp(key, "height") == 0 || strcmp(key, "h") == 0) { lua_pushnumber(L, r->height); return 1; }
    if (strcmp(key, "left") == 0) { lua_pushnumber(L, r->x); return 1; }
    if (strcmp(key, "right") == 0) { lua_pushnumber(L, r->x + r->width); return 1; }
    if (strcmp(key, "top") == 0) { lua_pushnumber(L, r->y); return 1; }
    if (strcmp(key, "bottom") == 0) { lua_pushnumber(L, r->y + r->height); return 1; }
    if (strcmp(key, "centerX") == 0) { lua_pushnumber(L, r->x + r->width/2); return 1; }
    if (strcmp(key, "centerY") == 0) { lua_pushnumber(L, r->y + r->height/2); return 1; }
    if (strcmp(key, "origin") == 0) {
        PDPoint *p = lua_newuserdata(L, sizeof(PDPoint));
        p->x = r->x; p->y = r->y;
        luaL_getmetatable(L, META_POINT);
        lua_setmetatable(L, -2);
        return 1;
    }
    if (strcmp(key, "size") == 0) {
        PDSize *s = lua_newuserdata(L, sizeof(PDSize));
        s->width = r->width; s->height = r->height;
        luaL_getmetatable(L, META_SIZE);
        lua_setmetatable(L, -2);
        return 1;
    }
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, key);
    return 1;
}

static int lua_rect_newindex(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    const char *key = luaL_checkstring(L, 2);
    float v = (float)luaL_checknumber(L, 3);
    if (strcmp(key, "x") == 0) { r->x = v; return 0; }
    if (strcmp(key, "y") == 0) { r->y = v; return 0; }
    if (strcmp(key, "width") == 0 || strcmp(key, "w") == 0) { r->width = v; return 0; }
    if (strcmp(key, "height") == 0 || strcmp(key, "h") == 0) { r->height = v; return 0; }
    return 0;
}

static int lua_rect_toString(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    lua_pushfstring(L, "rect(%f, %f, %f, %f)", r->x, r->y, r->width, r->height);
    return 1;
}

static void push_rect(lua_State *L, float x, float y, float w, float h) {
    PDRectInternal *out = lua_newuserdata(L, sizeof(PDRectInternal));
    out->x = x; out->y = y; out->width = w; out->height = h;
    luaL_getmetatable(L, META_RECT);
    lua_setmetatable(L, -2);
}

/* insetBy(dx, dy) -> new rect shrunk on all sides */
static int lua_rect_insetBy(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    float dx = (float)luaL_checknumber(L, 2);
    float dy = (float)luaL_optnumber(L, 3, dx);
    push_rect(L, r->x + dx, r->y + dy, r->width - 2 * dx, r->height - 2 * dy);
    return 1;
}

/* inset(dx, dy): mutate in place */
static int lua_rect_inset(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    float dx = (float)luaL_checknumber(L, 2);
    float dy = (float)luaL_optnumber(L, 3, dx);
    r->x += dx; r->y += dy;
    r->width -= 2 * dx; r->height -= 2 * dy;
    return 0;
}

static void rect_intersection(PDRectInternal *a, PDRectInternal *b,
                              float *x, float *y, float *w, float *h) {
    float x1 = a->x > b->x ? a->x : b->x;
    float y1 = a->y > b->y ? a->y : b->y;
    float x2 = (a->x + a->width) < (b->x + b->width) ? a->x + a->width : b->x + b->width;
    float y2 = (a->y + a->height) < (b->y + b->height) ? a->y + a->height : b->y + b->height;
    if (x2 <= x1 || y2 <= y1) { *x = 0; *y = 0; *w = 0; *h = 0; return; }
    *x = x1; *y = y1; *w = x2 - x1; *h = y2 - y1;
}

static int lua_rect_intersection2(lua_State *L) {
    PDRectInternal *a = luaL_checkudata(L, 1, META_RECT);
    PDRectInternal *b = luaL_checkudata(L, 2, META_RECT);
    float x, y, w, h;
    rect_intersection(a, b, &x, &y, &w, &h);
    push_rect(L, x, y, w, h);
    return 1;
}

static int lua_rect_union2(lua_State *L) {
    PDRectInternal *a = luaL_checkudata(L, 1, META_RECT);
    PDRectInternal *b = luaL_checkudata(L, 2, META_RECT);
    float x1 = a->x < b->x ? a->x : b->x;
    float y1 = a->y < b->y ? a->y : b->y;
    float x2 = (a->x + a->width) > (b->x + b->width) ? a->x + a->width : b->x + b->width;
    float y2 = (a->y + a->height) > (b->y + b->height) ? a->y + a->height : b->y + b->height;
    push_rect(L, x1, y1, x2 - x1, y2 - y1);
    return 1;
}

static int lua_rect_isEmpty(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    lua_pushboolean(L, r->width <= 0 || r->height <= 0);
    return 1;
}

static int lua_rect_isEqual(lua_State *L) {
    PDRectInternal *a = luaL_checkudata(L, 1, META_RECT);
    PDRectInternal *b = luaL_checkudata(L, 2, META_RECT);
    lua_pushboolean(L, a->x == b->x && a->y == b->y &&
                    a->width == b->width && a->height == b->height);
    return 1;
}

static int lua_rect_centerPoint(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    PDPoint *p = lua_newuserdata(L, sizeof(PDPoint));
    p->x = r->x + r->width / 2;
    p->y = r->y + r->height / 2;
    luaL_getmetatable(L, META_POINT);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_rect_containsRect(lua_State *L) {
    PDRectInternal *a = luaL_checkudata(L, 1, META_RECT);
    PDRectInternal *b = luaL_checkudata(L, 2, META_RECT);
    lua_pushboolean(L, b->x >= a->x && b->y >= a->y &&
                    b->x + b->width <= a->x + a->width &&
                    b->y + b->height <= a->y + a->height);
    return 1;
}

static int lua_rect_isCollapsed(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    lua_pushboolean(L, r->width <= 0 || r->height <= 0);
    return 1;
}

static int lua_rect_toPolygon(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    Polygon *poly = lua_newuserdata(L, sizeof(Polygon));
    poly->count = 10;
    poly->coords = malloc(10 * sizeof(float));
    poly->coords[0] = r->x;                  poly->coords[1] = r->y;
    poly->coords[2] = r->x + r->width;       poly->coords[3] = r->y;
    poly->coords[4] = r->x + r->width;       poly->coords[5] = r->y + r->height;
    poly->coords[6] = r->x;                  poly->coords[7] = r->y + r->height;
    poly->coords[8] = r->x;                  poly->coords[9] = r->y;
    luaL_getmetatable(L, META_POLYGON);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_rect_encapsulate(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    float px = (float)luaL_checknumber(L, 2);
    float py = (float)luaL_checknumber(L, 3);
    float right = r->x + r->width, bottom = r->y + r->height;
    if (px < r->x) r->x = px;
    if (py < r->y) r->y = py;
    if (px > right) right = px;
    if (py > bottom) bottom = py;
    r->width = right - r->x;
    r->height = bottom - r->y;
    return 0;
}

static int lua_rect_offsetBy(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    float dx = (float)luaL_checknumber(L, 2);
    float dy = (float)luaL_checknumber(L, 3);
    PDRectInternal *out = lua_newuserdata(L, sizeof(PDRectInternal));
    out->x = r->x + dx;
    out->y = r->y + dy;
    out->width = r->width;
    out->height = r->height;
    luaL_getmetatable(L, META_RECT);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_rect_offset(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    r->x += (float)luaL_checknumber(L, 2);
    r->y += (float)luaL_checknumber(L, 3);
    return 0;
}

static int lua_rect_unpack(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    lua_pushnumber(L, r->x);
    lua_pushnumber(L, r->y);
    lua_pushnumber(L, r->width);
    lua_pushnumber(L, r->height);
    return 4;
}

static int lua_rect_containsPoint(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    float px, py;
    PDPoint *p = luaL_testudata(L, 2, META_POINT);
    if (p) { px = p->x; py = p->y; }
    else { px = (float)luaL_checknumber(L, 2); py = (float)luaL_checknumber(L, 3); }
    lua_pushboolean(L, px >= r->x && px < r->x + r->width && py >= r->y && py < r->y + r->height);
    return 1;
}

static int lua_rect_intersects(lua_State *L) {
    PDRectInternal *a = luaL_checkudata(L, 1, META_RECT);
    PDRectInternal *b = luaL_checkudata(L, 2, META_RECT);
    lua_pushboolean(L, a->x < b->x + b->width && a->x + a->width > b->x &&
                       a->y < b->y + b->height && a->y + a->height > b->y);
    return 1;
}

static int lua_rect_copy(lua_State *L) {
    PDRectInternal *r = luaL_checkudata(L, 1, META_RECT);
    PDRectInternal *out = lua_newuserdata(L, sizeof(PDRectInternal));
    *out = *r;
    luaL_getmetatable(L, META_RECT);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_size_new(lua_State *L) {
    float w = (float)luaL_optnumber(L, 1, 0);
    float h = (float)luaL_optnumber(L, 2, 0);
    PDSize *s = lua_newuserdata(L, sizeof(PDSize));
    s->width = w; s->height = h;
    luaL_getmetatable(L, META_SIZE);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_size_index(lua_State *L) {
    PDSize *s = luaL_checkudata(L, 1, META_SIZE);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "width") == 0) { lua_pushnumber(L, s->width); return 1; }
    if (strcmp(key, "height") == 0) { lua_pushnumber(L, s->height); return 1; }
    if (strcmp(key, "w") == 0) { lua_pushnumber(L, s->width); return 1; }
    if (strcmp(key, "h") == 0) { lua_pushnumber(L, s->height); return 1; }
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, key);
    return 1;
}

static int lua_size_newindex(lua_State *L) {
    PDSize *s = luaL_checkudata(L, 1, META_SIZE);
    const char *key = luaL_checkstring(L, 2);
    float v = (float)luaL_checknumber(L, 3);
    if (strcmp(key, "width") == 0 || strcmp(key, "w") == 0) { s->width = v; return 0; }
    if (strcmp(key, "height") == 0 || strcmp(key, "h") == 0) { s->height = v; return 0; }
    return 0;
}

static int lua_polygon_new(lua_State *L) {
    int nargs = lua_gettop(L);
    int count = 0;
    float *coords = NULL;

    if (nargs == 1 && lua_istable(L, 1)) {
        count = (int)lua_rawlen(L, 1);
        coords = malloc(count * sizeof(float));
        for (int i = 0; i < count; i++) {
            lua_rawgeti(L, 1, i + 1);
            coords[i] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    } else if (nargs == 1 && lua_isuserdata(L, 1)) {
        Polygon *src = luaL_checkudata(L, 1, META_POLYGON);
        count = src->count;
        coords = malloc(count * sizeof(float));
        memcpy(coords, src->coords, count * sizeof(float));
    } else if (nargs == 2 && lua_isuserdata(L, 1) && lua_isuserdata(L, 2)) {
        PDPoint *p1 = luaL_checkudata(L, 1, META_POINT);
        PDPoint *p2 = luaL_checkudata(L, 2, META_POINT);
        count = 4;
        coords = malloc(4 * sizeof(float));
        coords[0] = p1->x; coords[1] = p1->y;
        coords[2] = p2->x; coords[3] = p2->y;
    } else {
        int cap = nargs;
        coords = malloc(cap * sizeof(float));
        for (int i = 1; i <= nargs; i++) {
            if (lua_isnumber(L, i)) {
                coords[count++] = (float)lua_tonumber(L, i);
            } else if (lua_isuserdata(L, i)) {
                PDPoint *p = luaL_testudata(L, i, META_POINT);
                if (p) {
                    if (count + 2 > cap) { cap += 2; coords = realloc(coords, cap * sizeof(float)); }
                    coords[count++] = p->x;
                    coords[count++] = p->y;
                }
            }
        }
    }

    Polygon *poly = lua_newuserdata(L, sizeof(Polygon));
    poly->count = count;
    poly->coords = coords;
    luaL_getmetatable(L, META_POLYGON);
    lua_setmetatable(L, -2);
    return 1;
}

static int l_noop(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    free(p->coords);
    p->coords = NULL;
    p->count = 0;
    return 0;
}

static int lua_polygon_index(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "npoints") == 0) {
        lua_pushinteger(L, p->count / 2);
        return 1;
    }
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, key);
    return 1;
}

static int lua_polygon_getBounds(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    if (p->count < 2) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 4; }
    float minx = p->coords[0], maxx = p->coords[0];
    float miny = p->coords[1], maxy = p->coords[1];
    for (int i = 2; i < p->count; i += 2) {
        if (p->coords[i] < minx) minx = p->coords[i];
        if (p->coords[i] > maxx) maxx = p->coords[i];
        if (p->coords[i+1] < miny) miny = p->coords[i+1];
        if (p->coords[i+1] > maxy) maxy = p->coords[i+1];
    }
    lua_pushnumber(L, minx);
    lua_pushnumber(L, miny);
    lua_pushnumber(L, maxx - minx);
    lua_pushnumber(L, maxy - miny);
    return 4;
}

static int lua_polygon_toString(lua_State *L) {
    lua_pushfstring(L, "polygon(%d points)", ((Polygon *)luaL_checkudata(L, 1, META_POLYGON))->count / 2);
    return 1;
}

static int lua_polygon_close(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    if (p->count >= 4 && (p->coords[0] != p->coords[p->count - 2] ||
                          p->coords[1] != p->coords[p->count - 1])) {
        p->coords = realloc(p->coords, (size_t)(p->count + 2) * sizeof(float));
        p->coords[p->count] = p->coords[0];
        p->coords[p->count + 1] = p->coords[1];
        p->count += 2;
    }
    return 0;
}

static int lua_polygon_isClosed(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    if (p->count < 4) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, p->coords[0] == p->coords[p->count - 2] &&
                        p->coords[1] == p->coords[p->count - 1]);
    return 1;
}

static int lua_polygon_setPointAt(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    int idx = (int)luaL_checkinteger(L, 2);
    float x = (float)luaL_checknumber(L, 3);
    float y = (float)luaL_checknumber(L, 4);
    if (idx < 0 || idx >= p->count / 2) return 0;
    p->coords[idx * 2] = x;
    p->coords[idx * 2 + 1] = y;
    return 0;
}

static int lua_polygon_getPointAt(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    int idx = (int)luaL_checkinteger(L, 2);
    if (idx < 0 || idx >= p->count / 2) { lua_pushnil(L); return 1; }
    pd_push_point(L, p->coords[idx * 2], p->coords[idx * 2 + 1]);
    return 1;
}

static int lua_polygon_count(lua_State *L) {
    Polygon *p = luaL_checkudata(L, 1, META_POLYGON);
    lua_pushinteger(L, p->count / 2);
    return 1;
}

static void transform_apply(AffineTransform *t, float x, float y, float *ox, float *oy);

/* polygon * affineTransform (either operand order) -> transformed copy;
   polygon * number -> uniformly scaled copy */
static int lua_polygon_mul(lua_State *L) {
    Polygon *p = luaL_testudata(L, 1, META_POLYGON);
    int other = 2;
    if (!p) { p = luaL_checkudata(L, 2, META_POLYGON); other = 1; }
    Polygon *out = lua_newuserdata(L, sizeof(Polygon));
    out->count = p->count;
    out->coords = malloc((size_t)p->count * sizeof(float));
    AffineTransform *t = luaL_testudata(L, other, META_TRANSFORM);
    if (t) {
        for (int i = 0; i + 1 < p->count; i += 2)
            transform_apply(t, p->coords[i], p->coords[i + 1],
                            &out->coords[i], &out->coords[i + 1]);
    } else {
        float s = (float)luaL_checknumber(L, other);
        for (int i = 0; i < p->count; i++)
            out->coords[i] = p->coords[i] * s;
    }
    luaL_getmetatable(L, META_POLYGON);
    lua_setmetatable(L, -2);
    return 1;
}

static void transform_apply(AffineTransform *t, float x, float y, float *ox, float *oy) {
    *ox = t->m[0] * x + t->m[2] * y + t->m[4];
    *oy = t->m[1] * x + t->m[3] * y + t->m[5];
}

static int lua_transform_new(lua_State *L) {
    AffineTransform *t = lua_newuserdata(L, sizeof(AffineTransform));
    t->m[0] = 1; t->m[1] = 0;
    t->m[2] = 0; t->m[3] = 1;
    t->m[4] = 0; t->m[5] = 0;
    luaL_getmetatable(L, META_TRANSFORM);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_transform_identity(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    t->m[0] = 1; t->m[1] = 0; t->m[2] = 0; t->m[3] = 1; t->m[4] = 0; t->m[5] = 0;
    return 0;
}

static int lua_transform_translate(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    float dx = (float)luaL_checknumber(L, 2);
    float dy = (float)luaL_checknumber(L, 3);
    t->m[4] += t->m[0] * dx + t->m[2] * dy;
    t->m[5] += t->m[1] * dx + t->m[3] * dy;
    lua_pushvalue(L, 1);
    return 1;
}

static int lua_transform_rotate(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    float angle = (float)luaL_checknumber(L, 2);
    float rad = angle * (float)M_PI / 180.0f;
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);
    float m0 = t->m[0], m1 = t->m[1], m2 = t->m[2], m3 = t->m[3];
    t->m[0] = cos_a * m0 + sin_a * m2;
    t->m[1] = cos_a * m1 + sin_a * m3;
    t->m[2] = -sin_a * m0 + cos_a * m2;
    t->m[3] = -sin_a * m1 + cos_a * m3;
    if (lua_gettop(L) >= 4) {
        float cx = (float)luaL_checknumber(L, 3);
        float cy = (float)luaL_checknumber(L, 4);
        t->m[4] += cx - (t->m[0] * cx + t->m[2] * cy);
        t->m[5] += cy - (t->m[1] * cx + t->m[3] * cy);
    }
    lua_pushvalue(L, 1);
    return 1;
}

static int lua_transform_scale(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    float sx = (float)luaL_checknumber(L, 2);
    float sy = (float)luaL_optnumber(L, 3, sx);
    t->m[0] *= sx; t->m[1] *= sx;
    t->m[2] *= sy; t->m[3] *= sy;
    lua_pushvalue(L, 1);
    return 1;
}

static int lua_transform_multiply(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    AffineTransform *o = luaL_checkudata(L, 2, META_TRANSFORM);
    AffineTransform result;
    result.m[0] = o->m[0] * t->m[0] + o->m[1] * t->m[2];
    result.m[1] = o->m[0] * t->m[1] + o->m[1] * t->m[3];
    result.m[2] = o->m[2] * t->m[0] + o->m[3] * t->m[2];
    result.m[3] = o->m[2] * t->m[1] + o->m[3] * t->m[3];
    result.m[4] = o->m[4] * t->m[0] + o->m[5] * t->m[2] + t->m[4];
    result.m[5] = o->m[4] * t->m[1] + o->m[5] * t->m[3] + t->m[5];
    *t = result;
    lua_pushvalue(L, 1);
    return 1;
}

static int lua_transform_transformXY(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float ox, oy;
    transform_apply(t, x, y, &ox, &oy);
    lua_pushnumber(L, ox);
    lua_pushnumber(L, oy);
    return 2;
}

static int lua_transform_applyToPoint(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    PDPoint *p = luaL_checkudata(L, 2, META_POINT);
    float ox, oy;
    transform_apply(t, p->x, p->y, &ox, &oy);
    p->x = ox; p->y = oy;
    lua_pushvalue(L, 2);
    return 1;
}

static int lua_transform_applyToPolygon(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    Polygon *p = luaL_checkudata(L, 2, META_POLYGON);
    for (int i = 0; i < p->count; i += 2) {
        float ox, oy;
        transform_apply(t, p->coords[i], p->coords[i+1], &ox, &oy);
        p->coords[i] = ox;
        p->coords[i+1] = oy;
    }
    lua_pushvalue(L, 2);
    return 1;
}

static int lua_transform_transformedPolygon(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    Polygon *p = luaL_checkudata(L, 2, META_POLYGON);
    Polygon *out = lua_newuserdata(L, sizeof(Polygon));
    out->count = p->count;
    out->coords = malloc((size_t)p->count * sizeof(float));
    for (int i = 0; i < p->count; i += 2)
        transform_apply(t, p->coords[i], p->coords[i+1],
                        &out->coords[i], &out->coords[i+1]);
    luaL_getmetatable(L, META_POLYGON);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_transform_toString(lua_State *L) {
    AffineTransform *t = luaL_checkudata(L, 1, META_TRANSFORM);
    lua_pushfstring(L, "transform(%f,%f,%f,%f,%f,%f)", t->m[0], t->m[1], t->m[2], t->m[3], t->m[4], t->m[5]);
    return 1;
}

#define META_VECTOR "pd.vector2D"

typedef struct { float dx, dy; } PDVector2D;

void pd_push_vector2D(lua_State *L, float dx, float dy);

static PDVector2D *push_vector(lua_State *L, float dx, float dy) {
    PDVector2D *v = lua_newuserdata(L, sizeof(PDVector2D));
    v->dx = dx;
    v->dy = dy;
    luaL_getmetatable(L, META_VECTOR);
    lua_setmetatable(L, -2);
    return v;
}

void pd_push_vector2D(lua_State *L, float dx, float dy) {
    push_vector(L, dx, dy);
}

void pd_push_rect(lua_State *L, float x, float y, float w, float h) {
    PDRectInternal *r = lua_newuserdata(L, sizeof(PDRectInternal));
    r->x = x; r->y = y; r->width = w; r->height = h;
    luaL_getmetatable(L, META_RECT);
    lua_setmetatable(L, -2);
}

void pd_push_point(lua_State *L, float x, float y) {
    PDPoint *p = lua_newuserdata(L, sizeof(PDPoint));
    p->x = x; p->y = y;
    luaL_getmetatable(L, META_POINT);
    lua_setmetatable(L, -2);
}

static int lua_vector_new(lua_State *L) {
    push_vector(L, (float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0));
    return 1;
}

static int lua_vector_index(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "dx") == 0 || strcmp(key, "x") == 0) { lua_pushnumber(L, v->dx); return 1; }
    if (strcmp(key, "dy") == 0 || strcmp(key, "y") == 0) { lua_pushnumber(L, v->dy); return 1; }
    lua_getmetatable(L, 1);
    lua_getfield(L, -1, key);
    return 1;
}

static int lua_vector_newindex(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "dx") == 0 || strcmp(key, "x") == 0) v->dx = (float)luaL_checknumber(L, 3);
    else if (strcmp(key, "dy") == 0 || strcmp(key, "y") == 0) v->dy = (float)luaL_checknumber(L, 3);
    return 0;
}

static int lua_vector_toString(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    lua_pushfstring(L, "vector2D(%f, %f)", v->dx, v->dy);
    return 1;
}

static int lua_vector_add(lua_State *L) {
    PDVector2D *a = luaL_checkudata(L, 1, META_VECTOR);
    PDVector2D *b = luaL_checkudata(L, 2, META_VECTOR);
    push_vector(L, a->dx + b->dx, a->dy + b->dy);
    return 1;
}

static int lua_vector_sub(lua_State *L) {
    PDVector2D *a = luaL_checkudata(L, 1, META_VECTOR);
    PDVector2D *b = luaL_checkudata(L, 2, META_VECTOR);
    push_vector(L, a->dx - b->dx, a->dy - b->dy);
    return 1;
}

static int lua_vector_mul(lua_State *L) {
    if (lua_isnumber(L, 1)) {
        float s = (float)lua_tonumber(L, 1);
        PDVector2D *v = luaL_checkudata(L, 2, META_VECTOR);
        push_vector(L, v->dx * s, v->dy * s);
        return 1;
    }
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    if (lua_isnumber(L, 2)) {
        float s = (float)lua_tonumber(L, 2);
        push_vector(L, v->dx * s, v->dy * s);
        return 1;
    }
    PDVector2D *b = luaL_checkudata(L, 2, META_VECTOR);
    lua_pushnumber(L, v->dx * b->dx + v->dy * b->dy);
    return 1;
}

static int lua_vector_div(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    float s = (float)luaL_checknumber(L, 2);
    push_vector(L, s != 0 ? v->dx / s : 0, s != 0 ? v->dy / s : 0);
    return 1;
}

static int lua_vector_unm(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    push_vector(L, -v->dx, -v->dy);
    return 1;
}

static int lua_vector_magnitude(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    lua_pushnumber(L, sqrtf(v->dx * v->dx + v->dy * v->dy));
    return 1;
}

static int lua_vector_magnitudeSquared(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    lua_pushnumber(L, v->dx * v->dx + v->dy * v->dy);
    return 1;
}

static int lua_vector_normalize(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    float m = sqrtf(v->dx * v->dx + v->dy * v->dy);
    if (m > 0) { v->dx /= m; v->dy /= m; }
    return 0;
}

static int lua_vector_normalized(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    float m = sqrtf(v->dx * v->dx + v->dy * v->dy);
    if (m > 0) push_vector(L, v->dx / m, v->dy / m);
    else push_vector(L, 0, 0);
    return 1;
}

static int lua_vector_scaledBy(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    float s = (float)luaL_checknumber(L, 2);
    push_vector(L, v->dx * s, v->dy * s);
    return 1;
}

static int lua_vector_scale(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    float s = (float)luaL_checknumber(L, 2);
    v->dx *= s;
    v->dy *= s;
    return 0;
}

static int lua_vector_addVector(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    PDVector2D *b = luaL_checkudata(L, 2, META_VECTOR);
    v->dx += b->dx;
    v->dy += b->dy;
    if (getenv("PD_TRACE")) fprintf(stderr, "[addV %g,%g -> %g,%g]", b->dx, b->dy, v->dx, v->dy);
    return 0;
}

static int lua_vector_dotProduct(lua_State *L) {
    PDVector2D *a = luaL_checkudata(L, 1, META_VECTOR);
    PDVector2D *b = luaL_checkudata(L, 2, META_VECTOR);
    lua_pushnumber(L, a->dx * b->dx + a->dy * b->dy);
    return 1;
}

static int lua_vector_unpack(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    lua_pushnumber(L, v->dx);
    lua_pushnumber(L, v->dy);
    return 2;
}

static int lua_vector_copy(lua_State *L) {
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    push_vector(L, v->dx, v->dy);
    return 1;
}

static int lua_vector_angleBetween(lua_State *L) {
    PDVector2D *a = luaL_checkudata(L, 1, META_VECTOR);
    PDVector2D *b = luaL_checkudata(L, 2, META_VECTOR);
    float angle = atan2f(b->dy, b->dx) - atan2f(a->dy, a->dx);
    lua_pushnumber(L, angle * 180.0f / (float)M_PI);
    return 1;
}

static int lua_point_sub(lua_State *L) {
    PDPoint *a = luaL_checkudata(L, 1, META_POINT);
    PDPoint *b = luaL_testudata(L, 2, META_POINT);
    if (b) {
        pd_push_vector2D(L, a->x - b->x, a->y - b->y);
        return 1;
    }
    PDVector2D *v = luaL_checkudata(L, 2, META_VECTOR);
    pd_push_point(L, a->x - v->dx, a->y - v->dy);
    return 1;
}

static int lua_point_add(lua_State *L) {
    PDPoint *a = luaL_testudata(L, 1, META_POINT);
    if (a) {
        PDVector2D *v = luaL_checkudata(L, 2, META_VECTOR);
        pd_push_point(L, a->x + v->dx, a->y + v->dy);
        return 1;
    }
    PDVector2D *v = luaL_checkudata(L, 1, META_VECTOR);
    PDPoint *b = luaL_checkudata(L, 2, META_POINT);
    pd_push_point(L, b->x + v->dx, b->y + v->dy);
    return 1;
}

static int lua_point_offsetBy(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    float dx = (float)luaL_checknumber(L, 2);
    float dy = (float)luaL_checknumber(L, 3);
    pd_push_point(L, p->x + dx, p->y + dy);
    return 1;
}

static int lua_point_offset(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    p->x += (float)luaL_checknumber(L, 2);
    p->y += (float)luaL_checknumber(L, 3);
    return 0;
}

static int lua_point_copy(lua_State *L) {
    PDPoint *p = luaL_checkudata(L, 1, META_POINT);
    pd_push_point(L, p->x, p->y);
    return 1;
}

static int lua_point_distanceToPoint(lua_State *L) {
    PDPoint *a = luaL_checkudata(L, 1, META_POINT);
    PDPoint *b = luaL_checkudata(L, 2, META_POINT);
    float dx = a->x - b->x, dy = a->y - b->y;
    lua_pushnumber(L, sqrtf(dx * dx + dy * dy));
    return 1;
}

static int lua_point_squaredDistanceToPoint(lua_State *L) {
    PDPoint *a = luaL_checkudata(L, 1, META_POINT);
    PDPoint *b = luaL_checkudata(L, 2, META_POINT);
    float dx = a->x - b->x, dy = a->y - b->y;
    lua_pushnumber(L, dx * dx + dy * dy);
    return 1;
}

#define META_LINESEG "pd.lineSegment"

typedef struct { float x1, y1, x2, y2; } PDLineSegment;

static int lua_lineseg_new(lua_State *L) {
    PDLineSegment *ls = lua_newuserdata(L, sizeof(PDLineSegment));
    ls->x1 = (float)luaL_optnumber(L, 1, 0);
    ls->y1 = (float)luaL_optnumber(L, 2, 0);
    ls->x2 = (float)luaL_optnumber(L, 3, 0);
    ls->y2 = (float)luaL_optnumber(L, 4, 0);
    luaL_getmetatable(L, META_LINESEG);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_lineseg_index(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "x1") == 0) { lua_pushnumber(L, ls->x1); return 1; }
    if (strcmp(key, "y1") == 0) { lua_pushnumber(L, ls->y1); return 1; }
    if (strcmp(key, "x2") == 0) { lua_pushnumber(L, ls->x2); return 1; }
    if (strcmp(key, "y2") == 0) { lua_pushnumber(L, ls->y2); return 1; }
    luaL_getmetatable(L, META_LINESEG);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int lua_lineseg_newindex(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    const char *key = luaL_checkstring(L, 2);
    float v = (float)luaL_checknumber(L, 3);
    if (strcmp(key, "x1") == 0) ls->x1 = v;
    else if (strcmp(key, "y1") == 0) ls->y1 = v;
    else if (strcmp(key, "x2") == 0) ls->x2 = v;
    else if (strcmp(key, "y2") == 0) ls->y2 = v;
    return 0;
}

static int lua_lineseg_unpack(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    lua_pushnumber(L, ls->x1);
    lua_pushnumber(L, ls->y1);
    lua_pushnumber(L, ls->x2);
    lua_pushnumber(L, ls->y2);
    return 4;
}

static int lua_lineseg_length(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    float dx = ls->x2 - ls->x1, dy = ls->y2 - ls->y1;
    lua_pushnumber(L, sqrtf(dx * dx + dy * dy));
    return 1;
}

static int lua_lineseg_midPoint(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    pd_push_point(L, (ls->x1 + ls->x2) * 0.5f, (ls->y1 + ls->y2) * 0.5f);
    return 1;
}

static int lua_lineseg_segmentVector(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    pd_push_vector2D(L, ls->x2 - ls->x1, ls->y2 - ls->y1);
    return 1;
}

static int lua_lineseg_offsetBy(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    float dx = (float)luaL_checknumber(L, 2);
    float dy = (float)luaL_checknumber(L, 3);
    PDLineSegment *out = lua_newuserdata(L, sizeof(PDLineSegment));
    out->x1 = ls->x1 + dx; out->y1 = ls->y1 + dy;
    out->x2 = ls->x2 + dx; out->y2 = ls->y2 + dy;
    luaL_getmetatable(L, META_LINESEG);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_lineseg_copy(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    PDLineSegment *out = lua_newuserdata(L, sizeof(PDLineSegment));
    *out = *ls;
    luaL_getmetatable(L, META_LINESEG);
    lua_setmetatable(L, -2);
    return 1;
}

int pd_get_lineSegment(lua_State *L, int idx, float *x1, float *y1, float *x2, float *y2) {
    PDLineSegment *ls = luaL_testudata(L, idx, META_LINESEG);
    if (!ls) return 0;
    *x1 = ls->x1; *y1 = ls->y1; *x2 = ls->x2; *y2 = ls->y2;
    return 1;
}

static int lua_lineseg_pointOnLine(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    float dist = (float)luaL_checknumber(L, 2);
    int extend = lua_toboolean(L, 3);
    float dx = ls->x2 - ls->x1, dy = ls->y2 - ls->y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len == 0) { pd_push_point(L, ls->x1, ls->y1); return 1; }
    float t = dist / len;
    if (!extend && (t < 0 || t > 1)) { lua_pushnil(L); return 1; }
    pd_push_point(L, ls->x1 + dx * t, ls->y1 + dy * t);
    return 1;
}

static int lua_lineseg_closestPointOnLineToPoint(lua_State *L) {
    PDLineSegment *ls = luaL_checkudata(L, 1, META_LINESEG);
    PDPoint *p = luaL_checkudata(L, 2, META_POINT);
    float dx = ls->x2 - ls->x1, dy = ls->y2 - ls->y1;
    float len_sq = dx * dx + dy * dy;
    if (len_sq == 0) { pd_push_point(L, ls->x1, ls->y1); return 1; }
    float t = ((p->x - ls->x1) * dx + (p->y - ls->y1) * dy) / len_sq;
    if (t < 0) t = 0; if (t > 1) t = 1;
    pd_push_point(L, ls->x1 + dx * t, ls->y1 + dy * t);
    return 1;
}

static int lua_lineseg_intersectsLineSegment(lua_State *L) {
    PDLineSegment *a = luaL_checkudata(L, 1, META_LINESEG);
    PDLineSegment *b = luaL_testudata(L, 2, META_LINESEG);
    if (!b) {
        float x1 = (float)luaL_checknumber(L, 2), y1 = (float)luaL_checknumber(L, 3);
        float x2 = (float)luaL_checknumber(L, 4), y2 = (float)luaL_checknumber(L, 5);
        float d = (a->x2 - a->x1) * (y2 - y1) - (a->y2 - a->y1) * (x2 - x1);
        if (d == 0) { lua_pushboolean(L, 0); return 1; }
        float t = ((x1 - a->x1) * (y2 - y1) - (y1 - a->y1) * (x2 - x1)) / d;
        float u = ((x1 - a->x1) * (a->y2 - a->y1) - (y1 - a->y1) * (a->x2 - a->x1)) / d;
        lua_pushboolean(L, t >= 0 && t <= 1 && u >= 0 && u <= 1);
        return 1;
    }
    float d = (a->x2 - a->x1) * (b->y2 - b->y1) - (a->y2 - a->y1) * (b->x2 - b->x1);
    if (d == 0) { lua_pushboolean(L, 0); return 1; }
    float t = ((b->x1 - a->x1) * (b->y2 - b->y1) - (b->y1 - a->y1) * (b->x2 - b->x1)) / d;
    float u = ((b->x1 - a->x1) * (a->y2 - a->y1) - (b->y1 - a->y1) * (a->x2 - a->x1)) / d;
    lua_pushboolean(L, t >= 0 && t <= 1 && u >= 0 && u <= 1);
    return 1;
}

#define META_ARC "pd.arc"

typedef struct {
    float x, y, radius, startAngle, endAngle;
    int clockwise;
} PDArc;

static int lua_arc_new(lua_State *L) {
    PDArc *a = lua_newuserdata(L, sizeof(PDArc));
    a->x = (float)luaL_optnumber(L, 1, 0);
    a->y = (float)luaL_optnumber(L, 2, 0);
    a->radius = (float)luaL_optnumber(L, 3, 0);
    a->startAngle = (float)luaL_optnumber(L, 4, 0);
    a->endAngle = (float)luaL_optnumber(L, 5, 360);
    a->clockwise = lua_isnoneornil(L, 6) ? 1 : lua_toboolean(L, 6);
    luaL_getmetatable(L, META_ARC);
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_arc_index(lua_State *L) {
    PDArc *a = luaL_checkudata(L, 1, META_ARC);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "x") == 0) { lua_pushnumber(L, a->x); return 1; }
    if (strcmp(key, "y") == 0) { lua_pushnumber(L, a->y); return 1; }
    if (strcmp(key, "radius") == 0) { lua_pushnumber(L, a->radius); return 1; }
    if (strcmp(key, "startAngle") == 0) { lua_pushnumber(L, a->startAngle); return 1; }
    if (strcmp(key, "endAngle") == 0) { lua_pushnumber(L, a->endAngle); return 1; }
    if (strcmp(key, "clockwise") == 0) { lua_pushboolean(L, a->clockwise); return 1; }
    luaL_getmetatable(L, META_ARC);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int lua_arc_newindex(lua_State *L) {
    PDArc *a = luaL_checkudata(L, 1, META_ARC);
    const char *key = luaL_checkstring(L, 2);
    if (strcmp(key, "clockwise") == 0) { a->clockwise = lua_toboolean(L, 3); return 0; }
    float v = (float)luaL_checknumber(L, 3);
    if (strcmp(key, "x") == 0) a->x = v;
    else if (strcmp(key, "y") == 0) a->y = v;
    else if (strcmp(key, "radius") == 0) a->radius = v;
    else if (strcmp(key, "startAngle") == 0) a->startAngle = v;
    else if (strcmp(key, "endAngle") == 0) a->endAngle = v;
    return 0;
}

static int lua_arc_length(lua_State *L) {
    PDArc *a = luaL_checkudata(L, 1, META_ARC);
    float sweep = a->endAngle - a->startAngle;
    if (sweep < 0) sweep = -sweep;
    lua_pushnumber(L, a->radius * sweep * (float)M_PI / 180.0f);
    return 1;
}

/* playdate.geometry.distanceToPoint(x1, y1, x2, y2) — fast variant */
static int lua_geo_distanceToPoint(lua_State *L) {
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    float dx = x2 - x1, dy = y2 - y1;
    lua_pushnumber(L, sqrtf(dx * dx + dy * dy));
    return 1;
}

static int lua_geo_squaredDistanceToPoint(lua_State *L) {
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    float dx = x2 - x1, dy = y2 - y1;
    lua_pushnumber(L, dx * dx + dy * dy);
    return 1;
}

void pd_geometry_register(lua_State *L) {
    luaL_newmetatable(L, META_POINT);
    lua_pushcfunction(L, lua_point_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_point_newindex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, lua_point_toString); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, lua_point_sub); lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, lua_point_add); lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, lua_point_unpack); lua_setfield(L, -2, "unpack");
    lua_pushcfunction(L, lua_point_unpack); lua_setfield(L, -2, "getX");
    lua_pushcfunction(L, lua_point_unpack); lua_setfield(L, -2, "getY");
    lua_pushcfunction(L, lua_point_offsetBy); lua_setfield(L, -2, "offsetBy");
    lua_pushcfunction(L, lua_point_offset); lua_setfield(L, -2, "offset");
    lua_pushcfunction(L, lua_point_copy); lua_setfield(L, -2, "copy");
    lua_pushcfunction(L, lua_point_distanceToPoint); lua_setfield(L, -2, "distanceToPoint");
    lua_pushcfunction(L, lua_point_squaredDistanceToPoint); lua_setfield(L, -2, "squaredDistanceToPoint");
    lua_pop(L, 1);

    luaL_newmetatable(L, META_RECT);
    lua_pushcfunction(L, lua_rect_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_rect_newindex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, lua_rect_toString); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, lua_rect_offsetBy); lua_setfield(L, -2, "offsetBy");
    lua_pushcfunction(L, lua_rect_offset); lua_setfield(L, -2, "offset");
    lua_pushcfunction(L, lua_rect_unpack); lua_setfield(L, -2, "unpack");
    lua_pushcfunction(L, lua_rect_containsPoint); lua_setfield(L, -2, "containsPoint");
    lua_pushcfunction(L, lua_rect_intersects); lua_setfield(L, -2, "intersects");
    lua_pushcfunction(L, lua_rect_copy); lua_setfield(L, -2, "copy");
    lua_pushcfunction(L, lua_rect_isCollapsed); lua_setfield(L, -2, "isCollapsed");
    lua_pushcfunction(L, lua_rect_encapsulate); lua_setfield(L, -2, "encapsulate");
    lua_pushcfunction(L, lua_rect_insetBy); lua_setfield(L, -2, "insetBy");
    lua_pushcfunction(L, lua_rect_inset); lua_setfield(L, -2, "inset");
    lua_pushcfunction(L, lua_rect_intersection2); lua_setfield(L, -2, "intersection");
    lua_pushcfunction(L, lua_rect_union2); lua_setfield(L, -2, "union");
    lua_pushcfunction(L, lua_rect_isEmpty); lua_setfield(L, -2, "isEmpty");
    lua_pushcfunction(L, lua_rect_isEqual); lua_setfield(L, -2, "isEqual");
    lua_pushcfunction(L, lua_rect_centerPoint); lua_setfield(L, -2, "centerPoint");
    lua_pushcfunction(L, lua_rect_containsRect); lua_setfield(L, -2, "containsRect");
    lua_pushcfunction(L, lua_rect_toPolygon); lua_setfield(L, -2, "toPolygon");
    lua_pop(L, 1);

    luaL_newmetatable(L, META_SIZE);
    lua_pushcfunction(L, lua_size_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_size_newindex); lua_setfield(L, -2, "__newindex");
    lua_pop(L, 1);

    luaL_newmetatable(L, META_POLYGON);
    lua_pushcfunction(L, lua_polygon_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_noop); lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, lua_polygon_toString); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, lua_polygon_mul); lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, lua_polygon_getBounds); lua_setfield(L, -2, "getBounds");
    lua_pushcfunction(L, lua_polygon_close); lua_setfield(L, -2, "close");
    lua_pushcfunction(L, lua_polygon_isClosed); lua_setfield(L, -2, "isClosed");
    lua_pushcfunction(L, lua_polygon_setPointAt); lua_setfield(L, -2, "setPointAt");
    lua_pushcfunction(L, lua_polygon_getPointAt); lua_setfield(L, -2, "getPointAt");
    lua_pushcfunction(L, lua_polygon_count); lua_setfield(L, -2, "count");
    lua_pop(L, 1);

    luaL_newmetatable(L, META_TRANSFORM);
    lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_transform_toString); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, lua_transform_identity); lua_setfield(L, -2, "identity");
    lua_pushcfunction(L, lua_transform_identity); lua_setfield(L, -2, "reset");
    lua_pushcfunction(L, lua_transform_transformXY); lua_setfield(L, -2, "transformXY");
    lua_pushcfunction(L, lua_transform_translate); lua_setfield(L, -2, "translate");
    lua_pushcfunction(L, lua_transform_rotate); lua_setfield(L, -2, "rotate");
    lua_pushcfunction(L, lua_transform_scale); lua_setfield(L, -2, "scale");
    lua_pushcfunction(L, lua_transform_multiply); lua_setfield(L, -2, "multiply");
    lua_pushcfunction(L, lua_transform_applyToPoint); lua_setfield(L, -2, "transformPoint");
    lua_pushcfunction(L, lua_transform_applyToPolygon); lua_setfield(L, -2, "transformPolygon");
    lua_pushcfunction(L, lua_transform_transformedPolygon); lua_setfield(L, -2, "transformedPolygon");
    lua_pop(L, 1);

    luaL_newmetatable(L, META_VECTOR);
    lua_pushcfunction(L, lua_vector_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_vector_newindex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, lua_vector_toString); lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, lua_vector_add); lua_setfield(L, -2, "__add");
    lua_pushcfunction(L, lua_vector_sub); lua_setfield(L, -2, "__sub");
    lua_pushcfunction(L, lua_vector_mul); lua_setfield(L, -2, "__mul");
    lua_pushcfunction(L, lua_vector_div); lua_setfield(L, -2, "__div");
    lua_pushcfunction(L, lua_vector_unm); lua_setfield(L, -2, "__unm");
    lua_pushcfunction(L, lua_vector_magnitude); lua_setfield(L, -2, "magnitude");
    lua_pushcfunction(L, lua_vector_magnitudeSquared); lua_setfield(L, -2, "magnitudeSquared");
    lua_pushcfunction(L, lua_vector_normalize); lua_setfield(L, -2, "normalize");
    lua_pushcfunction(L, lua_vector_normalized); lua_setfield(L, -2, "normalized");
    lua_pushcfunction(L, lua_vector_scaledBy); lua_setfield(L, -2, "scaledBy");
    lua_pushcfunction(L, lua_vector_scale); lua_setfield(L, -2, "scale");
    lua_pushcfunction(L, lua_vector_addVector); lua_setfield(L, -2, "addVector");
    lua_pushcfunction(L, lua_vector_dotProduct); lua_setfield(L, -2, "dotProduct");
    lua_pushcfunction(L, lua_vector_unpack); lua_setfield(L, -2, "unpack");
    lua_pushcfunction(L, lua_vector_copy); lua_setfield(L, -2, "copy");
    lua_pushcfunction(L, lua_vector_angleBetween); lua_setfield(L, -2, "angleBetween");
    lua_pop(L, 1);

    lua_getglobal(L, "playdate");
    lua_newtable(L);

    lua_pushcfunction(L, lua_geo_distanceToPoint);
    lua_setfield(L, -2, "distanceToPoint");
    lua_pushcfunction(L, lua_geo_squaredDistanceToPoint);
    lua_setfield(L, -2, "squaredDistanceToPoint");

    /* Expose each metatable as the public class table so CoreLibs
       identity checks like getmetatable(p) == playdate.geometry.point hold. */
    luaL_getmetatable(L, META_POINT);
    lua_pushcfunction(L, lua_point_new); lua_setfield(L, -2, "new");
    lua_pushcfunction(L, lua_point_new); lua_setfield(L, -2, "newP");
    lua_setfield(L, -2, "point");

    luaL_getmetatable(L, META_RECT);
    lua_pushcfunction(L, lua_rect_new); lua_setfield(L, -2, "new");
    lua_pushcfunction(L, lua_rect_new); lua_setfield(L, -2, "newR");
    lua_pushcfunction(L, lua_rect_fast_intersection); lua_setfield(L, -2, "fast_intersection");
    lua_pushcfunction(L, lua_rect_fast_union); lua_setfield(L, -2, "fast_union");
    lua_setfield(L, -2, "rect");

    luaL_getmetatable(L, META_SIZE);
    lua_pushcfunction(L, lua_size_new); lua_setfield(L, -2, "new");
    lua_pushcfunction(L, lua_size_new); lua_setfield(L, -2, "newS");
    lua_setfield(L, -2, "size");

    luaL_getmetatable(L, META_POLYGON);
    lua_pushcfunction(L, lua_polygon_new); lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "polygon");

    luaL_getmetatable(L, META_TRANSFORM);
    lua_pushcfunction(L, lua_transform_new); lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "affineTransform");

    luaL_newmetatable(L, META_ARC);
    lua_pushcfunction(L, lua_arc_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_arc_newindex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, lua_arc_length); lua_setfield(L, -2, "length");
    lua_pushcfunction(L, lua_arc_new); lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "arc");

    luaL_newmetatable(L, META_LINESEG);
    lua_pushcfunction(L, lua_lineseg_index); lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_lineseg_newindex); lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, lua_lineseg_unpack); lua_setfield(L, -2, "unpack");
    lua_pushcfunction(L, lua_lineseg_length); lua_setfield(L, -2, "length");
    lua_pushcfunction(L, lua_lineseg_midPoint); lua_setfield(L, -2, "midPoint");
    lua_pushcfunction(L, lua_lineseg_segmentVector); lua_setfield(L, -2, "segmentVector");
    lua_pushcfunction(L, lua_lineseg_offsetBy); lua_setfield(L, -2, "offsetBy");
    lua_pushcfunction(L, lua_lineseg_copy); lua_setfield(L, -2, "copy");
    lua_pushcfunction(L, lua_lineseg_pointOnLine); lua_setfield(L, -2, "pointOnLine");
    lua_pushcfunction(L, lua_lineseg_closestPointOnLineToPoint); lua_setfield(L, -2, "closestPointOnLineToPoint");
    lua_pushcfunction(L, lua_lineseg_intersectsLineSegment); lua_setfield(L, -2, "intersectsLineSegment");
    lua_pushcfunction(L, lua_lineseg_new); lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "lineSegment");

    luaL_getmetatable(L, META_VECTOR);
    lua_pushcfunction(L, lua_vector_new); lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "vector2D");

    lua_setfield(L, -2, "geometry");
    lua_pop(L, 1);
}
