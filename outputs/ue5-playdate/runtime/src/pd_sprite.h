#ifndef PD_SPRITE_H
#define PD_SPRITE_H

#include "pd_runtime.h"
#include "pd_geometry.h"

typedef enum {
    kCollisionTypeSlide = 0,
    kCollisionTypeFreeze = 1,
    kCollisionTypeOverlap = 2,
    kCollisionTypeBounce = 3
} SpriteCollisionResponseType;

typedef struct LCDSprite {
    float x, y;
    float width, height;
    float center_x, center_y;
    LCDBitmap *image;
    LCDBitmapFlip flip;
    int16_t z_index;
    int visible;
    int updates_enabled;
    int collisions_enabled;
    int opaque;
    int dirty;
    int ignores_draw_offset;
    uint8_t tag;
    PDRectInternal collide_rect;
    int has_collide_rect;
    PDRectInternal clip_rect;
    int has_clip_rect;
    LCDBitmapDrawMode draw_mode;
    float rotation;
    float scale;
    int lua_update_ref;
    int lua_draw_ref;
    int lua_collision_ref;
    int lua_image_ref;
    int in_list;
    int free_flag;
    uint32_t group_mask;
    uint32_t collides_mask;
    uint32_t add_seq;
    LCDBitmap *stencil;
    int has_stencil;
    /* last drawn screen-space bounds, for regional background repaint */
    float last_x, last_y, last_w, last_h;
    int was_drawn;
} LCDSprite;

void pd_sprite_register(lua_State *L);

#endif
