#pragma once
#include <stdint.h>

#define MENU_MAX_ITEMS 8
#define MENU_MAX_OPTIONS 16

typedef struct {
    int id, kind, value; /* kind 0 action, 1 checkmark (0/1), 2 options (1-based) */
    char title[96];
    int option_count;
    char options[MENU_MAX_OPTIONS][48];
} CrankMenuItem;

typedef struct {
    int count;
    CrankMenuItem items[MENU_MAX_ITEMS];
    int image_offset, has_image;
} CrankMenuModel;

/* Parses the JSON snapshot from crank_copy_menu. Returns 1 on success. */
int cranklab_menu_parse(const char *json, CrankMenuModel *model);

/* Optional CJK font (PFT). Safe to call repeatedly; a missing file keeps '?' fallbacks. */
void cranklab_menu_load_font(const char *path);
void cranklab_menu_free_font(void);

/* Rows are the game's items plus a final "Resume". Returns the row count. */
int cranklab_menu_rows(const CrankMenuModel *model);

/* Draws the menu over `background` (BGRA 400x240, grey = 1-bit picture) into XRGB pixels.
   `background` may be NULL for a blank page. */
void cranklab_menu_draw(uint32_t *pixels, const CrankMenuModel *model, int selected,
                        const uint8_t *background, int background_offset,
                        uint32_t black, uint32_t white);
