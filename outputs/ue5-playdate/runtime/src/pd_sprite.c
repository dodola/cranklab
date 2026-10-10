/*
 * Sprite subsystem — Lua-table-first rewrite.
 *
 * Every sprite is a Lua TABLE (not userdata). The LCDSprite* lives in
 * table field "_userdata" (pointer userdata, META_SPRITE metatable).
 * All C methods are exported via a single `pd.sprite` metatable whose
 * __index points to itself — so sprites, CoreLibs spritelib decorations,
 * and user methods coexist on the *same* table chain.
 *
 * Invariants:
 *  - lua_sprite_new() returns a table; it never returns userdata.
 *  - `graphics.sprite` IS `pd.sprite` (the metatable), so CoreLibs'
 *    `spritelib = gfx.sprite; spritelib.new = spritelib.new` works.
 *  - check_sprite() accepts a table (extracts `_userdata`) or a raw
 *    userdata, never errors; callers must NULL-check.
 *  - lua_sprite_free() removes from global list but never unrefs Lua
 *    refs and never free()s the C struct — we leak sprite memory on
 *    purpose to avoid the double-free that plagues CoreLibs GC.
 */

#include "pd_sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define META_SPRITE "pd.sprite"

static LCDSprite **g_sprites = NULL;
static int g_sprite_count = 0;
static int g_sprite_capacity = 0;

static int g_sprite_table_ref = LUA_NOREF;
/* Sprites intentionally survive individual Lua GC calls. Reclaim their C
   allocations only after the complete Lua state has closed. */
static LCDSprite **g_allocated = NULL;
static int g_allocated_count = 0, g_allocated_capacity = 0;

static void ensure_sprite_capacity(int needed) {
    if (g_sprite_capacity < needed) {
        int newcap = g_sprite_capacity ? g_sprite_capacity * 2 : 16;
        while (newcap < needed) newcap *= 2;
        g_sprites = (LCDSprite **)realloc(g_sprites, (size_t)newcap * sizeof(LCDSprite *));
        g_sprite_capacity = newcap;
    }
}

static LCDSprite *check_sprite(lua_State *L, int n) {
    if (lua_isnil(L, n)) return NULL;
    if (lua_istable(L, n)) {
        lua_getfield(L, n, "_userdata");
        LCDSprite **ud = luaL_testudata(L, -1, META_SPRITE);
        lua_pop(L, 1);
        if (!ud || !*ud) return NULL;
        return *ud;
    }
    LCDSprite **ud = luaL_testudata(L, n, META_SPRITE);
    if (!ud) return NULL;
    return *ud;
}

#define SPRITE_MAP_KEY "pd.sprite.map"

static void get_sprite_map(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, SPRITE_MAP_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_newtable(L);
        lua_pushstring(L, "v");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, SPRITE_MAP_KEY);
    }
}

static void sprite_map_store(lua_State *L, LCDSprite *s, int table_idx) {
    table_idx = lua_absindex(L, table_idx);
    get_sprite_map(L);
    lua_pushlightuserdata(L, s);
    lua_pushvalue(L, table_idx);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static void attach_sprite_userdata(lua_State *L, LCDSprite *s, int table_idx) {
    table_idx = lua_absindex(L, table_idx);
    LCDSprite **ud = lua_newuserdata(L, sizeof(LCDSprite *));
    *ud = s;
    luaL_getmetatable(L, META_SPRITE);
    lua_setmetatable(L, -2);
    lua_setfield(L, table_idx, "_userdata");
    sprite_map_store(L, s, table_idx);
}

static void sprite_sync_fields(lua_State *L, LCDSprite *s) {
    if (!s) return;
    get_sprite_map(L);
    lua_pushlightuserdata(L, s);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
        lua_pushnumber(L, s->x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, s->y);
        lua_setfield(L, -2, "y");
        lua_pushnumber(L, s->width);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, s->height);
        lua_setfield(L, -2, "height");
    }
    lua_pop(L, 2);
}

static void push_sprite_table(lua_State *L, LCDSprite *s) {
    if (!s) { lua_pushnil(L); return; }
    get_sprite_map(L);
    lua_pushlightuserdata(L, s);
    lua_rawget(L, -2);
    if (lua_istable(L, -1)) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 2);
    lua_newtable(L);
    if (g_sprite_table_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_sprite_table_ref);
        if (lua_istable(L, -1)) lua_setmetatable(L, -2);
        else lua_pop(L, 1);
    }
    attach_sprite_userdata(L, s, -1);
}

static LCDSprite *sprite_alloc_defaults(void);

static int lua_sprite_new(lua_State *L) {
    LCDSprite *s = sprite_alloc_defaults();

    if (lua_type(L, 1) == LUA_TSTRING) {
        const char *path = lua_tostring(L, 1);
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
        if (access(fullpath, F_OK) != 0)
            snprintf(fullpath, sizeof(fullpath), "%s/%s.pdi", g_pd.pdx_dir, path);
        LCDBitmap *bm = pd_load_pdi(fullpath);
        if (bm) {
            s->image = bm;
            s->width = bm->width;
            s->height = bm->height;
            LCDBitmap **iud = lua_newuserdata(L, sizeof(LCDBitmap *));
            *iud = bm;
            luaL_getmetatable(L, "pd.bitmap");
            lua_setmetatable(L, -2);
            s->lua_image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    } else if (lua_isuserdata(L, 1)) {
        LCDBitmap **bm = luaL_testudata(L, 1, "pd.bitmap");
        if (bm && *bm) {
            s->image = *bm;
            s->width = (*bm)->width;
            s->height = (*bm)->height;
            lua_pushvalue(L, 1);
            s->lua_image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    } else if (lua_isnumber(L, 1)) {
        int w = (int)lua_tonumber(L, 1);
        int h = (int)lua_tonumber(L, 2);
        if (w > 0 && h > 0) {
            s->image = calloc(1, sizeof(LCDBitmap));
            s->image->width = w;
            s->image->height = h;
            s->image->rowbytes = (w + 7) / 8;
            s->image->mask = calloc(s->image->rowbytes * h, 1);
            s->image->data = calloc(s->image->rowbytes * h, 1);
            s->width = (float)w;
            s->height = (float)h;
        }
    }
    push_sprite_table(L, s);
    sprite_sync_fields(L, s);
    return 1;
}

static void sprite_live_release(lua_State *L, LCDSprite *s);

static int lua_sprite_free(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (s->in_list) {
        for (int i = 0; i < g_sprite_count; i++) {
            if (g_sprites[i] == s) {
                g_sprites[i] = g_sprites[--g_sprite_count];
                break;
            }
        }
        s->in_list = 0;
    }
    sprite_live_release(L, s);
#ifdef CRANK_ESP32
    if(lua_isuserdata(L,1)) {
        /* The userdata finalizer owns this unreachable sprite. Delay C
           reclamation until callback/list snapshots have left the frame. */
        s->free_flag=1;
        LCDSprite **ud=luaL_testudata(L,1,META_SPRITE);if(ud)*ud=NULL;
    }
#endif
    return 0;
}

static int lua_sprite_setBounds(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    float bx, by, bw, bh;
    if (lua_isuserdata(L, 2)) {
        PDRectInternal *r = luaL_checkudata(L, 2, "pd.rect");
        bx = r->x; by = r->y; bw = r->width; bh = r->height;
    } else {
        bx = (float)luaL_optnumber(L, 2, 0);
        by = (float)luaL_optnumber(L, 3, 0);
        bw = (float)luaL_optnumber(L, 4, 0);
        bh = (float)luaL_optnumber(L, 5, 0);
    }
    /* bounds are top-left based; our position is center-based */
    s->width = bw;
    s->height = bh;
    s->x = bx + bw * s->center_x;
    s->y = by + bh * s->center_y;
    sprite_sync_fields(L, s);
    return 0;
}

/* SDK: getBounds() returns x, y, width, height as four numbers
   (getBoundsRect is the rect-returning variant) */
static int lua_sprite_getBounds(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, s->x - s->width * s->center_x);
    lua_pushnumber(L, s->y - s->height * s->center_y);
    lua_pushnumber(L, s->width);
    lua_pushnumber(L, s->height);
    return 4;
}

static int lua_sprite_getBoundsRect(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    PDRectInternal *r = lua_newuserdata(L, sizeof(PDRectInternal));
    r->x = s->x - s->width * s->center_x;
    r->y = s->y - s->height * s->center_y;
    r->width = s->width;
    r->height = s->height;
    luaL_getmetatable(L, "pd.rect");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_sprite_moveTo(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s || lua_isnil(L, 2)) return 0;
    if (lua_isuserdata(L, 2)) {
        PDPoint *p = luaL_checkudata(L, 2, "pd.point");
        s->x = p->x; s->y = p->y;
        sprite_sync_fields(L, s);
        return 0;
    }
    if (lua_tonumber(L, 2)) s->x = (float)lua_tonumber(L, 2);
    if (lua_tonumber(L, 3)) s->y = (float)lua_tonumber(L, 3);
    sprite_sync_fields(L, s);
    return 0;
}

static int lua_sprite_moveBy(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s || lua_isnil(L, 2)) return 0;
    if (lua_tonumber(L, 2)) s->x += (float)lua_tonumber(L, 2);
    if (lua_tonumber(L, 3)) s->y += (float)lua_tonumber(L, 3);
    sprite_sync_fields(L, s);
    return 0;
}

static int lua_sprite_getPosition(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, s->x);
    lua_pushnumber(L, s->y);
    return 2;
}

static int lua_sprite_setSize(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->width = (float)luaL_optnumber(L, 2, 0);
    s->height = (float)luaL_optnumber(L, 3, 0);
    sprite_sync_fields(L, s);
    return 0;
}

static int lua_sprite_getSize(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, s->width);
    lua_pushnumber(L, s->height);
    return 2;
}

static int lua_sprite_setCenter(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->center_x = (float)luaL_optnumber(L, 2, 0.5);
    s->center_y = (float)luaL_optnumber(L, 3, 0.5);
    return 0;
}

static int lua_sprite_getCenter(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, s->center_x);
    lua_pushnumber(L, s->center_y);
    return 2;
}

static int lua_sprite_setImage(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (lua_isstring(L, 2)) {
        const char *path = lua_tostring(L, 2);
        char fullpath[1024];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
        if (access(fullpath, F_OK) != 0)
            snprintf(fullpath, sizeof(fullpath), "%s/%s.pdi", g_pd.pdx_dir, path);
        LCDBitmap *bm = pd_load_pdi(fullpath);
        if (bm) {
            s->image = bm;
            s->width = bm->width;
            s->height = bm->height;
            if (s->lua_image_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s->lua_image_ref);
            LCDBitmap **iud = lua_newuserdata(L, sizeof(LCDBitmap *));
            *iud = bm;
            luaL_getmetatable(L, "pd.bitmap");
            lua_setmetatable(L, -2);
            s->lua_image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
        if (lua_gettop(L) >= 3) s->flip = (LCDBitmapFlip)pd_flip_arg(L, 3);
        return 0;
    }
    if (lua_isnil(L, 2)) {
        s->image = NULL;
        s->width = 0;
        s->height = 0;
        if (s->lua_image_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, s->lua_image_ref);
            s->lua_image_ref = LUA_NOREF;
        }
        return 0;
    }
    LCDBitmap **bm = luaL_checkudata(L, 2, "pd.bitmap");
    s->image = *bm;
    if (s->image) {
        if (getenv("PD_TRACE")) {
            int blacks = 0;
            for (int bi = 0; bi < s->image->rowbytes * s->image->height; bi++)
                if (s->image->data[bi]) { blacks = 1; break; }
            fprintf(stderr, "[setImage %dx%d blk=%d]", s->image->width, s->image->height, blacks);
        }
        s->width = s->image->width;
        s->height = s->image->height;
        if (s->lua_image_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, s->lua_image_ref);
        lua_pushvalue(L, 2);
        s->lua_image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (lua_gettop(L) >= 3) s->flip = (LCDBitmapFlip)pd_flip_arg(L, 3);
    sprite_sync_fields(L, s);
    return 0;
}

static int lua_sprite_getImage(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s || !s->image) { lua_pushnil(L); return 1; }
    if (s->lua_image_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->lua_image_ref);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

/* sprite:setTilemap(tilemap): sprite renders the tilemap; sized to fit */
static int lua_sprite_setTilemap(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s || !lua_istable(L, 1)) return 0;
    if (lua_isnil(L, 2)) {
        lua_pushnil(L);
        lua_setfield(L, 1, "_pd_tilemap");
        return 0;
    }
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "_pd_tilemap");
    lua_getfield(L, 2, "getPixelSize");
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 2);
        if (lua_pcall(L, 1, 2, 0) == LUA_OK) {
            s->width = (float)lua_tonumber(L, -2);
            s->height = (float)lua_tonumber(L, -1);
            lua_pop(L, 2);
            sprite_sync_fields(L, s);
        } else {
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    return 0;
}

static int lua_sprite_setImageFlip(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->flip = (LCDBitmapFlip)pd_flip_arg(L, 2);
    return 0;
}

static int lua_sprite_method_noop(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_sprite_setRotation(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->rotation = (float)luaL_optnumber(L, 2, 0);
    if (lua_gettop(L) >= 3) s->scale = (float)luaL_optnumber(L, 3, 1.0);
    return 0;
}

static int lua_sprite_getRotation(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    lua_pushnumber(L, s ? s->rotation : 0);
    return 1;
}

static int lua_sprite_setScale(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->scale = (float)luaL_optnumber(L, 2, 1.0);
    if (s->scale <= 0) s->scale = 1.0f;
    if (s->image) {
        s->width = s->image->width * s->scale;
        s->height = s->image->height * s->scale;
        sprite_sync_fields(L, s);
    }
    return 0;
}

static int lua_sprite_getScale(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    lua_pushnumber(L, s ? s->scale : 1);
    lua_pushnumber(L, s ? s->scale : 1);
    return 2;
}

static int lua_sprite_setImageDrawMode(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->draw_mode = lua_isnoneornil(L, 2) ? kDrawModeCopy
                                         : (LCDBitmapDrawMode)pd_drawmode_arg(L, 2);
    return 0;
}

static int lua_sprite_getImageFlip(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (int)s->flip);
    return 1;
}

static int lua_sprite_setVisible(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->visible = lua_toboolean(L, 2);
    return 0;
}

static int lua_sprite_setUpdatesEnabled(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->updates_enabled = lua_toboolean(L, 2);
    return 0;
}

static int lua_sprite_updatesEnabled(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, s->updates_enabled);
    return 1;
}

static int lua_sprite_isVisible(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, s->visible);
    return 1;
}

static int lua_sprite_setZIndex(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->z_index = (int16_t)luaL_optinteger(L, 2, 0);
    return 0;
}

static int lua_sprite_getZIndex(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, s->z_index);
    return 1;
}

static int lua_sprite_setTag(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->tag = (uint8_t)(luaL_optinteger(L, 2, 0) & 0xFF);
    return 0;
}

static int lua_sprite_getTag(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, s->tag);
    return 1;
}

static int lua_sprite_setOpaque(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->opaque = (int)lua_toboolean(L, 2);
    return 0;
}

static int lua_sprite_isOpaque(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, s->opaque);
    return 1;
}

static int lua_sprite_setCollisionsEnabled(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->collisions_enabled = lua_toboolean(L, 2);
    return 0;
}

/* Accepts an array of group numbers (1-32) or a single group number. */
static uint32_t groups_to_mask(lua_State *L, int idx) {
    uint32_t mask = 0;
    if (lua_istable(L, idx)) {
        lua_Integer n = lua_rawlen(L, idx);
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, idx, i);
            lua_Integer g = lua_tointeger(L, -1);
            lua_pop(L, 1);
            if (g >= 1 && g <= 32) mask |= (uint32_t)1 << (g - 1);
        }
    } else if (lua_isnumber(L, idx)) {
        lua_Integer g = lua_tointeger(L, idx);
        if (g >= 1 && g <= 32) mask |= (uint32_t)1 << (g - 1);
    }
    return mask;
}

static int lua_sprite_setGroups(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->group_mask = groups_to_mask(L, 2);
    return 0;
}

static int lua_sprite_setCollidesWithGroups(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->collides_mask = groups_to_mask(L, 2);
    return 0;
}

static int lua_sprite_setGroupMask(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->group_mask = (uint32_t)luaL_checkinteger(L, 2);
    return 0;
}

static int lua_sprite_getGroupMask(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    lua_pushinteger(L, s ? (lua_Integer)s->group_mask : 0);
    return 1;
}

static int lua_sprite_setCollidesWithGroupsMask(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->collides_mask = (uint32_t)luaL_checkinteger(L, 2);
    return 0;
}

static int lua_sprite_getCollidesWithGroupsMask(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    lua_pushinteger(L, s ? (lua_Integer)s->collides_mask : 0);
    return 1;
}

static int lua_sprite_resetGroupMask(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (s) s->group_mask = 0;
    return 0;
}

static int lua_sprite_resetCollidesWithGroupsMask(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (s) s->collides_mask = 0;
    return 0;
}

/* Group filter: collision is skipped only when the moving sprite has a
   collides-with mask, the other sprite belongs to groups, and they don't
   intersect (matches device behavior: unset masks collide with anything). */
static int groups_can_collide(LCDSprite *a, LCDSprite *b) {
    if (a->collides_mask && b->group_mask && !(a->collides_mask & b->group_mask)) return 0;
    return 1;
}

static int lua_sprite_setCollideRect(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (lua_isuserdata(L, 2)) {
        PDRectInternal *r = luaL_checkudata(L, 2, "pd.rect");
        s->collide_rect = *r;
        s->has_collide_rect = 1;
        return 0;
    }
    PDRectInternal r;
    r.x = (float)luaL_optnumber(L, 2, 0);
    r.y = (float)luaL_optnumber(L, 3, 0);
    r.width = (float)luaL_optnumber(L, 4, 0);
    r.height = (float)luaL_optnumber(L, 5, 0);
    s->collide_rect = r;
    s->has_collide_rect = 1;
    return 0;
}

static int lua_sprite_getCollideRect(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    PDRectInternal *r = lua_newuserdata(L, sizeof(PDRectInternal));
    *r = s->has_collide_rect ? s->collide_rect : (PDRectInternal){s->x, s->y, s->width, s->height};
    luaL_getmetatable(L, "pd.rect");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_sprite_clearCollideRect(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->has_collide_rect = 0;
    return 0;
}

static int lua_sprite_getCollideBounds(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    if (!s->has_collide_rect) {
        lua_pushnumber(L, s->x - s->width * s->center_x);
        lua_pushnumber(L, s->y - s->height * s->center_y);
        lua_pushnumber(L, s->width);
        lua_pushnumber(L, s->height);
    } else {
        lua_pushnumber(L, s->x - s->width * s->center_x + s->collide_rect.x);
        lua_pushnumber(L, s->y - s->height * s->center_y + s->collide_rect.y);
        lua_pushnumber(L, s->collide_rect.width);
        lua_pushnumber(L, s->collide_rect.height);
    }
    return 4;
}

static int lua_sprite_setStencilImage(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (lua_isnil(L, 2)) {
        s->stencil = NULL;
        s->has_stencil = 0;
    } else {
        LCDBitmap **ud = luaL_testudata(L, 2, "pd.bitmap");
        s->stencil = ud ? *ud : NULL;
        s->has_stencil = 1;
    }
    return 0;
}

static int lua_sprite_clearStencil(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->stencil = NULL;
    s->has_stencil = 0;
    return 0;
}

static int lua_sprite_setClipRect(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (lua_isuserdata(L, 2)) {
        PDRectInternal *r = luaL_checkudata(L, 2, "pd.rect");
        s->clip_rect = *r;
        s->has_clip_rect = 1;
        return 0;
    }
    PDRectInternal r;
    r.x = (float)luaL_optnumber(L, 2, 0);
    r.y = (float)luaL_optnumber(L, 3, 0);
    r.width = (float)luaL_optnumber(L, 4, 0);
    r.height = (float)luaL_optnumber(L, 5, 0);
    s->clip_rect = r;
    s->has_clip_rect = 1;
    return 0;
}

static int lua_sprite_clearClipRect(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->has_clip_rect = 0;
    return 0;
}

static int lua_sprite_setIgnoresDrawOffset(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->ignores_draw_offset = (int)lua_toboolean(L, 2);
    return 0;
}

static int lua_sprite_markDirty(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    s->dirty = (int)lua_toboolean(L, 2);
    return 0;
}

static void set_lua_ref(LCDSprite *s, int which, lua_State *L, int index) {
    int *ref_slot = which == 0 ? &s->lua_update_ref
                  : which == 1 ? &s->lua_draw_ref
                  : &s->lua_collision_ref;
    if (*ref_slot != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, *ref_slot);
    if (lua_isfunction(L, index)) {
        lua_pushvalue(L, index);
        *ref_slot = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        *ref_slot = LUA_NOREF;
    }
}

static int lua_sprite_setUpdateFunction(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    set_lua_ref(s, 0, L, 2);
    return 0;
}

static int lua_sprite_setDrawFunction(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    set_lua_ref(s, 1, L, 2);
    return 0;
}

static int lua_sprite_setCollisionResponseFunction(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    set_lua_ref(s, 2, L, 2);
    return 0;
}

/* World-space collide rect: bounds origin + local collide rect offset. */
static void sprite_world_collide_rect(LCDSprite *s, float x, float y,
                                      float *out_l, float *out_t, float *out_w, float *out_h) {
    float left = x - s->width * s->center_x;
    float top = y - s->height * s->center_y;
    if (s->has_collide_rect) {
        *out_l = left + s->collide_rect.x;
        *out_t = top + s->collide_rect.y;
        *out_w = s->collide_rect.width;
        *out_h = s->collide_rect.height;
    } else {
        *out_l = left;
        *out_t = top;
        *out_w = s->width;
        *out_h = s->height;
    }
}

/* Ask the sprite how to respond to colliding with `other`.
   Checks setCollisionResponseFunction ref, then the `collisionResponse`
   method on the sprite table. Defaults to freeze (Playdate default). */
static int sprite_collision_response(lua_State *L, LCDSprite *s, LCDSprite *other) {
    int response = kCollisionTypeFreeze;
    int found = 0;
    if (s->lua_collision_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->lua_collision_ref);
        if (lua_isfunction(L, -1)) {
            push_sprite_table(L, s);
            push_sprite_table(L, other);
            if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
                found = 1;
            } else {
                fprintf(stderr, "[collisionResponse] %s\n", lua_tostring(L, -1));
            }
        } else {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
    } else {
        push_sprite_table(L, s);
        lua_getfield(L, -1, "collisionResponse");
        if (lua_isnumber(L, -1) || lua_isstring(L, -1)) {
            lua_remove(L, -2);
            found = 1;
        } else if (lua_isfunction(L, -1) && lua_tocfunction(L, -1) == NULL) {
            lua_pushvalue(L, -2);
            push_sprite_table(L, other);
            if (lua_pcall(L, 2, 1, 0) == LUA_OK) {
                found = 1;
                lua_remove(L, -2);
            } else {
                fprintf(stderr, "[collisionResponse] %s\n", lua_tostring(L, -1));
                lua_remove(L, -2);
            }
        } else {
            lua_pop(L, 1);
        }
    }
    if (found) {
        if (lua_isnumber(L, -1)) {
            response = (int)lua_tointeger(L, -1);
        } else if (lua_isstring(L, -1)) {
            const char *t = lua_tostring(L, -1);
            if (strcmp(t, "slide") == 0) response = kCollisionTypeSlide;
            else if (strcmp(t, "freeze") == 0) response = kCollisionTypeFreeze;
            else if (strcmp(t, "overlap") == 0) response = kCollisionTypeOverlap;
            else if (strcmp(t, "bounce") == 0) response = kCollisionTypeBounce;
        }
    }
    lua_pop(L, 1);
    return response;
}

static int rects_overlap(float ax, float ay, float aw, float ah,
                         float bx, float by, float bw, float bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

int pd_sprite_forced_draw = 0;

#define MAX_COLLISIONS 32

static int sprite_move_with_collisions(lua_State *L, int commit) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    float goal_x = s->x;
    float goal_y = s->y;
    if (lua_isuserdata(L, 2)) {
        PDPoint *p = luaL_checkudata(L, 2, "pd.point");
        goal_x = p->x; goal_y = p->y;
    } else {
        if (lua_isnumber(L, 2)) goal_x = (float)lua_tonumber(L, 2);
        if (lua_isnumber(L, 3)) goal_y = (float)lua_tonumber(L, 3);
    }

    if (getenv("PD_TRACE")) {
        float dl, dt2, dw, dh;
        sprite_world_collide_rect(s, goal_x, goal_y, &dl, &dt2, &dw, &dh);
        fprintf(stderr, "[mwc %g,%g -> %g,%g cr=%g,%g %gx%g en=%d has=%d]",
                s->x, s->y, goal_x, goal_y, dl, dt2, dw, dh, s->collisions_enabled, s->has_collide_rect);
    }
    LCDSprite *hits[MAX_COLLISIONS];
    float normals_x[MAX_COLLISIONS], normals_y[MAX_COLLISIONS];
    int types[MAX_COLLISIONS];
    int nhits = 0;

    if (!s->collisions_enabled || !s->has_collide_rect) {
        if (commit) {
            s->x = goal_x;
            s->y = goal_y;
            sprite_sync_fields(L, s);
        }
        lua_pushnumber(L, commit ? s->x : goal_x);
        lua_pushnumber(L, commit ? s->y : goal_y);
        lua_newtable(L);
        lua_pushinteger(L, 0);
        return 4;
    }

    float sw, sh, sl, st;
    sprite_world_collide_rect(s, s->x, s->y, &sl, &st, &sw, &sh);
    float cur_x = s->x, cur_y = s->y;

    /* Swept movement: advance in sub-steps no larger than half the collide
       rect so fast movers can't tunnel through thin walls. Each step
       resolves X then Y; a blocked axis stops contributing (slide). */
    float min_dim = (sw > 0 && sh > 0) ? (sw < sh ? sw : sh) : 8.0f;
    float max_step = min_dim * 0.5f;
    if (max_step < 1.0f) max_step = 1.0f;
    if (max_step > 8.0f) max_step = 8.0f;
    float rem_x = goal_x - cur_x, rem_y = goal_y - cur_y;
    int guard = 0;

    while ((fabsf(rem_x) > 0.0001f || fabsf(rem_y) > 0.0001f) && guard++ < 512) {
        float step_x = rem_x, step_y = rem_y;
        if (step_x > max_step) step_x = max_step;
        if (step_x < -max_step) step_x = -max_step;
        if (step_y > max_step) step_y = max_step;
        if (step_y < -max_step) step_y = -max_step;

        for (int axis = 0; axis < 2; axis++) {
            float want = axis == 0 ? cur_x + step_x : cur_y + step_y;
            float try_x = axis == 0 ? want : cur_x;
            float try_y = axis == 0 ? cur_y : want;
            if (axis == 0 && step_x == 0) continue;
            if (axis == 1 && step_y == 0) continue;
            sprite_world_collide_rect(s, try_x, try_y, &sl, &st, &sw, &sh);

            for (int i = 0; i < g_sprite_count; i++) {
                LCDSprite *other = g_sprites[i];
                if (other == s || !other->collisions_enabled || !other->has_collide_rect) continue;
                if (!groups_can_collide(s, other)) continue;
                float ol, ot, ow, oh;
                sprite_world_collide_rect(other, other->x, other->y, &ol, &ot, &ow, &oh);
                if (ow <= 0 || oh <= 0) continue;
                if (!rects_overlap(sl, st, sw, sh, ol, ot, ow, oh)) continue;

                int response = sprite_collision_response(L, s, other);

                /* record collision (dedupe by other) */
                int seen = 0;
                for (int k = 0; k < nhits; k++)
                    if (hits[k] == other) { seen = 1; break; }
                if (!seen && nhits < MAX_COLLISIONS) {
                    hits[nhits] = other;
                    types[nhits] = response;
                    if (axis == 0)
                        { normals_x[nhits] = (try_x > cur_x) ? -1.0f : 1.0f; normals_y[nhits] = 0; }
                    else
                        { normals_x[nhits] = 0; normals_y[nhits] = (try_y > cur_y) ? -1.0f : 1.0f; }
                    nhits++;
                }

                if (response == kCollisionTypeOverlap) continue;

                /* push back so rects touch on this axis */
                if (axis == 0) {
                    if (try_x > cur_x) try_x -= (sl + sw) - ol;
                    else try_x += (ol + ow) - sl;
                } else {
                    if (try_y > cur_y) try_y -= (st + sh) - ot;
                    else try_y += (ot + oh) - st;
                }
                if (response == kCollisionTypeFreeze) { rem_x = 0; rem_y = 0; }
                sprite_world_collide_rect(s, try_x, try_y, &sl, &st, &sw, &sh);
            }

            if (axis == 0) {
                float advanced = try_x - cur_x;
                cur_x = try_x;
                rem_x -= step_x;
                if (fabsf(advanced - step_x) > 0.0001f) rem_x = 0; /* blocked */
            } else {
                float advanced = try_y - cur_y;
                cur_y = try_y;
                rem_y -= step_y;
                if (fabsf(advanced - step_y) > 0.0001f) rem_y = 0; /* blocked */
            }
        }
    }

    if (commit) {
        s->x = cur_x;
        s->y = cur_y;
        sprite_sync_fields(L, s);
    }

    lua_pushnumber(L, cur_x);
    lua_pushnumber(L, cur_y);
    lua_newtable(L);
    for (int k = 0; k < nhits; k++) {
        lua_newtable(L);
        push_sprite_table(L, s);
        lua_setfield(L, -2, "sprite");
        push_sprite_table(L, hits[k]);
        lua_setfield(L, -2, "other");
        lua_pushinteger(L, types[k]);
        lua_setfield(L, -2, "type");
        pd_push_vector2D(L, normals_x[k], normals_y[k]);
        lua_setfield(L, -2, "normal");
        lua_pushboolean(L, types[k] == kCollisionTypeOverlap);
        lua_setfield(L, -2, "overlaps");
        {
            float rl, rt, rw, rh;
            sprite_world_collide_rect(s, cur_x, cur_y, &rl, &rt, &rw, &rh);
            pd_push_rect(L, rl, rt, rw, rh);
            lua_setfield(L, -2, "spriteRect");
            sprite_world_collide_rect(hits[k], hits[k]->x, hits[k]->y, &rl, &rt, &rw, &rh);
            pd_push_rect(L, rl, rt, rw, rh);
            lua_setfield(L, -2, "otherRect");
            pd_push_point(L, cur_x, cur_y);
            lua_setfield(L, -2, "touch");
            pd_push_vector2D(L, cur_x - goal_x, cur_y - goal_y);
            lua_setfield(L, -2, "move");
            lua_pushnumber(L, 0);
            lua_setfield(L, -2, "ti");
        }
        lua_rawseti(L, -2, k + 1);
    }
    if (getenv("PD_TRACE")) fprintf(stderr, "[mwc-hits %d]", nhits);
    lua_pushinteger(L, nhits);
    return 4;
}

static int lua_sprite_moveWithCollisions(lua_State *L) {
    return sprite_move_with_collisions(L, 1);
}

static int lua_sprite_checkCollisions(lua_State *L) {
    return sprite_move_with_collisions(L, 0);
}

static int lua_sprite_overlappingSprites(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    lua_newtable(L);
    if (!s || !s->collisions_enabled) return 1;
    float sl, st, sw, sh;
    sprite_world_collide_rect(s, s->x, s->y, &sl, &st, &sw, &sh);
    int idx = 1;
    for (int i = 0; i < g_sprite_count; i++) {
        LCDSprite *o = g_sprites[i];
        if (o == s || !o->collisions_enabled) continue;
        if (!groups_can_collide(s, o)) continue;
        float ol, ot, ow, oh;
        sprite_world_collide_rect(o, o->x, o->y, &ol, &ot, &ow, &oh);
        if (ow <= 0 || oh <= 0) continue;
        if (rects_overlap(sl, st, sw, sh, ol, ot, ow, oh)) {
            push_sprite_table(L, o);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

static int lua_sprite_allOverlappingSprites(lua_State *L) {
    lua_newtable(L);
    return 1;
}

static int lua_sprite_querySpritesAtPoint(lua_State *L) {
    float x, y;
    PDPoint *pt = luaL_testudata(L, 1, "pd.point");
    if (pt) { x = pt->x; y = pt->y; }
    else {
        x = (float)luaL_checknumber(L, 1);
        y = (float)luaL_checknumber(L, 2);
    }
    lua_newtable(L);
    int idx = 1;
    for (int i = 0; i < g_sprite_count; i++) {
        LCDSprite *s = g_sprites[i];
        if (!s->collisions_enabled) continue;
        float sl, st, sw, sh;
        sprite_world_collide_rect(s, s->x, s->y, &sl, &st, &sw, &sh);
        if (sw <= 0 || sh <= 0) continue;
        if (x >= sl && x < sl + sw && y >= st && y < st + sh) {
            push_sprite_table(L, s);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

static int lua_sprite_querySpritesInRect(lua_State *L) {
    float x, y, w, h;
    PDRectInternal *r = luaL_testudata(L, 1, "pd.rect");
    if (r) {
        x = r->x; y = r->y; w = r->width; h = r->height;
    } else {
        x = (float)luaL_checknumber(L, 1);
        y = (float)luaL_checknumber(L, 2);
        w = (float)luaL_checknumber(L, 3);
        h = (float)luaL_checknumber(L, 4);
    }
    lua_newtable(L);
    int idx = 1;
    for (int i = 0; i < g_sprite_count; i++) {
        LCDSprite *s = g_sprites[i];
        if (!s->collisions_enabled) continue;
        float sl, st, sw, sh;
        sprite_world_collide_rect(s, s->x, s->y, &sl, &st, &sw, &sh);
        if (sw <= 0 || sh <= 0) continue;
        if (rects_overlap(x, y, w, h, sl, st, sw, sh)) {
            push_sprite_table(L, s);
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

/* Strong refs for sprites in the display list: the weak sprite map alone
   lets GC collect sprite tables (and their user fields) once the game drops
   its own references, even though the sprite is still active on screen.
   Real SDK retains sprites while added; mirror that here. */
#define SPRITE_LIVE_KEY "pd.sprite.live"

static void get_sprite_live(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, SPRITE_LIVE_KEY);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, SPRITE_LIVE_KEY);
    }
}

static void sprite_live_retain(lua_State *L, LCDSprite *s) {
    get_sprite_live(L);
    lua_pushlightuserdata(L, s);
    push_sprite_table(L, s);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static void sprite_live_release(lua_State *L, LCDSprite *s) {
    get_sprite_live(L);
    lua_pushlightuserdata(L, s);
    lua_pushnil(L);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

static void sprite_live_release_all(lua_State *L) {
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, SPRITE_LIVE_KEY);
}

static int lua_sprite_add(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (!s->in_list) {
        ensure_sprite_capacity(g_sprite_count + 1);
        {
            static uint32_t add_counter = 0;
            s->add_seq = ++add_counter;
        }
        g_sprites[g_sprite_count++] = s;
        s->in_list = 1;
    }
    sprite_live_retain(L, s);
    return 0;
}

static void sprite_vacate(LCDSprite *s);

static int lua_sprite_remove(lua_State *L) {
    if (lua_isnil(L, 1) || lua_gettop(L) >= 2) {
        for (int i = 0; i < g_sprite_count; i++) {
            g_sprites[i]->in_list = 0;
            sprite_vacate(g_sprites[i]);
        }
        g_sprite_count = 0;
        sprite_live_release_all(L);
        return 0;
    }
    LCDSprite *s = check_sprite(L, 1);
    if (!s) return 0;
    if (s->in_list) {
        for (int i = 0; i < g_sprite_count; i++) {
            if (g_sprites[i] == s) {
                g_sprites[i] = g_sprites[--g_sprite_count];
                break;
            }
        }
        s->in_list = 0;
        sprite_vacate(s);
    }
    sprite_live_release(L, s);
    return 0;
}

static int lua_sprite_removeAll(lua_State *L) {
    for (int i = 0; i < g_sprite_count; i++) {
        g_sprites[i]->in_list = 0;
        sprite_vacate(g_sprites[i]);
    }
    g_sprite_count = 0;
    sprite_live_release_all(L);
    return 0;
}

/* sprite.removeSprites(spriteArray): remove every sprite in the list */
static int lua_sprite_removeSprites(lua_State *L) {
    int t = lua_istable(L, 1) ? 1 : (lua_istable(L, 2) ? 2 : 0);
    if (!t) return 0;
    lua_Integer n = luaL_len(L, t);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, t, i);
        LCDSprite *s = check_sprite(L, lua_gettop(L));
        if (s && s->in_list) {
            for (int j = 0; j < g_sprite_count; j++) {
                if (g_sprites[j] == s) {
                    g_sprites[j] = g_sprites[--g_sprite_count];
                    break;
                }
            }
            s->in_list = 0;
            sprite_vacate(s);
            sprite_live_release(L, s);
        }
        lua_pop(L, 1);
    }
    return 0;
}

static int lua_sprite_getSpriteCount(lua_State *L) {
    lua_pushinteger(L, g_sprite_count);
    return 1;
}

static int lua_sprite_getAllSprites(lua_State *L) {
    lua_newtable(L);
    int idx = 1;
    for (int i = 0; i < g_sprite_count; i++) {
        push_sprite_table(L, g_sprites[i]);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

static void call_lua_ref(lua_State *L, LCDSprite *s, int ref) {
    int top=lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (lua_isfunction(L, -1)) {
        push_sprite_table(L, s);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            fprintf(stderr, "[sprite ref] %s\n", lua_tostring(L, -1));
        }
    }
    lua_settop(L,top);
}

extern void pd_draw_bitmap_at(LCDBitmap *bm, int x, int y, LCDBitmapFlip flip, LCDBitmapDrawMode mode);
extern void pd_draw_bitmap_transformed(LCDBitmap *bm, float cx, float cy, float angle_deg, float scale, LCDBitmapFlip flip, LCDBitmapDrawMode mode);

static int sprite_cmp(const void *a, const void *b) {
    const LCDSprite *sa = *(const LCDSprite **)a;
    const LCDSprite *sb = *(const LCDSprite **)b;
    if (sa->z_index < sb->z_index) return -1;
    if (sa->z_index > sb->z_index) return 1;
    /* SDK: equal z-index draws in add order (qsort alone is unstable) */
    if (sa->add_seq < sb->add_seq) return -1;
    if (sa->add_seq > sb->add_seq) return 1;
    return 0;
}

int g_sprites_drawn_this_frame = 0;
int pd_sprite_draw_only = 0;

#define PD_BG_CALLBACK_KEY "pd.sprite.bgcallback"

static int lua_sprite_setBackgroundDrawingCallback(lua_State *L) {
    if (lua_isfunction(L, 1))
        lua_pushvalue(L, 1);
    else
        lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, PD_BG_CALLBACK_KEY);
    return 0;
}

/* Screen-space bounds a sprite paints (includes rotation/scale extent and
   the global draw offset). */
static void sprite_screen_bounds(LCDSprite *s, float *ox, float *oy,
                                 float *ow, float *oh) {
    float x = s->x - s->width * s->center_x;
    float y = s->y - s->height * s->center_y;
    float w = s->width, h = s->height;
    if (s->rotation != 0.0f || s->scale != 1.0f) {
        float hw = s->width * s->scale * 0.5f;
        float hh = s->height * s->scale * 0.5f;
        float ext = sqrtf(hw * hw + hh * hh) + 1.0f;
        x = s->x - ext;
        y = s->y - ext;
        w = ext * 2;
        h = ext * 2;
    }
    if (!s->ignores_draw_offset) {
        x += g_pd.draw_offset_x;
        y += g_pd.draw_offset_y;
    }
    *ox = x; *oy = y; *ow = w; *oh = h;
}

/* Rects vacated by removed sprites, repainted on the next sprite pass. */
#define PD_VACATED_MAX 128
static float g_vacated[PD_VACATED_MAX][4];
static int g_vacated_count = 0;
static int g_vacated_overflow = 0;

static void sprite_vacate(LCDSprite *s) {
    if (!s->was_drawn) return;
    s->was_drawn = 0;
    if (g_vacated_count >= PD_VACATED_MAX) {
        g_vacated_overflow = 1;
        return;
    }
    float *r = g_vacated[g_vacated_count++];
    r[0] = s->last_x; r[1] = s->last_y; r[2] = s->last_w; r[3] = s->last_h;
}

static int lua_sprite_updateAndDraw(lua_State *L) {
    g_sprites_drawn_this_frame = 1;
    g_pd.clip_enabled = 0;
    LCDSprite **sorted = NULL;
    int n = g_sprite_count; /* snapshot: callbacks may add/remove sprites */
    if (n > 0) {
        sorted = malloc((size_t)n * sizeof(LCDSprite *));
        memcpy(sorted, g_sprites, (size_t)n * sizeof(LCDSprite *));
        qsort(sorted, (size_t)n, sizeof(LCDSprite *), sprite_cmp);
    }
    for (int i = 0; pd_sprite_draw_only == 0 && i < n; i++) {
        LCDSprite *s = sorted[i];
        if (!s->in_list) continue; /* removed during an earlier callback */
        if (!s->updates_enabled) continue;
        if (s->lua_update_ref != LUA_NOREF) {
            call_lua_ref(L, s, s->lua_update_ref);
        } else {
            push_sprite_table(L, s);
            lua_getfield(L, -1, "update");
            if (lua_tocfunction(L, -1) != NULL) {
                lua_pop(L, 2);
                continue;
            }
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[sprite update] %s\n", lua_tostring(L, -1));
#ifdef CRANK_ESP32
                    pd_host_fail(lua_tostring(L,-1));
#endif
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
    }
    static int trace = -1;
    if (trace == -1) trace = getenv("PD_TRACE") ? 1 : 0;

    /* Real hardware only repaints dirty regions, so UI a game draws
       directly is not wiped by a later sprite pass. Approximate that with
       a damage mask: pixels the game drew this frame are preserved. */
    pd_damage_protect = 1;
    lua_getfield(L, LUA_REGISTRYINDEX, PD_BG_CALLBACK_KEY);
    int has_bg_cb = lua_isfunction(L, -1);
    lua_pop(L, 1);
    if (!pd_sprite_forced_draw) {
        if (has_bg_cb || g_vacated_overflow) {
            /* a bg callback repaints everything under the sprites anyway */
            extern void pd_clear_display_bg(void);
            pd_clear_display_bg();
            g_vacated_overflow = 0;
            g_vacated_count = 0;
        } else {
            /* Regional repaint: only clear where sprites were or will be,
               so content the game drew directly persists across frames
               (matches the device's dirty-rect behavior). */
            for (int i = 0; i < g_vacated_count; i++) {
                float *r = g_vacated[i];
                pd_clear_display_bg_rect((int)r[0], (int)r[1],
                                         (int)(r[0] + r[2] + 1), (int)(r[1] + r[3] + 1));
            }
            g_vacated_count = 0;
            for (int i = 0; i < n; i++) {
                LCDSprite *s = sorted[i];
                if (s->was_drawn)
                    pd_clear_display_bg_rect((int)s->last_x, (int)s->last_y,
                                             (int)(s->last_x + s->last_w + 1),
                                             (int)(s->last_y + s->last_h + 1));
                if (s->in_list && s->visible) {
                    float bx, by, bw, bh;
                    sprite_screen_bounds(s, &bx, &by, &bw, &bh);
                    pd_clear_display_bg_rect((int)bx, (int)by,
                                             (int)(bx + bw + 1), (int)(by + bh + 1));
                }
            }
        }
    }

    /* Background drawing callback runs under the sprites. */
    lua_getfield(L, LUA_REGISTRYINDEX, PD_BG_CALLBACK_KEY);
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, PD_SCREEN_WIDTH);
        lua_pushinteger(L, PD_SCREEN_HEIGHT);
        if (lua_pcall(L, 4, 0, 0) != LUA_OK) {
            fprintf(stderr, "[sprite bg] %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    /* When the game itself calls sprite.update() mid-frame, sprites draw
       ON TOP of whatever the game already drew (device behavior). The
       damage mask only protects game drawing from the runtime's forced
       end-of-frame pass and from the background clear above. */
    pd_damage_protect = pd_sprite_forced_draw;
    for (int i = 0; i < n; i++) {
        LCDSprite *s = sorted[i];
        if (!s->in_list) continue; /* removed during update callbacks */
        if (trace) fprintf(stderr, "[S z=%d v=%d img=%p %gx%g@%g,%g]", s->z_index, s->visible, (void *)s->image, s->width, s->height, s->x, s->y);
        if (!s->visible) {
            if (!pd_sprite_forced_draw) s->was_drawn = 0;
            continue;
        }
        int saved_ox = g_pd.draw_offset_x, saved_oy = g_pd.draw_offset_y;
        if (s->ignores_draw_offset) {
            g_pd.draw_offset_x = 0;
            g_pd.draw_offset_y = 0;
        }
        float draw_x = s->x - s->width * s->center_x;
        float draw_y = s->y - s->height * s->center_y;
        LCDBitmap *saved_stencil = pd_gfx_get_stencil();
        if (s->has_stencil && s->stencil)
            pd_gfx_set_stencil(s->stencil);
        if (s->lua_draw_ref != LUA_NOREF) {
            /* draw callbacks run with the origin at the sprite's top-left */
            g_pd.draw_offset_x += (int)draw_x;
            g_pd.draw_offset_y += (int)draw_y;
            call_lua_ref(L, s, s->lua_draw_ref);
        } else if (s->image) {
            if (s->rotation != 0.0f || s->scale != 1.0f)
                pd_draw_bitmap_transformed(s->image, s->x, s->y, s->rotation,
                                           s->scale, s->flip, s->draw_mode);
            else
                pd_draw_bitmap_at(s->image, (int)draw_x, (int)draw_y, s->flip, s->draw_mode);
        } else {
            push_sprite_table(L, s);
            lua_getfield(L, -1, "_pd_tilemap");
            if (lua_istable(L, -1)) {
                lua_getfield(L, -1, "draw");
                if (lua_isfunction(L, -1)) {
                    lua_pushvalue(L, -2);
                    lua_pushnumber(L, draw_x);
                    lua_pushnumber(L, draw_y);
                    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                        fprintf(stderr, "[sprite tilemap] %s\n", lua_tostring(L, -1));
                        lua_pop(L, 1);
                    }
                } else {
                    lua_pop(L, 1);
                }
                lua_pop(L, 2);
                g_pd.draw_offset_x = saved_ox;
                g_pd.draw_offset_y = saved_oy;
                continue;
            }
            lua_pop(L, 1);
            lua_getfield(L, -1, "draw");
            if (lua_isfunction(L, -1) && lua_tocfunction(L, -1) == NULL) {
                /* draw callbacks run with the origin at the sprite's top-left */
                g_pd.draw_offset_x += (int)draw_x;
                g_pd.draw_offset_y += (int)draw_y;
                lua_pushvalue(L, -2);
                lua_pushinteger(L, 0);
                lua_pushinteger(L, 0);
                lua_pushinteger(L, (lua_Integer)s->width);
                lua_pushinteger(L, (lua_Integer)s->height);
                if (lua_pcall(L, 5, 0, 0) != LUA_OK) {
                    fprintf(stderr, "[sprite draw] %s\n", lua_tostring(L, -1));
#ifdef CRANK_ESP32
                    pd_host_fail(lua_tostring(L,-1));
#endif
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        }
        g_pd.draw_offset_x = saved_ox;
        g_pd.draw_offset_y = saved_oy;
        pd_gfx_set_stencil(saved_stencil);
        if (!pd_sprite_forced_draw) {
            sprite_screen_bounds(s, &s->last_x, &s->last_y, &s->last_w, &s->last_h);
            s->was_drawn = 1;
        }
    }
    pd_damage_protect = 0;
    static int dbg_rects = -1;
    if (dbg_rects == -1) dbg_rects = getenv("PD_DEBUG_RECTS") ? 1 : 0;
    if (dbg_rects) {
        extern void pd_debug_draw_rect(int x, int y, int w, int h);
        for (int i = 0; i < n; i++) {
            LCDSprite *s = sorted[i];
            if (!s->in_list || !s->collisions_enabled || !s->has_collide_rect) continue;
            float l, t, w, h;
            sprite_world_collide_rect(s, s->x, s->y, &l, &t, &w, &h);
            pd_debug_draw_rect((int)l + g_pd.draw_offset_x, (int)t + g_pd.draw_offset_y,
                               (int)w, (int)h);
        }
    }
    free(sorted);
    return 0;
}

static int lua_sprite_update(lua_State *L) {
    /* Called as instance method (spr:update()) it is a per-sprite no-op;
       called statically (gfx.sprite.update()) it updates AND draws all
       sprites, matching real Playdate behavior. */
    if (lua_gettop(L) >= 1 && check_sprite(L, 1))
        return 0;
    return lua_sprite_updateAndDraw(L);
}

static int lua_sprite_drawSprites(lua_State *L) {
    return lua_sprite_updateAndDraw(L);
}

static int lua_sprite_setAlwaysRedraw(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_sprite_addDirtyRect(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_sprite_resetCollisionWorld(lua_State *L) {
    (void)L;
    return 0;
}

static LCDSprite *sprite_alloc_defaults(void) {
    LCDSprite *s = calloc(1, sizeof(LCDSprite));
    if(g_allocated_count==g_allocated_capacity) {
        g_allocated_capacity=g_allocated_capacity ? g_allocated_capacity*2 : 64;
        g_allocated=realloc(g_allocated,g_allocated_capacity*sizeof(*g_allocated));
    }
    g_allocated[g_allocated_count++]=s;
    s->x = 0; s->y = 0;
    s->width = 0; s->height = 0;
    s->center_x = 0.5f; s->center_y = 0.5f;
    s->visible = 1;
    s->updates_enabled = 1;
    s->collisions_enabled = 1;
    s->z_index = 0;
    s->flip = kBitmapUnflipped;
    s->draw_mode = kDrawModeCopy;
    s->rotation = 0;
    s->scale = 1.0f;
    s->lua_update_ref = LUA_NOREF;
    s->lua_draw_ref = LUA_NOREF;
    s->lua_collision_ref = LUA_NOREF;
    s->lua_image_ref = LUA_NOREF;
    s->in_list = 0;
    return s;
}

/* sprite:copy() -> new sprite duplicating all C-side properties and any
   custom Lua fields (CoreLibs wraps this and re-applies the metatable). */
static int lua_sprite_copy(lua_State *L) {
    LCDSprite *s = check_sprite(L, 1);
    if (!s) { lua_pushnil(L); return 1; }
    LCDSprite *c = sprite_alloc_defaults();
    *c = *s;
    c->in_list = 0;
    c->free_flag = 0;
    c->lua_update_ref = LUA_NOREF;
    c->lua_draw_ref = LUA_NOREF;
    c->lua_collision_ref = LUA_NOREF;
    c->lua_image_ref = LUA_NOREF;
    if (s->lua_update_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->lua_update_ref);
        c->lua_update_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (s->lua_draw_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->lua_draw_ref);
        c->lua_draw_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (s->lua_collision_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->lua_collision_ref);
        c->lua_collision_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (s->lua_image_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->lua_image_ref);
        c->lua_image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    push_sprite_table(L, c);
    if (lua_istable(L, 1)) { /* carry over custom instance fields */
        lua_pushnil(L);
        while (lua_next(L, 1)) {
            if (!(lua_type(L, -2) == LUA_TSTRING &&
                  strcmp(lua_tostring(L, -2), "_userdata") == 0)) {
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_settable(L, -5);
            }
            lua_pop(L, 1);
        }
    }
    sprite_sync_fields(L, c);
    return 1;
}

static int lua_sprite_init(lua_State *L) {
    if (!lua_istable(L, 1)) return 0;
    if (check_sprite(L, 1)) return 0;
    LCDSprite *s = sprite_alloc_defaults();
    attach_sprite_userdata(L, s, 1);
    LCDBitmap **bm = lua_gettop(L) >= 2 ? luaL_testudata(L, 2, "pd.bitmap") : NULL;
    if (bm && *bm) {
        s->image = *bm;
        s->width = (*bm)->width;
        s->height = (*bm)->height;
        lua_pushvalue(L, 2);
        s->lua_image_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    sprite_sync_fields(L, s);
    return 0;
}

static const luaL_Reg sprite_methods[] = {
    {"init", lua_sprite_init},
    {"new", lua_sprite_new},
    {"copy", lua_sprite_copy},
    {"add", lua_sprite_add},
    {"addSprite", lua_sprite_add},
    {"remove", lua_sprite_remove},
    {"removeSprite", lua_sprite_remove},
    {"removeSprites", lua_sprite_removeSprites},
    {"removeAll", lua_sprite_removeAll},
    {"getSpriteCount", lua_sprite_getSpriteCount},
    {"getAllSprites", lua_sprite_getAllSprites},
    {"setBounds", lua_sprite_setBounds},
    {"getBounds", lua_sprite_getBounds},
    {"getBoundsRect", lua_sprite_getBoundsRect},
    {"moveTo", lua_sprite_moveTo},
    {"moveBy", lua_sprite_moveBy},
    {"getPosition", lua_sprite_getPosition},
    {"setSize", lua_sprite_setSize},
    {"getSize", lua_sprite_getSize},
    {"setCenter", lua_sprite_setCenter},
    {"getCenter", lua_sprite_getCenter},
    {"setImage", lua_sprite_setImage},
    {"setTilemap", lua_sprite_setTilemap},
    {"getImage", lua_sprite_getImage},
    {"setImageFlip", lua_sprite_setImageFlip},
    {"setImageDrawMode", lua_sprite_setImageDrawMode},
    {"setRotation", lua_sprite_setRotation},
    {"getRotation", lua_sprite_getRotation},
    {"setScale", lua_sprite_setScale},
    {"getScale", lua_sprite_getScale},
    {"setGroups", lua_sprite_setGroups},
    {"setGroupMask", lua_sprite_setGroupMask},
    {"getGroupMask", lua_sprite_getGroupMask},
    {"setCollidesWithGroups", lua_sprite_setCollidesWithGroups},
    {"setCollidesWithGroupsMask", lua_sprite_setCollidesWithGroupsMask},
    {"getCollidesWithGroupsMask", lua_sprite_getCollidesWithGroupsMask},
    {"resetGroupMask", lua_sprite_resetGroupMask},
    {"resetCollidesWithGroupsMask", lua_sprite_resetCollidesWithGroupsMask},
    {"getImageFlip", lua_sprite_getImageFlip},
    {"setVisible", lua_sprite_setVisible},
    {"isVisible", lua_sprite_isVisible},
    {"setUpdatesEnabled", lua_sprite_setUpdatesEnabled},
    {"updatesEnabled", lua_sprite_updatesEnabled},
    {"setZIndex", lua_sprite_setZIndex},
    {"getZIndex", lua_sprite_getZIndex},
    {"setTag", lua_sprite_setTag},
    {"getTag", lua_sprite_getTag},
    {"setOpaque", lua_sprite_setOpaque},
    {"isOpaque", lua_sprite_isOpaque},
    {"setCollisionsEnabled", lua_sprite_setCollisionsEnabled},
    {"setCollideRect", lua_sprite_setCollideRect},
    {"getCollideRect", lua_sprite_getCollideRect},
    {"clearCollideRect", lua_sprite_clearCollideRect},
    {"getCollideBounds", lua_sprite_getCollideBounds},
    {"setStencilImage", lua_sprite_setStencilImage},
    {"clearStencil", lua_sprite_clearStencil},
    {"setClipRect", lua_sprite_setClipRect},
    {"clearClipRect", lua_sprite_clearClipRect},
    {"setIgnoresDrawOffset", lua_sprite_setIgnoresDrawOffset},
    {"markDirty", lua_sprite_markDirty},
    {"setUpdateFunction", lua_sprite_setUpdateFunction},
    {"setDrawFunction", lua_sprite_setDrawFunction},
    {"setCollisionResponseFunction", lua_sprite_setCollisionResponseFunction},
    {"moveWithCollisions", lua_sprite_moveWithCollisions},
    {"checkCollisions", lua_sprite_checkCollisions},
    {"updateAndDrawSprites", lua_sprite_updateAndDraw},
    {"update", lua_sprite_update},
    {"drawSprites", lua_sprite_drawSprites},
    {"updateAndDraw", lua_sprite_updateAndDraw},
    {"setAlwaysRedraw", lua_sprite_setAlwaysRedraw},
    {"setBackgroundDrawingCallback", lua_sprite_setBackgroundDrawingCallback},
    {"addDirtyRect", lua_sprite_addDirtyRect},
    {"resetCollisionWorld", lua_sprite_resetCollisionWorld},
    {"overlappingSprites", lua_sprite_overlappingSprites},
    {"allOverlappingSprites", lua_sprite_allOverlappingSprites},
    {"querySpritesAtPoint", lua_sprite_querySpritesAtPoint},
    {"querySpritesInRect", lua_sprite_querySpritesInRect},
    {"free", lua_sprite_free},
    {NULL, NULL}
};

static int lua_sprite_table_index(lua_State *L) {
    if (lua_isstring(L, 2)) {
        const char *key = lua_tostring(L, 2);
        if (strcmp(key, "x") == 0 || strcmp(key, "y") == 0 ||
            strcmp(key, "width") == 0 || strcmp(key, "height") == 0) {
            LCDSprite *s = check_sprite(L, 1);
            if (s) {
                switch (key[0]) {
                    case 'x': lua_pushnumber(L, s->x); return 1;
                    case 'y': lua_pushnumber(L, s->y); return 1;
                    case 'w': lua_pushnumber(L, s->width); return 1;
                    default: lua_pushnumber(L, s->height); return 1;
                }
            }
        }
    }
    luaL_getmetatable(L, META_SPRITE);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

void pd_sprite_register(lua_State *L) {
    luaL_newmetatable(L, META_SPRITE);
    lua_pushcfunction(L, lua_sprite_table_index);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, sprite_methods, 0);
    int meta_abs = lua_gettop(L);

    lua_getglobal(L, "playdate");
    lua_getfield(L, -1, "graphics");
    int gfx_abs = lua_gettop(L);

    lua_pushinteger(L, kCollisionTypeSlide);
    lua_setfield(L, meta_abs, "kCollisionTypeSlide");
    lua_pushinteger(L, kCollisionTypeFreeze);
    lua_setfield(L, meta_abs, "kCollisionTypeFreeze");
    lua_pushinteger(L, kCollisionTypeOverlap);
    lua_setfield(L, meta_abs, "kCollisionTypeOverlap");
    lua_pushinteger(L, kCollisionTypeBounce);
    lua_setfield(L, meta_abs, "kCollisionTypeBounce");

    lua_pushvalue(L, meta_abs);
    lua_setfield(L, gfx_abs, "sprite");

    lua_pushvalue(L, meta_abs);
    g_sprite_table_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_pop(L, 3);
}

void pd_sprite_shutdown(void) {
    for(int i=0;i<g_allocated_count;i++) free(g_allocated[i]);
    free(g_allocated); g_allocated=NULL; g_allocated_count=g_allocated_capacity=0;
    free(g_sprites); g_sprites=NULL; g_sprite_count=g_sprite_capacity=0;
    g_sprite_table_ref=LUA_NOREF; g_vacated_count=g_vacated_overflow=0;
    g_sprites_drawn_this_frame=0;
}
void pd_sprite_native_release(lua_State *L,int index) {
    LCDSprite *s=check_sprite(L,index);if(!s)return;
    // Native ownership ends now; keep the struct alive until the frame's
    // callback/list snapshots have returned, then reclaim it below.
    s->free_flag=1;
    lua_getfield(L,index,"_userdata");LCDSprite **ud=luaL_testudata(L,-1,META_SPRITE);if(ud)*ud=NULL;lua_pop(L,1);
}
void pd_sprite_native_collect(lua_State *L) {
    int count=0;
    for(int i=0;i<g_allocated_count;i++) {
        LCDSprite *s=g_allocated[i];if(!s->free_flag){g_allocated[count++]=s;continue;}
        int refs[]={s->lua_update_ref,s->lua_draw_ref,s->lua_collision_ref,s->lua_image_ref};
        for(int j=0;j<4;j++)if(refs[j]!=LUA_NOREF)luaL_unref(L,LUA_REGISTRYINDEX,refs[j]);
        get_sprite_map(L);lua_pushlightuserdata(L,s);lua_pushnil(L);lua_rawset(L,-3);lua_pop(L,1);free(s);
    }
    g_allocated_count=count;
}
