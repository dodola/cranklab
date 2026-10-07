/* Built-in 5x7 font and ROM selection menu.
 *
 * When the runtime is pointed at a directory containing a `roms/` folder,
 * pd_rom_menu() lists every *.pdx directory (or any directory with a
 * pdxinfo) inside it and lets the user pick one with d-pad/keys + A.
 */
#include "pd_runtime.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Classic public-domain 5x7 ASCII font, chars 32..126.
 * 5 column bytes per glyph, bit 0 = top row. */
static const uint8_t font5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /*   */ {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */ {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */ {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */ {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */ {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */ {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */ {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */ {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */ {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */ {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */ {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */ {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */ {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */ {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */ {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */ {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */ {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */ {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */ {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x01,0x01}, /* F */ {0x3E,0x41,0x41,0x51,0x32}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */ {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */ {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */ {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */ {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */ {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */ {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */ {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */ {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */ {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */ {0x00,0x00,0x7F,0x41,0x41}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */ {0x41,0x41,0x7F,0x00,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */ {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */ {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */ {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */ {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */ {0x08,0x14,0x54,0x54,0x3C}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */ {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */ {0x00,0x7F,0x10,0x28,0x44}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */ {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */ {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */ {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */ {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */ {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */ {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */ {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */ {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */ {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x08,0x08,0x2A,0x1C,0x08}, /* ~ */
};

const uint8_t *pd_font5x7_glyph(int c) {
    if (c < 32 || c > 126) return NULL;
    return font5x7[c - 32];
}

static void menu_set_pixel(LCDBitmap *bm, int x, int y, int black) {
    if (!bm || x < 0 || x >= bm->width || y < 0 || y >= bm->height) return;
    int byte_idx = y * bm->rowbytes + (x / 8);
    int bit_idx = 7 - (x % 8);
    if (black)
        bm->data[byte_idx] |= (uint8_t)(1 << bit_idx);
    else
        bm->data[byte_idx] &= (uint8_t)~(1 << bit_idx);
    if (bm->mask) bm->mask[byte_idx] |= (uint8_t)(1 << bit_idx);
}

void pd_builtin_text(LCDBitmap *bm, int x, int y, int scale, int black, const char *str) {
    if (scale < 1) scale = 1;
    int cx = x;
    for (const char *p = str; *p; p++) {
        if (*p == '\n') {
            cx = x;
            y += 8 * scale;
            continue;
        }
        const uint8_t *g = pd_font5x7_glyph((unsigned char)*p);
        if (g) {
            for (int col = 0; col < 5; col++) {
                for (int row = 0; row < 7; row++) {
                    if ((g[col] >> row) & 1) {
                        for (int sx = 0; sx < scale; sx++)
                            for (int sy = 0; sy < scale; sy++)
                                menu_set_pixel(bm, cx + col * scale + sx,
                                               y + row * scale + sy, black);
                    }
                }
            }
        }
        cx += 6 * scale;
    }
}

static void menu_fill_rect(LCDBitmap *bm, int x, int y, int w, int h, int black) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++)
            menu_set_pixel(bm, px, py, black);
}

typedef struct {
    char dir[512];
    char name[128];
} RomEntry;

static int rom_cmp(const void *a, const void *b) {
    return strcasecmp(((const RomEntry *)a)->name, ((const RomEntry *)b)->name);
}

static int rom_read_name(const char *dir, char *out, size_t out_sz) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/pdxinfo", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "name=", 5) == 0) {
            snprintf(out, out_sz, "%s", line + 5);
            out[strcspn(out, "\r\n")] = 0;
            found = 0;
            break;
        }
    }
    fclose(f);
    return found;
}

static int scan_roms(const char *base_dir, RomEntry **out) {
    char roms_dir[1024];
    snprintf(roms_dir, sizeof(roms_dir), "%s/roms", base_dir);
    DIR *d = opendir(roms_dir);
    if (!d) return -1;
    int count = 0, cap = 16;
    RomEntry *list = malloc((size_t)cap * sizeof(RomEntry));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char full[1200];
        snprintf(full, sizeof(full), "%s/%s", roms_dir, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        char info[1400];
        snprintf(info, sizeof(info), "%s/pdxinfo", full);
        size_t len = strlen(e->d_name);
        int is_pdx = len > 4 && strcasecmp(e->d_name + len - 4, ".pdx") == 0;
        if (!is_pdx && access(info, F_OK) != 0) continue;
        if (count == cap) {
            cap *= 2;
            list = realloc(list, (size_t)cap * sizeof(RomEntry));
        }
        RomEntry *r = &list[count];
        snprintf(r->dir, sizeof(r->dir), "%s", full);
        if (rom_read_name(full, r->name, sizeof(r->name)) != 0) {
            snprintf(r->name, sizeof(r->name), "%.*s", is_pdx ? (int)(len - 4) : (int)len, e->d_name);
        }
        count++;
    }
    closedir(d);
    if (count == 0) {
        free(list);
        return 0;
    }
    qsort(list, (size_t)count, sizeof(RomEntry), rom_cmp);
    *out = list;
    return count;
}

static void menu_render(RomEntry *roms, int count, int sel, int scroll) {
    LCDBitmap *bm = g_pd.display_bitmap;
    memset(bm->data, 0x00, (size_t)bm->rowbytes * bm->height);
    if (bm->mask) memset(bm->mask, 0xFF, (size_t)bm->rowbytes * bm->height);

    menu_fill_rect(bm, 0, 0, PD_SCREEN_WIDTH, 26, 1);
    pd_builtin_text(bm, 12, 6, 2, 0, "PLAYDATE");
    pd_builtin_text(bm, PD_SCREEN_WIDTH - 12 - 6 * 6 * 1, 10, 1, 0, "A:PLAY");

    const int row_h = 16;
    const int list_y = 32;
    const int visible = (PD_SCREEN_HEIGHT - list_y - 4) / row_h;

    for (int i = 0; i < visible && scroll + i < count; i++) {
        int idx = scroll + i;
        int y = list_y + i * row_h;
        if (idx == sel) {
            menu_fill_rect(bm, 6, y - 4, PD_SCREEN_WIDTH - 12, 15, 1);
            pd_builtin_text(bm, 14, y, 1, 0, roms[idx].name);
        } else {
            pd_builtin_text(bm, 14, y, 1, 1, roms[idx].name);
        }
    }
    if (scroll > 0) pd_builtin_text(bm, PD_SCREEN_WIDTH - 16, list_y, 1, 1, "^");
    if (scroll + visible < count)
        pd_builtin_text(bm, PD_SCREEN_WIDTH - 16, PD_SCREEN_HEIGHT - 14, 1, 1, "v");

    pd_present_frame();
    if (bm->texture) {
        SDL_RenderClear(g_pd.renderer);
        SDL_RenderCopy(g_pd.renderer, bm->texture, NULL, NULL);
        SDL_RenderPresent(g_pd.renderer);
    }

    if (getenv("PD_MENU_DUMP")) {
        FILE *pf = fopen("/tmp/pd_menu_fb.pbm", "wb");
        if (pf) {
            fprintf(pf, "P1\n%d %d\n", bm->width, bm->height);
            for (int fy = 0; fy < bm->height; fy++) {
                for (int fx = 0; fx < bm->width; fx++) {
                    int v = (bm->data[fy * bm->rowbytes + fx / 8] >> (7 - fx % 8)) & 1;
                    fputc(v ? '1' : '0', pf);
                }
                fputc('\n', pf);
            }
            fclose(pf);
        }
    }
}

/* Returns 0 with out_dir set, 1 if the user quit, -1 if no roms found. */
int pd_rom_menu(const char *base_dir, char *out_dir, size_t out_sz) {
    RomEntry *roms = NULL;
    int count = scan_roms(base_dir, &roms);
    if (count <= 0) return -1;
    if (count == 1) {
        snprintf(out_dir, out_sz, "%s", roms[0].dir);
        free(roms);
        return 0;
    }

    const char *auto_pick = getenv("PD_MENU_PICK");
    if (auto_pick) {
        int idx = atoi(auto_pick);
        if (idx < 0) idx = 0;
        if (idx >= count) idx = count - 1;
        if (getenv("PD_MENU_DUMP")) menu_render(roms, count, idx, 0);
        snprintf(out_dir, out_sz, "%s", roms[idx].dir);
        free(roms);
        return 0;
    }

    int sel = 0, scroll = 0, done = 0, quit = 0;
    const int row_h = 16;
    const int visible = (PD_SCREEN_HEIGHT - 32 - 4) / row_h;

    /* open any attached controller */
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameControllerOpen(i);
            break;
        }
    }

    menu_render(roms, count, sel, scroll);
    while (!done && !quit) {
        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, 100)) {
            int dir = 0, pick = 0, back = 0;
            switch (ev.type) {
                case SDL_QUIT: quit = 1; break;
                case SDL_KEYDOWN:
                    switch (ev.key.keysym.sym) {
                        case SDLK_UP: dir = -1; break;
                        case SDLK_DOWN: dir = 1; break;
                        case SDLK_RETURN: case SDLK_z: case SDLK_a: case SDLK_j:
                            pick = 1; break;
                        case SDLK_ESCAPE: back = 1; break;
                        default: break;
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    switch (ev.cbutton.button) {
                        case SDL_CONTROLLER_BUTTON_DPAD_UP: dir = -1; break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: dir = 1; break;
                        case SDL_CONTROLLER_BUTTON_A:
                        case SDL_CONTROLLER_BUTTON_START: pick = 1; break;
                        case SDL_CONTROLLER_BUTTON_BACK: back = 1; break;
                        default: break;
                    }
                    break;
                default: break;
            }
            if (dir) {
                sel += dir;
                if (sel < 0) sel = count - 1;
                if (sel >= count) sel = 0;
                if (sel < scroll) scroll = sel;
                if (sel >= scroll + visible) scroll = sel - visible + 1;
                break; /* re-render promptly on movement */
            }
            if (pick) { done = 1; break; }
            if (back) { quit = 1; break; }
        }
        /* Re-present every pass: on KMSDRM/handheld displays the first
           frame can be dropped before the display pipeline is up, which
           left the menu invisible until a button press forced a redraw. */
        menu_render(roms, count, sel, scroll);
    }

    if (done) snprintf(out_dir, out_sz, "%s", roms[sel].dir);
    free(roms);
    return done ? 0 : 1;
}
