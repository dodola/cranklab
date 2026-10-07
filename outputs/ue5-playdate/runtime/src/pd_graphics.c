#include "pd_runtime.h"
#include "pd_pdx.h"
#include "pd_geometry.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

static LCDBitmap *create_bitmap(int width, int height) {
    LCDBitmap *bm = calloc(1, sizeof(LCDBitmap));
    bm->width = width;
    bm->height = height;
    bm->rowbytes = (width + 7) / 8;
    bm->data = calloc(bm->rowbytes * height, 1);
    bm->mask = malloc(bm->rowbytes * height);
    memset(bm->mask, 0xFF, bm->rowbytes * height);
    return bm;
}

static void free_bitmap(LCDBitmap *bm) {
    if (!bm) return;
    if (bm->texture) SDL_DestroyTexture(bm->texture);
    free(bm->data);
    free(bm->mask);
    free(bm);
}

typedef struct PDLoadedFont {LCDFont *font;struct PDLoadedFont *next;} PDLoadedFont;
static PDLoadedFont *g_loaded_fonts=NULL;
static void free_font(LCDFont *font) {
    if(!font)return;
    for(int i=0;i<PD_FONT_MAX_GLYPHS;i++)free_bitmap(font->glyph_bmp[i]);
    for(int i=0;i<font->glyph_ext_count;i++)free_bitmap(font->glyph_ext[i].bmp);
    free(font->glyph_ext);free(font);
}
static void bitmap_set_pixel(LCDBitmap *bm, int x, int y, int black) {
    if (x < 0 || x >= bm->width || y < 0 || y >= bm->height) return;
    int byte_idx = y * bm->rowbytes + (x / 8);
    int bit_idx = 7 - (x % 8);
    if (black)
        bm->data[byte_idx] |= (1 << bit_idx);
    else
        bm->data[byte_idx] &= ~(1 << bit_idx);
}

int pd_font_text_width(LCDFont *font, const char *text);

static int bitmap_get_pixel(LCDBitmap *bm, int x, int y) {
    if (x < 0 || x >= bm->width || y < 0 || y >= bm->height) return 0;
    int byte_idx = y * bm->rowbytes + (x / 8);
    int bit_idx = 7 - (x % 8);
    return (bm->data[byte_idx] >> bit_idx) & 1;
}

static uint8_t g_current_pattern[16] = {0};
static int g_has_pattern = 0;

int pd_frame_draw_ops = 0;
uint32_t pd_frame_serial = 0;

/* Dirty-region approximation: pixels the game draws directly each frame
   are recorded here; sprite passes later in the same frame must not paint
   over them (real hardware only repaints dirty rects, so direct drawing
   survives sprite passes). */
LCDBitmap *pd_screen_bitmap = NULL;
int pd_damage_protect = 0; /* 1 while a sprite pass paints */
static uint8_t pd_damage_mask[PD_SCREEN_HEIGHT][(PD_SCREEN_WIDTH + 7) / 8];

void pd_damage_reset(void) {
    memset(pd_damage_mask, 0, sizeof(pd_damage_mask));
}

static int pattern_pixel(int x, int y) {
    if (!g_has_pattern) return -1;
    int row = y % 8;
    int col = x % 8;
    int mask_bit = (g_current_pattern[row + 8] >> (7 - col)) & 1;
    if (!mask_bit) return -1;
    return (g_current_pattern[row] >> (7 - col)) & 1;
}

static LCDSolidColor resolve_color(LCDSolidColor color, int x, int y) {
    if (color == kColorXOR) {
        int v = bitmap_get_pixel(g_pd.display_bitmap, x, y);
        return v ? kColorWhite : kColorBlack;
    }
    if (color == kColorBlack || color == kColorWhite) {
        int p = pattern_pixel(x, y);
        if (p >= 0) return p ? kColorBlack : kColorWhite;
        if (g_has_pattern) return kColorClear;
    }
    return color;
}

static void upload_bitmap_to_texture(LCDBitmap *bm, SDL_Renderer *renderer) {
    if (!bm->texture) {
        bm->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        bm->width, bm->height);
    }
    uint32_t *pixels;
    int pitch;
    SDL_LockTexture(bm->texture, NULL, (void **)&pixels, &pitch);
    for (int y = 0; y < bm->height; y++) {
        for (int x = 0; x < bm->width; x++) {
            pixels[y * (pitch / 4) + x] = bitmap_get_pixel(bm, x, y) ? 0xFF2D2A27 : 0xFFD6D3CD;
        }
    }
    SDL_UnlockTexture(bm->texture);
}

void pd_present_frame(void) {
    if (!g_pd.display_bitmap || !g_pd.renderer) return;
    upload_bitmap_to_texture(g_pd.display_bitmap, g_pd.renderer);
}

static void update_display_bitmap(void) {
    if (!g_pd.display_bitmap || !g_pd.renderer) return;
    upload_bitmap_to_texture(g_pd.display_bitmap, g_pd.renderer);
    SDL_RenderCopy(g_pd.renderer, g_pd.display_bitmap->texture, NULL, NULL);
    SDL_RenderPresent(g_pd.renderer);
}

static void clear_screen(LCDSolidColor color) {
    LCDBitmap *bm = g_pd.display_bitmap;
    if (!bm) return;
    if (color == kColorBlack || color == kColorWhite) {
        memset(bm->data, color == kColorBlack ? 0xFF : 0x00, (size_t)bm->rowbytes * bm->height);
        if (bm->mask) memset(bm->mask, 0xFF, (size_t)bm->rowbytes * bm->height);
        return;
    }
    if (color == kColorClear) {
        memset(bm->data, 0x00, (size_t)bm->rowbytes * bm->height);
        if (bm->mask) memset(bm->mask, 0x00, (size_t)bm->rowbytes * bm->height);
        return;
    }
    for (int y = 0; y < bm->height; y++) {
        for (int x = 0; x < bm->width; x++) {
            int v = bitmap_get_pixel(bm, x, y);
            bitmap_set_pixel(bm, x, y, !v);
        }
    }
}

static void bitmap_set_mask(LCDBitmap *bm, int x, int y, int opaque) {
    if (!bm->mask || x < 0 || x >= bm->width || y < 0 || y >= bm->height) return;
    int byte_idx = y * bm->rowbytes + (x / 8);
    int bit_idx = 7 - (x % 8);
    if (opaque)
        bm->mask[byte_idx] |= (uint8_t)(1 << bit_idx);
    else
        bm->mask[byte_idx] &= (uint8_t)~(1 << bit_idx);
}

static LCDBitmap *g_stencil = NULL;

static void draw_pixel_clip(int x, int y, LCDSolidColor color) {
    x += g_pd.draw_offset_x;
    y += g_pd.draw_offset_y;
    if (g_pd.clip_enabled) {
        if (x < g_pd.clip_x || x >= g_pd.clip_x + g_pd.clip_w ||
            y < g_pd.clip_y || y >= g_pd.clip_y + g_pd.clip_h) return;
    }
    if (g_stencil) {
        int sx = g_stencil->width > 0 ? x % g_stencil->width : 0;
        int sy = g_stencil->height > 0 ? y % g_stencil->height : 0;
        if (sx < 0) sx += g_stencil->width;
        if (sy < 0) sy += g_stencil->height;
        if (bitmap_get_pixel(g_stencil, sx, sy)) return; /* black blocks */
    }
    if (g_pd.display_bitmap == pd_screen_bitmap && x >= 0 && x < PD_SCREEN_WIDTH &&
        y >= 0 && y < PD_SCREEN_HEIGHT) {
        if (pd_damage_protect) {
            /* sprite pass: leave the game's own drawing intact */
            if ((pd_damage_mask[y][x / 8] >> (x % 8)) & 1) return;
        } else {
            pd_damage_mask[y][x / 8] |= (uint8_t)(1 << (x % 8));
        }
    }
    color = resolve_color(color, x, y);
    pd_frame_draw_ops++;
    int val;
    switch (color) {
        case kColorBlack: val = 1; break;
        case kColorWhite: val = 0; break;
        case kColorClear:
            bitmap_set_mask(g_pd.display_bitmap, x, y, 0);
            return;
        case kColorXOR:
            val = bitmap_get_pixel(g_pd.display_bitmap, x, y);
            val = !val;
            break;
        default: val = 1;
    }
    bitmap_set_pixel(g_pd.display_bitmap, x, y, val);
    bitmap_set_mask(g_pd.display_bitmap, x, y, 1);
}

static void draw_line_bresenham(int x1, int y1, int x2, int y2, int width, LCDSolidColor color) {
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    int half_w = width / 2;
    while (1) {
        for (int wy = -half_w; wy <= half_w; wy++) {
            for (int wx = -half_w; wx <= half_w; wx++) {
                draw_pixel_clip(x1 + wx, y1 + wy, color);
            }
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

static void fill_rect(int x, int y, int w, int h, LCDSolidColor color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            draw_pixel_clip(px, py, color);
        }
    }
}

static void draw_rect(int x, int y, int w, int h, int linewidth, LCDSolidColor color) {
    for (int i = 0; i < linewidth; i++) {
        for (int px = x; px < x + w; px++) {
            draw_pixel_clip(px, y + i, color);
            draw_pixel_clip(px, y + h - 1 - i, color);
        }
        for (int py = y; py < y + h; py++) {
            draw_pixel_clip(x + i, py, color);
            draw_pixel_clip(x + w - 1 - i, py, color);
        }
    }
}

void pd_debug_draw_rect(int x, int y, int w, int h) {
    draw_rect(x, y, w, h, 1, kColorXOR);
}

static void fill_ellipse(int x, int y, int w, int h, float startAngle, float endAngle, LCDSolidColor color) {
    (void)startAngle; (void)endAngle;
    int cx = x + w / 2, cy = y + h / 2;
    int rx = w / 2, ry = h / 2;
    for (int py = -ry; py <= ry; py++) {
        for (int px = -rx; px <= rx; px++) {
            float fx = (float)px / (rx > 0 ? rx : 1);
            float fy = (float)py / (ry > 0 ? ry : 1);
            if (fx * fx + fy * fy <= 1.0f) {
                draw_pixel_clip(cx + px, cy + py, color);
            }
        }
    }
}

static void draw_ellipse(int x, int y, int w, int h, int linewidth, float startAngle, float endAngle, LCDSolidColor color) {
    (void)startAngle; (void)endAngle;
    int cx = x + w / 2, cy = y + h / 2;
    int rx = w / 2, ry = h / 2;
    int r_max = rx > ry ? rx : ry;
    for (int a = 0; a < 360 * 4; a++) {
        float angle = (float)a / 4.0f * (float)M_PI / 180.0f;
        int px = (int)(cosf(angle) * rx);
        int py = (int)(sinf(angle) * ry);
        for (int lw = -linewidth/2; lw <= linewidth/2; lw++) {
            int r = r_max + lw;
            (void)r;
            draw_pixel_clip(cx + px, cy + py, color);
        }
    }
}

static void fill_polygon_scanline(int npts, int *coords, LCDSolidColor color) {
    if (npts < 3) return;
    int miny = coords[1], maxy = coords[1];
    for (int i = 1; i < npts; i++) {
        int y = coords[i * 2 + 1];
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    for (int y = miny; y <= maxy; y++) {
        int xints[64];
        int nint = 0;
        int j = npts - 1;
        for (int i = 0; i < npts; i++) {
            int yi = coords[i * 2 + 1];
            int yj = coords[j * 2 + 1];
            if (yi < y && yj >= y || yj < y && yi >= y) {
                if (nint < 64) {
                    xints[nint++] = coords[i * 2] + (y - yi) * (coords[j * 2] - coords[i * 2]) / (yj - yi);
                }
            }
            j = i;
        }
        for (int i = 0; i < nint - 1; i++) {
            for (int j2 = i + 1; j2 < nint; j2++) {
                if (xints[i] > xints[j2]) { int tmp = xints[i]; xints[i] = xints[j2]; xints[j2] = tmp; }
            }
        }
        for (int i = 0; i < nint; i += 2) {
            for (int x = xints[i]; x <= xints[i + 1]; x++) {
                draw_pixel_clip(x, y, color);
            }
        }
    }
}

static void draw_polygon_outline(int npts, int *coords, int linewidth, LCDSolidColor color) {
    for (int i = 0; i < npts - 1; i++) {
        draw_line_bresenham(coords[i * 2], coords[i * 2 + 1],
                           coords[(i + 1) * 2], coords[(i + 1) * 2 + 1], linewidth, color);
    }
    if (npts > 2) {
        draw_line_bresenham(coords[(npts - 1) * 2], coords[(npts - 1) * 2 + 1],
                           coords[0], coords[1], linewidth, color);
    }
}

void pd_draw_bitmap_at(LCDBitmap *bm, int x, int y, LCDBitmapFlip flip, LCDBitmapDrawMode mode) {
    if (!bm) return;
    int saved_pattern = g_has_pattern;
    LCDBitmapDrawMode saved_mode = g_pd.draw_mode;
    g_has_pattern = 0;
    /* draw_pixel_clip applies g_pd.draw_mode; since we handle mode here,
       temporarily set copy mode to avoid double-applying it. */
    g_pd.draw_mode = kDrawModeCopy;
    for (int py = 0; py < bm->height; py++) {
        for (int px = 0; px < bm->width; px++) {
            int src_x = (flip & kBitmapFlippedX) ? bm->width - 1 - px : px;
            int src_y = (flip & kBitmapFlippedY) ? bm->height - 1 - py : py;
            if (bm->mask) {
                int mbyte = src_y * bm->rowbytes + (src_x / 8);
                int mbit = 7 - (src_x % 8);
                if (!((bm->mask[mbyte] >> mbit) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, src_x, src_y);
            if (mode == kDrawModeWhiteTransparent && pixel == 0) continue;
            if (mode == kDrawModeBlackTransparent && pixel == 1) continue;
            if (mode == kDrawModeFillWhite) {
                draw_pixel_clip(x + px, y + py, kColorWhite);
                continue;
            }
            if (mode == kDrawModeFillBlack) {
                draw_pixel_clip(x + px, y + py, kColorBlack);
                continue;
            }
            if (mode == kDrawModeInverted) {
                draw_pixel_clip(x + px, y + py, pixel ? kColorWhite : kColorBlack);
                continue;
            }
            if (mode == kDrawModeXOR) {
                /* white-src pixels always visible: result = !(dst ^ src) */
                int dst = bitmap_get_pixel(g_pd.display_bitmap, x + px, y + py);
                draw_pixel_clip(x + px, y + py, (dst ^ pixel) ? kColorWhite : kColorBlack);
            } else if (mode == kDrawModeNXOR) {
                /* black-src pixels always visible: result = dst ^ src */
                int dst = bitmap_get_pixel(g_pd.display_bitmap, x + px, y + py);
                draw_pixel_clip(x + px, y + py, (dst ^ pixel) ? kColorBlack : kColorWhite);
            } else {
                draw_pixel_clip(x + px, y + py, pixel ? kColorBlack : kColorWhite);
            }
        }
    }
    g_has_pattern = saved_pattern;
    g_pd.draw_mode = saved_mode;
}

static LCDBitmap *parse_pdi_cell(const uint8_t *cell, long cell_len) {
    if (cell_len < 16) return NULL;
    uint16_t cw, ch, stride, cl, cr, ct, cb, cflags;
    memcpy(&cw, cell + 0, 2);
    memcpy(&ch, cell + 2, 2);
    memcpy(&stride, cell + 4, 2);
    memcpy(&cl, cell + 6, 2);
    memcpy(&cr, cell + 8, 2);
    memcpy(&ct, cell + 10, 2);
    memcpy(&cb, cell + 12, 2);
    memcpy(&cflags, cell + 14, 2);

    int full_w = cl + cw + cr;
    int full_h = ct + ch + cb;
    if (full_w <= 0 || full_h <= 0 || full_w > 4096 || full_h > 4096) return NULL;

    LCDBitmap *bm = create_bitmap(full_w, full_h);
    /* default: fully transparent-white; mark mask visible only where cell data lands */
    memset(bm->mask, 0x00, bm->rowbytes * full_h);

    const uint8_t *pix = cell + 16;
    long need = (long)stride * ch;
    int has_mask = (cflags & 0x3) != 0 && (cell_len - 16) >= need * 2;
    const uint8_t *msk = has_mask ? pix + need : NULL;

    if ((cell_len - 16) < need) return bm;

    for (int y = 0; y < ch; y++) {
        for (int x = 0; x < cw; x++) {
            int byte_i = y * stride + (x / 8);
            int bit_i = 7 - (x % 8);
            int v = (pix[byte_i] >> bit_i) & 1;
            int dx = cl + x, dy = ct + y;
            int dst_byte = dy * bm->rowbytes + (dx / 8);
            int dst_bit = 7 - (dx % 8);
            /* PDI stores 1 = white, 0 = black; our framebuffer: 1 = black */
            if (!v) bm->data[dst_byte] |= (uint8_t)(1 << dst_bit);
            int mv = msk ? ((msk[byte_i] >> bit_i) & 1) : 1;
            if (mv) bm->mask[dst_byte] |= (uint8_t)(1 << dst_bit);
        }
    }
    return bm;
}

/* Rotated+scaled bitmap draw centered on (cx, cy), inverse-mapped so there
 * are no holes. Respects the bitmap mask and draw mode. */
void pd_draw_bitmap_transformed(LCDBitmap *bm, float cx, float cy,
                                float angle_deg, float scale,
                                LCDBitmapFlip flip, LCDBitmapDrawMode mode) {
    if (!bm || scale <= 0) return;
    float rad = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float hw = bm->width * scale * 0.5f;
    float hh = bm->height * scale * 0.5f;
    float ext = sqrtf(hw * hw + hh * hh) + 1.0f;
    int x0 = (int)(cx - ext), x1 = (int)(cx + ext);
    int y0 = (int)(cy - ext), y1 = (int)(cy + ext);
    int saved_pattern = g_has_pattern;
    g_has_pattern = 0;
    for (int dy = y0; dy <= y1; dy++) {
        for (int dx = x0; dx <= x1; dx++) {
            /* inverse rotate into source space */
            float rx = ((float)dx - cx);
            float ry = ((float)dy - cy);
            float sx = (rx * c + ry * s) / scale + bm->width * 0.5f;
            float sy = (-rx * s + ry * c) / scale + bm->height * 0.5f;
            int ix = (int)sx, iy = (int)sy;
            if (ix < 0 || ix >= bm->width || iy < 0 || iy >= bm->height) continue;
            int src_x = (flip & kBitmapFlippedX) ? bm->width - 1 - ix : ix;
            int src_y = (flip & kBitmapFlippedY) ? bm->height - 1 - iy : iy;
            if (bm->mask) {
                int mbyte = src_y * bm->rowbytes + (src_x / 8);
                int mbit = 7 - (src_x % 8);
                if (!((bm->mask[mbyte] >> mbit) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, src_x, src_y);
            if (mode == kDrawModeWhiteTransparent && pixel == 0) continue;
            if (mode == kDrawModeBlackTransparent && pixel == 1) continue;
            if (mode == kDrawModeFillWhite) { draw_pixel_clip(dx, dy, kColorWhite); continue; }
            if (mode == kDrawModeFillBlack) { draw_pixel_clip(dx, dy, kColorBlack); continue; }
            if (mode == kDrawModeInverted) {
                draw_pixel_clip(dx, dy, pixel ? kColorWhite : kColorBlack);
                continue;
            }
            draw_pixel_clip(dx, dy, pixel ? kColorBlack : kColorWhite);
        }
    }
    g_has_pattern = saved_pattern;
}

LCDBitmap *pd_load_pdi(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[12];
    if (fread(magic, 1, 12, f) != 12) { fclose(f); return NULL; }
    int is_img = memcmp(magic, "Playdate IMG", 12) == 0;
    int is_pii = memcmp(magic, "Playdate PI", 11) == 0;
    if (!is_img && !is_pii) { fclose(f); return NULL; }

    uint32_t flags = 0;
    fread(&flags, 4, 1, f);
    int compressed = (flags & 0x80000000u) != 0;

    uint8_t *cell = NULL;
    long cell_len = 0;
    if (compressed) {
        uint32_t decomp_size = 0, ow = 0, oh = 0, reserved = 0;
        fread(&decomp_size, 4, 1, f);
        fread(&ow, 4, 1, f);
        fread(&oh, 4, 1, f);
        fread(&reserved, 4, 1, f);
        (void)ow; (void)oh; (void)reserved;
        fseek(f, 0, SEEK_END);
        long fsz = ftell(f);
        fseek(f, 32, SEEK_SET);
        long comp_len = fsz - 32;
        uint8_t *comp = malloc(comp_len);
        fread(comp, 1, comp_len, f);
        fclose(f);
        cell = malloc(decomp_size + 64);
        uLongf dst_len = decomp_size + 64;
        if (uncompress(cell, &dst_len, comp, comp_len) != Z_OK) {
            free(comp); free(cell);
            return NULL;
        }
        free(comp);
        cell_len = (long)dst_len;
    } else {
        fseek(f, 0, SEEK_END);
        long fsz = ftell(f);
        fseek(f, 16, SEEK_SET);
        cell_len = fsz - 16;
        cell = malloc(cell_len);
        fread(cell, 1, cell_len, f);
        fclose(f);
    }

    if (cell_len < 16) { free(cell); return NULL; }

    LCDBitmap *bm = parse_pdi_cell(cell, cell_len);
    free(cell);
    return bm;
}

LCDFont *pd_font_from_body(const uint8_t *body, long body_len);

LCDFont *pd_load_pft(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);
    if (sz < 16 || memcmp(data, "Playdate FNT", 12) != 0) {
        free(data);
        return NULL;
    }
    uint32_t flags;
    memcpy(&flags, data + 12, 4);
    uint8_t *body = NULL;
    long body_len = 0;
    if (flags & 0x80000000u) {
        if (sz < 32) { free(data); return NULL; }
        uint32_t decomp_size;
        memcpy(&decomp_size, data + 16, 4);
        body = malloc(decomp_size + 64);
        uLongf dst_len = decomp_size + 64;
        if (uncompress(body, &dst_len, data + 32, sz - 32) != Z_OK) {
            free(data);
            free(body);
            return NULL;
        }
        body_len = (long)dst_len;
        free(data);
    } else {
        body_len = sz - 16;
        body = malloc(body_len);
        memcpy(body, data + 16, body_len);
        free(data);
    }
    LCDFont *font = pd_font_from_body(body, body_len);
    free(body);
    return font;
}

LCDFont *pd_font_from_body(const uint8_t *body, long body_len) {
    if (body_len < 68) { return NULL; }

    LCDFont *font = calloc(1, sizeof(LCDFont));
    font->glyph_width = body[0];
    font->glyph_height = body[1];
    uint16_t tracking;
    memcpy(&tracking, body + 2, 2);
    font->tracking = tracking;

    const uint8_t *pageflags = body + 4;
    int page_indices[512];
    int npages = 0;
    for (int i = 0; i < 512; i++) {
        if ((pageflags[i / 8] >> (i % 8)) & 1) page_indices[npages++] = i;
    }
    long offs_pos = 68;
    long base = offs_pos + 4L * npages;
    if (body_len < base) { free(font); return NULL; }

    uint32_t prev = 0;
    for (int pi = 0; pi < npages; pi++) {
        uint32_t end;
        memcpy(&end, body + offs_pos + 4L * pi, 4);
        if(end<prev || (uint64_t)end>(uint64_t)(body_len-base)) {free_font(font);return NULL;}
        long pstart = base + prev;
        long pend = base + end;
        prev = end;
        int page_cp_base = page_indices[pi] * 256;
        if (pend > body_len || pstart + 36 > pend) continue;

        const uint8_t *page = body + pstart;
        int ng = page[3];
        const uint8_t *gflags = page + 4;
        /* A full 256-glyph Unicode page encodes its count as zero. */
        if (!ng) for (int bit=0;bit<256;bit++) ng += (gflags[bit/8]>>(bit%8))&1;
        long gofftab = 36;
        long gdata = gofftab + 2L * ng;
        gdata = (gdata + 3) & ~3L;
        if (pstart + gdata > pend) continue;

        int gi = 0;
        uint16_t gprev = 0;
        for (int ci = 0; ci < 256 && gi < ng; ci++) {
            if (!((gflags[ci / 8] >> (ci % 8)) & 1)) continue;
            uint16_t gend;
            memcpy(&gend, page + gofftab + 2L * gi, 2);
            if(gend<gprev || (long)gend>pend-pstart-gdata) break;
            long gstart = gdata + gprev;
            long gstop = gdata + gend;
            gprev = gend;
            gi++;
            if (pstart + gstop > pend || gstop - gstart < 4) continue;

            const uint8_t *g = page + gstart;
            uint8_t adv = g[0];
            uint8_t nshort = g[1];
            uint16_t nlong;
            memcpy(&nlong, g + 2, 2);
            long cpos = 4 + 2L * nshort;
            cpos = (cpos + 3) & ~3L;
            cpos += 4L * nlong;
            long clen = (gstop - gstart) - cpos;
            int cp = page_cp_base + ci;
            LCDBitmap *bmp = NULL;
            if (clen >= 16) {
                bmp = parse_pdi_cell(g + cpos, clen);
                if (bmp) font->has_glyphs = 1;
            }
            if (cp < PD_FONT_MAX_GLYPHS) {
                font->glyph_adv[cp] = adv;
                font->glyph_bmp[cp] = bmp;
            } else {
                if (font->glyph_ext_count == font->glyph_ext_cap) {
                    font->glyph_ext_cap = font->glyph_ext_cap ? font->glyph_ext_cap * 2 : 16;
                    font->glyph_ext = realloc(font->glyph_ext,
                                              font->glyph_ext_cap * sizeof(PDGlyphExt));
                }
                PDGlyphExt *e = &font->glyph_ext[font->glyph_ext_count++];
                e->cp = (uint32_t)cp;
                e->bmp = bmp;
                e->adv = adv;
            }
        }
    }
    if (getenv("PD_FONT_DEBUG")) {
        int base = 0;
        for (int i = 0; i < PD_FONT_MAX_GLYPHS; i++) if (font->glyph_adv[i]) base++;
        fprintf(stderr, "[font] base=%d ext=%d:", base, font->glyph_ext_count);
        for (int i = 0; i < font->glyph_ext_count; i++)
            fprintf(stderr, " %#x(bmp=%d)", font->glyph_ext[i].cp,
                    font->glyph_ext[i].bmp != NULL);
        fprintf(stderr, "\n");
    }
    PDLoadedFont *owned=malloc(sizeof(PDLoadedFont));
    if(!owned){free_font(font);return NULL;}
    owned->font=font;owned->next=g_loaded_fonts;g_loaded_fonts=owned;
    return font;
}

static int lua_gfx_clear(lua_State *L) {
    int color = (int)luaL_optinteger(L, 1, kColorWhite);
    clear_screen(color);
    return 0;
}

/* Masked clear of one screen rect to the background color: pixels the game
   drew this frame (damage mask) are preserved. Used by the sprite pass to
   repaint only regions sprites occupy, so direct drawing persists like on
   real hardware (which only repaints dirty regions). */
void pd_clear_display_bg_rect(int x0, int y0, int x1, int y1) {
    LCDBitmap *bm = g_pd.display_bitmap;
    if (!bm || bm != pd_screen_bitmap) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > bm->width) x1 = bm->width;
    if (y1 > bm->height) y1 = bm->height;
    if (x1 > PD_SCREEN_WIDTH) x1 = PD_SCREEN_WIDTH;
    if (y1 > PD_SCREEN_HEIGHT) y1 = PD_SCREEN_HEIGHT;
    int black = (g_pd.bg_color == kColorBlack);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if ((pd_damage_mask[y][x / 8] >> (x % 8)) & 1) continue;
            bitmap_set_pixel(bm, x, y, black);
            bitmap_set_mask(bm, x, y, 1);
        }
    }
}

/* Clear to the current background color (sprite system frame clear).
   Honors the damage mask so a sprite pass doesn't erase what the game
   already drew this frame. */
void pd_clear_display_bg(void) {
    if (pd_damage_protect && g_pd.display_bitmap == pd_screen_bitmap) {
        LCDBitmap *bm = g_pd.display_bitmap;
        if (!bm) return;
        int black = (g_pd.bg_color == kColorBlack);
        for (int y = 0; y < bm->height && y < PD_SCREEN_HEIGHT; y++) {
            for (int x = 0; x < bm->width && x < PD_SCREEN_WIDTH; x++) {
                if ((pd_damage_mask[y][x / 8] >> (x % 8)) & 1) continue;
                bitmap_set_pixel(bm, x, y, black);
                bitmap_set_mask(bm, x, y, 1);
            }
        }
        return;
    }
    clear_screen(g_pd.bg_color);
}

static int lua_gfx_setBackgroundColor(lua_State *L) {
    g_pd.bg_color = (LCDSolidColor)luaL_checknumber(L, 1);
    return 0;
}

static int lua_gfx_getBackgroundColor(lua_State *L) {
    lua_pushinteger(L, g_pd.bg_color);
    return 1;
}

/* SDK accepts draw modes as kDrawMode* numbers or strings */
int pd_drawmode_arg(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char *s = lua_tostring(L, idx);
        if (strcmp(s, "copy") == 0) return kDrawModeCopy;
        if (strcmp(s, "whiteTransparent") == 0) return kDrawModeWhiteTransparent;
        if (strcmp(s, "blackTransparent") == 0) return kDrawModeBlackTransparent;
        if (strcmp(s, "fillWhite") == 0) return kDrawModeFillWhite;
        if (strcmp(s, "fillBlack") == 0) return kDrawModeFillBlack;
        if (strcmp(s, "XOR") == 0) return kDrawModeXOR;
        if (strcmp(s, "NXOR") == 0) return kDrawModeNXOR;
        if (strcmp(s, "inverted") == 0) return kDrawModeInverted;
        return kDrawModeCopy;
    }
    return (int)luaL_checknumber(L, idx);
}

static int lua_gfx_setDrawMode(lua_State *L) {
    g_pd.draw_mode = pd_drawmode_arg(L, 1);
    lua_pushinteger(L, g_pd.draw_mode);
    return 1;
}

static int lua_gfx_getDrawMode(lua_State *L) {
    lua_pushinteger(L, g_pd.draw_mode);
    return 1;
}

static int g_line_width = 1;

static int lua_gfx_setLineWidth(lua_State *L) {
    g_line_width = (int)luaL_checknumber(L, 1);
    if (g_line_width < 1) g_line_width = 1;
    return 0;
}

static int lua_gfx_getLineWidth(lua_State *L) {
    lua_pushinteger(L, g_line_width);
    return 1;
}

static int lua_gfx_setStrokeLocation(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_gfx_setStencilImage(lua_State *L) {
    LCDBitmap **ud = luaL_testudata(L, 1, "pd.bitmap");
    g_stencil = (ud && *ud) ? *ud : NULL;
    return 0;
}

static int lua_gfx_clearStencil(lua_State *L) {
    (void)L;
    g_stencil = NULL;
    return 0;
}

static int lua_gfx_setDrawOffset(lua_State *L) {
    g_pd.draw_offset_x = (int)luaL_checknumber(L, 1);
    g_pd.draw_offset_y = (int)luaL_checknumber(L, 2);
    return 0;
}

static int lua_gfx_getDrawOffset(lua_State *L) {
    lua_pushinteger(L, g_pd.draw_offset_x);
    lua_pushinteger(L, g_pd.draw_offset_y);
    return 2;
}

/* SDK: setClipRect is in world coords (draw offset applies);
   setScreenClipRect is absolute screen coords. */
static int clip_rect_common(lua_State *L, int add_offset) {
    PDRectInternal *r = luaL_testudata(L, 1, "pd.rect");
    int ox = add_offset ? g_pd.draw_offset_x : 0;
    int oy = add_offset ? g_pd.draw_offset_y : 0;
    if (r) {
        g_pd.clip_x = (int)r->x + ox;
        g_pd.clip_y = (int)r->y + oy;
        g_pd.clip_w = (int)r->width;
        g_pd.clip_h = (int)r->height;
    } else {
        g_pd.clip_x = (int)luaL_checknumber(L, 1) + ox;
        g_pd.clip_y = (int)luaL_checknumber(L, 2) + oy;
        g_pd.clip_w = (int)luaL_checknumber(L, 3);
        g_pd.clip_h = (int)luaL_checknumber(L, 4);
    }
    g_pd.clip_enabled = 1;
    return 0;
}

static int lua_gfx_setClipRect(lua_State *L) {
    return clip_rect_common(L, 1);
}

static int lua_gfx_setScreenClipRect(lua_State *L) {
    return clip_rect_common(L, 0);
}

static int lua_gfx_clearClipRect(lua_State *L) {
    (void)L;
    g_pd.clip_enabled = 0;
    return 0;
}

static int lua_gfx_getClipRect(lua_State *L) {
    if (!g_pd.clip_enabled) {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, g_pd.display_bitmap ? g_pd.display_bitmap->width : PD_SCREEN_WIDTH);
        lua_pushinteger(L, g_pd.display_bitmap ? g_pd.display_bitmap->height : PD_SCREEN_HEIGHT);
        return 4;
    }
    lua_pushinteger(L, g_pd.clip_x - g_pd.draw_offset_x);
    lua_pushinteger(L, g_pd.clip_y - g_pd.draw_offset_y);
    lua_pushinteger(L, g_pd.clip_w);
    lua_pushinteger(L, g_pd.clip_h);
    return 4;
}

static int lua_gfx_getScreenClipRect(lua_State *L) {
    if (!g_pd.clip_enabled) {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, g_pd.display_bitmap ? g_pd.display_bitmap->width : PD_SCREEN_WIDTH);
        lua_pushinteger(L, g_pd.display_bitmap ? g_pd.display_bitmap->height : PD_SCREEN_HEIGHT);
        return 4;
    }
    lua_pushinteger(L, g_pd.clip_x);
    lua_pushinteger(L, g_pd.clip_y);
    lua_pushinteger(L, g_pd.clip_w);
    lua_pushinteger(L, g_pd.clip_h);
    return 4;
}

#define PD_FOCUS_STACK_MAX 32
typedef struct {
    LCDBitmap *target;
    int clip_enabled;
    int clip_x, clip_y, clip_w, clip_h;
    LCDBitmapDrawMode draw_mode;
    LCDSolidColor fg_color;
    LCDBitmap *stencil;
    int draw_offset_x, draw_offset_y;
} PDGfxContext;
static PDGfxContext g_focus_stack[PD_FOCUS_STACK_MAX];
static int g_focus_depth = 0;

static void push_focus(LCDBitmap *target) {
    if (g_focus_depth >= PD_FOCUS_STACK_MAX) return;
    PDGfxContext *c = &g_focus_stack[g_focus_depth++];
    c->target = g_pd.display_bitmap;
    c->clip_enabled = g_pd.clip_enabled;
    c->clip_x = g_pd.clip_x;
    c->clip_y = g_pd.clip_y;
    c->clip_w = g_pd.clip_w;
    c->clip_h = g_pd.clip_h;
    c->draw_mode = g_pd.draw_mode;
    c->fg_color = g_pd.fg_color;
    c->stencil = g_stencil;
    c->draw_offset_x = g_pd.draw_offset_x;
    c->draw_offset_y = g_pd.draw_offset_y;
    if (target) {
        g_pd.display_bitmap = target;
        g_pd.clip_enabled = 0;
        g_stencil = NULL;
        g_pd.draw_offset_x = 0;
        g_pd.draw_offset_y = 0;
    }
}

static void pop_focus(void) {
    if (g_focus_depth <= 0) return;
    PDGfxContext *c = &g_focus_stack[--g_focus_depth];
    g_pd.display_bitmap = c->target;
    g_pd.clip_enabled = c->clip_enabled;
    g_pd.clip_x = c->clip_x;
    g_pd.clip_y = c->clip_y;
    g_pd.clip_w = c->clip_w;
    g_pd.clip_h = c->clip_h;
    g_pd.draw_mode = c->draw_mode;
    g_pd.fg_color = c->fg_color;
    g_stencil = c->stencil;
    g_pd.draw_offset_x = c->draw_offset_x;
    g_pd.draw_offset_y = c->draw_offset_y;
}

void pd_gfx_reset_focus(void) {
    if (g_focus_depth > 0) {
        g_focus_depth = 1;
        pop_focus();
    }
}

LCDBitmap *pd_gfx_get_stencil(void) { return g_stencil; }
void pd_graphics_shutdown(void) {
    while(g_loaded_fonts){PDLoadedFont *owned=g_loaded_fonts;g_loaded_fonts=owned->next;free_font(owned->font);free(owned);}
    g_pd.current_font=NULL;
    g_stencil=NULL; g_focus_depth=0; g_has_pattern=0; g_line_width=1;
    pd_screen_bitmap=NULL; g_pd.display_bitmap=NULL;
    pd_frame_serial=0; pd_frame_draw_ops=0; pd_damage_protect=0; pd_damage_reset();
}
void pd_gfx_set_stencil(LCDBitmap *bm) { g_stencil = bm; }

static int lua_gfx_pushContext(lua_State *L) {
    LCDBitmap *target = NULL;
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        LCDBitmap **ud = luaL_testudata(L, 1, "pd.bitmap");
        if (ud) target = *ud;
    }
    push_focus(target);
    return 0;
}

static int lua_gfx_popContext(lua_State *L) {
    (void)L;
    pop_focus();
    return 0;
}

static int lua_gfx_lockFocus(lua_State *L) {
    LCDBitmap **ud = luaL_testudata(L, 1, "pd.bitmap");
    push_focus(ud ? *ud : NULL);
    return 0;
}

static int lua_gfx_unlockFocus(lua_State *L) {
    (void)L;
    pop_focus();
    return 0;
}

static int lua_gfx_setPixel(lua_State *L) {
    int x = (int)luaL_checknumber(L, 1);
    int y = (int)luaL_checknumber(L, 2);
    int color = (int)luaL_optinteger(L, 3, g_pd.fg_color);
    draw_pixel_clip(x, y, color);
    return 0;
}

static int lua_gfx_drawLine(lua_State *L) {
    float fx1, fy1, fx2, fy2;
    if (pd_get_lineSegment(L, 1, &fx1, &fy1, &fx2, &fy2)) {
        draw_line_bresenham((int)fx1, (int)fy1, (int)fx2, (int)fy2,
                            g_line_width, g_pd.fg_color);
        return 0;
    }
    int x1 = (int)luaL_checknumber(L, 1);
    int y1 = (int)luaL_checknumber(L, 2);
    int x2 = (int)luaL_checknumber(L, 3);
    int y2 = (int)luaL_checknumber(L, 4);
    int width = (int)luaL_optinteger(L, 5, g_line_width);
    int color = (int)luaL_optinteger(L, 6, g_pd.fg_color);
    draw_line_bresenham(x1, y1, x2, y2, width, color);
    return 0;
}

static int lua_gfx_fillRect(lua_State *L) {
    int x, y, w, h, color;
    PDRectInternal *r = luaL_testudata(L, 1, "pd.rect");
    if (r) {
        x = (int)r->x; y = (int)r->y; w = (int)r->width; h = (int)r->height;
        color = (int)luaL_optinteger(L, 2, g_pd.fg_color);
    } else {
        x = (int)luaL_checknumber(L, 1);
        y = (int)luaL_checknumber(L, 2);
        w = (int)luaL_checknumber(L, 3);
        h = (int)luaL_checknumber(L, 4);
        color = (int)luaL_optinteger(L, 5, g_pd.fg_color);
    }
    if (getenv("PD_TRACE"))
        fprintf(stderr, "[fillRect %d,%d %dx%d c=%d tgt=%dx%d clip=%d]", x, y, w, h, color,
                g_pd.display_bitmap ? g_pd.display_bitmap->width : -1,
                g_pd.display_bitmap ? g_pd.display_bitmap->height : -1,
                g_pd.clip_enabled);
    fill_rect(x, y, w, h, color);
    return 0;
}

/* Read x,y,w,h from either a pd.rect at arg 1 or four numbers starting at
   arg 1. Returns the index of the first argument after the geometry. */
static int rect_args(lua_State *L, int *x, int *y, int *w, int *h) {
    PDRectInternal *r = luaL_testudata(L, 1, "pd.rect");
    if (r) {
        *x = (int)r->x; *y = (int)r->y; *w = (int)r->width; *h = (int)r->height;
        return 2;
    }
    *x = (int)luaL_checknumber(L, 1);
    *y = (int)luaL_checknumber(L, 2);
    *w = (int)luaL_checknumber(L, 3);
    *h = (int)luaL_checknumber(L, 4);
    return 5;
}

static int lua_gfx_drawRect(lua_State *L) {
    int x, y, w, h;
    int next = rect_args(L, &x, &y, &w, &h);
    int color = (int)luaL_optinteger(L, next, g_pd.fg_color);
    draw_rect(x, y, w, h, 1, color);
    return 0;
}

static int lua_gfx_fillTriangle(lua_State *L) {
    int x1 = (int)luaL_checknumber(L, 1);
    int y1 = (int)luaL_checknumber(L, 2);
    int x2 = (int)luaL_checknumber(L, 3);
    int y2 = (int)luaL_checknumber(L, 4);
    int x3 = (int)luaL_checknumber(L, 5);
    int y3 = (int)luaL_checknumber(L, 6);
    int color = (int)luaL_optinteger(L, 7, g_pd.fg_color);
    int miny = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
    int maxy = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);
    for (int y = miny; y <= maxy; y++) {
        float a1 = (y2 - y3) != 0 ? (float)(y - y3) / (y2 - y3) : 0;
        float a2 = (y3 - y1) != 0 ? (float)(y - y1) / (y3 - y1) : 0;
        float a3 = (y1 - y2) != 0 ? (float)(y - y2) / (y1 - y2) : 0;
        int xa = (int)(x3 + a1 * (x1 - x3));
        int xb = (int)(x1 + a2 * (x2 - x1));
        int xc = (int)(x2 + a3 * (x3 - x2));
        int xlo = xa < xb ? (xa < xc ? xa : xc) : (xb < xc ? xb : xc);
        int xhi = xa > xb ? (xa > xc ? xa : xc) : (xb > xc ? xb : xc);
        for (int x = xlo; x <= xhi; x++)
            draw_pixel_clip(x, y, color);
    }
    return 0;
}

static int lua_gfx_drawEllipse(lua_State *L) {
    int x, y, w, h;
    int next = rect_args(L, &x, &y, &w, &h);
    int lw = (int)luaL_optnumber(L, next, 1); /* SDK tolerates float widths */
    float sa = (float)luaL_optnumber(L, next + 1, 0);
    float ea = (float)luaL_optnumber(L, next + 2, 360);
    int color = (int)luaL_optinteger(L, next + 3, g_pd.fg_color);
    draw_ellipse(x, y, w, h, lw, sa, ea, color);
    return 0;
}

static int lua_gfx_fillEllipse(lua_State *L) {
    int x, y, w, h;
    int next = rect_args(L, &x, &y, &w, &h);
    float sa = (float)luaL_optnumber(L, next, 0);
    float ea = (float)luaL_optnumber(L, next + 1, 360);
    int color = (int)luaL_optinteger(L, next + 2, g_pd.fg_color);
    fill_ellipse(x, y, w, h, sa, ea, color);
    return 0;
}

static int lua_gfx_fillCircleInRect(lua_State *L) {
    int x, y, w, h;
    int next = rect_args(L, &x, &y, &w, &h);
    int color = (int)luaL_optinteger(L, next, g_pd.fg_color);
    fill_ellipse(x, y, w, h, 0, 360, color);
    return 0;
}

static int lua_gfx_drawCircleInRect(lua_State *L) {
    int x, y, w, h;
    rect_args(L, &x, &y, &w, &h);
    draw_ellipse(x, y, w, h, g_line_width, 0, 360, g_pd.fg_color);
    return 0;
}

static int lua_gfx_fillCircleAtPoint(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    if (getenv("PD_TRACE"))
        fprintf(stderr, "[fillCircle %g,%g r=%g c=%d]", x, y, r, (int)g_pd.fg_color);
    fill_ellipse((int)(x - r), (int)(y - r), (int)(r * 2), (int)(r * 2), 0, 360, g_pd.fg_color);
    return 0;
}

static int lua_gfx_drawCircleAtPoint(lua_State *L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    if (getenv("PD_TRACE"))
        fprintf(stderr, "[drawCircle %g,%g r=%g c=%d]", x, y, r, (int)g_pd.fg_color);
    draw_ellipse((int)(x - r), (int)(y - r), (int)(r * 2), (int)(r * 2), g_line_width, 0, 360, g_pd.fg_color);
    return 0;
}

/* Horizontal inset of a rounded rect at row py (0-based inside rect). */
static int roundrect_inset(int py, int h, int r) {
    float dy = -1;
    if (py < r) dy = (float)(r - py) - 0.5f;
    else if (py >= h - r) dy = (float)(py - (h - r)) + 0.5f;
    if (dy < 0) return 0;
    if (dy > r) dy = (float)r;
    return r - (int)(sqrtf((float)r * r - dy * dy) + 0.5f);
}

static void fill_round_rect(int x, int y, int w, int h, int r, LCDSolidColor color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) { fill_rect(x, y, w, h, color); return; }
    for (int py = 0; py < h; py++) {
        int inset = roundrect_inset(py, h, r);
        fill_rect(x + inset, y + py, w - 2 * inset, 1, color);
    }
}

static int lua_gfx_fillRoundRect(lua_State *L) {
    int x, y, w, h;
    int next = rect_args(L, &x, &y, &w, &h);
    int r = (int)luaL_optnumber(L, next, 0);
    int color = (int)luaL_optnumber(L, next + 1, g_pd.fg_color);
    fill_round_rect(x, y, w, h, r, color);
    return 0;
}

static int lua_gfx_drawRoundRect(lua_State *L) {
    int x, y, w, h;
    int next = rect_args(L, &x, &y, &w, &h);
    int r = (int)luaL_optnumber(L, next, 0);
    int lw = g_line_width > 0 ? g_line_width : 1;
    int color = (int)luaL_optnumber(L, next + 1, g_pd.fg_color);
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) { draw_rect(x, y, w, h, lw, color); return 0; }
    for (int py = 0; py < h; py++) {
        int inset = roundrect_inset(py, h, r);
        if (py < lw || py >= h - lw) {
            fill_rect(x + inset, y + py, w - 2 * inset, 1, color);
        } else {
            int inset_above = roundrect_inset(py < h / 2 ? py + 1 : py - 1, h, r);
            int edge = lw;
            if (inset_above > inset) edge = inset_above - inset + lw;
            int span = w - 2 * inset;
            if (edge * 2 >= span) {
                fill_rect(x + inset, y + py, span, 1, color);
            } else {
                fill_rect(x + inset, y + py, edge, 1, color);
                fill_rect(x + w - inset - edge, y + py, edge, 1, color);
            }
        }
    }
    return 0;
}

static int lua_gfx_loadBitmap(lua_State *L);
static PDZEntry *find_pdz_asset(const char *path, int type);
static LCDBitmap *load_image_file(const char *path, char *attempt, size_t capacity) {
    const char *roots[] = {g_pd.pdx_dir, g_pd.save_dir};
    const char *dot = strrchr(path, '.');
    int extension = dot && (!strcmp(dot,".png") || !strcmp(dot,".pdi") || !strcmp(dot,".pdt"));
    for (int i=0; i<2; i++) {
        if(!roots[i]) continue;
        int count = snprintf(attempt,capacity,"%s/%s",roots[i],path);
        if(count<0 || (size_t)count>=capacity) continue;
        if(access(attempt,F_OK)!=0) pd_fix_path_case(attempt);
        LCDBitmap *bm = pd_load_pdi(attempt);
        if(bm) return bm;
        if(extension) count=snprintf(attempt,capacity,"%s/%.*s.pdi",roots[i],(int)(dot-path),path);
        else count=snprintf(attempt,capacity,"%s/%s.pdi",roots[i],path);
        if(count<0 || (size_t)count>=capacity) continue;
        if(access(attempt,F_OK)!=0) pd_fix_path_case(attempt);
        bm=pd_load_pdi(attempt);
        if(bm) return bm;
    }
    return NULL;
}

static int lua_gfx_newBitmap(lua_State *L) {
    if (lua_type(L, 1) == LUA_TSTRING)
        return lua_gfx_loadBitmap(L);
    int w = (int)luaL_checknumber(L, 1);
    int h = (int)luaL_checknumber(L, 2);
    int bg = (int)luaL_optinteger(L, 3, kColorClear);
    LCDBitmap *bm = create_bitmap(w, h);
    if (bg == kColorBlack) {
        memset(bm->data, 0xFF, bm->rowbytes * h);
    } else if (bg == kColorClear) {
        memset(bm->mask, 0x00, bm->rowbytes * h);
    }
    LCDBitmap **ud = lua_newuserdata(L, sizeof(LCDBitmap *));
    *ud = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_loadBitmap(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    LCDBitmap *bm = load_image_file(path,fullpath,sizeof(fullpath));
    if (!bm) {
        PDZEntry *e = find_pdz_asset(path, 2);
        if (e) bm = parse_pdi_cell(e->data, (long)e->size);
    }
    if (!bm) {
        /* firmware fallback: if only an image table exists at this path,
           image.new returns its first frame */
        extern int load_pdt_into_table(lua_State *L, const char *path);
        const char *dot = strrchr(path, '.');
        char pdtpath[1200];
        if (dot && (strcmp(dot, ".png") == 0 || strcmp(dot, ".pdi") == 0 || strcmp(dot, ".pdt") == 0))
            snprintf(pdtpath, sizeof(pdtpath), "%s/%.*s.pdt", g_pd.pdx_dir, (int)(dot - path), path);
        else
            snprintf(pdtpath, sizeof(pdtpath), "%s/%s.pdt", g_pd.pdx_dir, path);
        if (access(pdtpath, F_OK) != 0) pd_fix_path_case(pdtpath);
        if (load_pdt_into_table(L, pdtpath)) {
            /* the loader returns a userdata wrapper; frames live in its
               uservalue table */
            lua_getiuservalue(L, -1, 1);
            if (lua_istable(L, -1)) {
                lua_rawgeti(L, -1, 1);
                if (luaL_testudata(L, -1, "pd.bitmap")) {
                    lua_remove(L, -2); /* inner table */
                    lua_remove(L, -2); /* wrapper userdata */
                    return 1;
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 2);
        }
    }
    if (!bm) {
        fprintf(stderr, "[lb] FAILED %s ", fullpath);
        lua_pushnil(L);
        lua_pushstring(L, "could not load bitmap");
        return 2;
    }
    LCDBitmap **ud = lua_newuserdata(L, sizeof(LCDBitmap *));
    *ud = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

/* image:load(path): replace this bitmap's pixels in place */
static int lua_gfx_bitmapLoad(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    const char *path = luaL_checkstring(L, 2);
    char fullpath[1024];
    LCDBitmap *bm = load_image_file(path,fullpath,sizeof(fullpath));
    if (!bm) {
        PDZEntry *e = find_pdz_asset(path, 2);
        if (e) bm = parse_pdi_cell(e->data, (long)e->size);
    }
    if (!bm) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "could not load bitmap");
        return 2;
    }
    LCDBitmap *dst = *ud;
    if (dst) {
        free(dst->data);
        free(dst->mask);
        if (dst->texture) SDL_DestroyTexture(dst->texture);
        *dst = *bm;   /* steal buffers */
        free(bm);
    } else {
        *ud = bm;
    }
    lua_pushboolean(L, 1);
    return 1;
}

static void push_bitmap_ud(lua_State *L, LCDBitmap *bm) {
    LCDBitmap **ud = lua_newuserdata(L, sizeof(LCDBitmap *));
    *ud = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
}

#define META_IMAGETABLE "pd.imagetable"

static int imagetable_meta_index(lua_State *L) {
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int imagetable_meta_newindex(lua_State *L) {
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
    return 0;
}

static int imagetable_meta_len(lua_State *L) {
    lua_getiuservalue(L, 1, 1);
    lua_getfield(L, -1, "_count");
    if (!lua_isnumber(L, -1)) {
        lua_pop(L, 1);
        lua_pushinteger(L, (lua_Integer)lua_rawlen(L, -1));
    }
    return 1;
}

/* Replace the table on top of the stack with a userdata proxy: SDK
   imagetables are userdata and some games (e.g. autotable libs)
   type-check for that. */
static void imagetable_wrap(lua_State *L) {
    lua_newuserdatauv(L, 1, 1);
    lua_insert(L, -2);
    lua_setiuservalue(L, -2, 1);
    if (luaL_newmetatable(L, META_IMAGETABLE)) {
        lua_pushcfunction(L, imagetable_meta_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, imagetable_meta_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, imagetable_meta_len);
        lua_setfield(L, -2, "__len");
    }
    lua_setmetatable(L, -2);
}

/* Push the backing table of an imagetable (userdata proxy or legacy plain
   table) at idx; returns the absolute stack index of the pushed table. */
static int imagetable_data(lua_State *L, int idx) {
    idx = lua_absindex(L, idx);
    if (luaL_testudata(L, idx, META_IMAGETABLE)) {
        lua_getiuservalue(L, idx, 1);
    } else {
        luaL_checktype(L, idx, LUA_TTABLE);
        lua_pushvalue(L, idx);
    }
    return lua_gettop(L);
}

static int imagetable_getImage(lua_State *L) {
    int t = imagetable_data(L, 1);
    lua_Integer n;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3) && t > 3) {
        lua_Integer x = luaL_checkinteger(L, 2);
        lua_Integer y = luaL_checkinteger(L, 3);
        lua_getfield(L, t, "_cellsWide");
        lua_Integer cw = lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (cw < 1) cw = 1;
        n = (y - 1) * cw + x;
    } else {
        n = (lua_Integer)luaL_checknumber(L, 2);
    }
    lua_getfield(L, t, "_count");
    lua_Integer count = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (count > 0) {
        if (n < 1) n = 1;
        if (n > count) n = count;
    }
    lua_rawgeti(L, t, n);
    return 1;
}

static int imagetable_setImage(lua_State *L) {
    int t = imagetable_data(L, 1);
    lua_Integer n = luaL_checkinteger(L, 2);
    lua_pushvalue(L, 3);
    lua_rawseti(L, t, n);
    return 0;
}

static int imagetable_getLength(lua_State *L) {
    int t = imagetable_data(L, 1);
    lua_getfield(L, t, "_count");
    return 1;
}

static int imagetable_getSize(lua_State *L) {
    int t = imagetable_data(L, 1);
    lua_getfield(L, t, "_cellsWide");
    lua_Integer cw = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, t, "_count");
    lua_Integer count = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (cw < 1) cw = count > 0 ? count : 1;
    lua_pushinteger(L, cw);
    lua_pushinteger(L, (count + cw - 1) / cw);
    return 2;
}

/* SDK accepts flip as kBitmap* number or string: "flipX", "flipY", "flipXY" */
int pd_flip_arg(lua_State *L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        const char *s = lua_tostring(L, idx);
        if (strcmp(s, "flipX") == 0) return kBitmapFlippedX;
        if (strcmp(s, "flipY") == 0) return kBitmapFlippedY;
        if (strcmp(s, "flipXY") == 0) return kBitmapFlippedXY;
        return kBitmapUnflipped;
    }
    return (int)luaL_optinteger(L, idx, kBitmapUnflipped);
}

static int imagetable_drawImage(lua_State *L) {
    /* Read all args before imagetable_data pushes the backing table,
       which would otherwise occupy the optional flip slot (index 5). */
    lua_Integer n = luaL_checkinteger(L, 2);
    int x = (int)luaL_checknumber(L, 3);
    int y = (int)luaL_checknumber(L, 4);
    int flip = lua_gettop(L) >= 5 ? pd_flip_arg(L, 5) : kBitmapUnflipped;
    int t = imagetable_data(L, 1);
    lua_rawgeti(L, t, n);
    LCDBitmap **ud = luaL_testudata(L, -1, "pd.bitmap");
    if (ud && *ud) pd_draw_bitmap_at(*ud, x, y, (LCDBitmapFlip)flip, g_pd.draw_mode);
    lua_pop(L, 1);
    return 0;
}

static void imagetable_set_methods(lua_State *L) {
    lua_pushcfunction(L, imagetable_getImage);
    lua_setfield(L, -2, "getImage");
    lua_pushcfunction(L, imagetable_setImage);
    lua_setfield(L, -2, "setImage");
    lua_pushcfunction(L, imagetable_getLength);
    lua_setfield(L, -2, "getLength");
    lua_pushcfunction(L, imagetable_getSize);
    lua_setfield(L, -2, "getSize");
    lua_pushcfunction(L, imagetable_drawImage);
    lua_setfield(L, -2, "drawImage");
}

static int pdt_body_into_table(lua_State *L, const uint8_t *body, long body_len);

/* Look up an asset entry in the loaded PDZ by path (extension stripped). */
static PDZEntry *find_pdz_asset(const char *path, int type) {
    if (!g_pd.pdz) return NULL;
    char name[512];
    snprintf(name, sizeof(name), "%s", path);
    char *dot = strrchr(name, '.');
    if (dot && (strcmp(dot, ".pdi") == 0 || strcmp(dot, ".pdt") == 0 ||
                strcmp(dot, ".pft") == 0 || strcmp(dot, ".png") == 0 ||
                strcmp(dot, ".gif") == 0))
        *dot = 0;
    /* pdc strips "-table-<w>-<h>" from imagetable names */
    if (type == 3) {
        char *tbl = strstr(name, "-table-");
        if (tbl) *tbl = 0;
    }
    PDZEntry *e = pdz_find((PDZFile *)g_pd.pdz, name);
    return (e && e->entry_type == type && e->data) ? e : NULL;
}

int load_pdt_into_table(lua_State *L, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char magic[12];
    if (fread(magic, 1, 12, f) != 12 || memcmp(magic, "Playdate IMT", 12) != 0) {
        fclose(f);
        return 0;
    }
    uint32_t flags = 0;
    fread(&flags, 4, 1, f);
    int compressed = (flags & 0x80000000u) != 0;

    uint8_t *body = NULL;
    long body_len = 0;
    if (compressed) {
        uint32_t decomp_size = 0, iw = 0, ih = 0, icount = 0;
        fread(&decomp_size, 4, 1, f);
        fread(&iw, 4, 1, f);
        fread(&ih, 4, 1, f);
        fread(&icount, 4, 1, f);
        (void)iw; (void)ih; (void)icount;
        fseek(f, 0, SEEK_END);
        long fsz = ftell(f);
        fseek(f, 32, SEEK_SET);
        long comp_len = fsz - 32;
        uint8_t *comp = malloc(comp_len);
        fread(comp, 1, comp_len, f);
        fclose(f);
        body = malloc(decomp_size + 64);
        uLongf dst_len = decomp_size + 64;
        if (uncompress(body, &dst_len, comp, comp_len) != Z_OK) {
            free(comp);
            free(body);
            return 0;
        }
        free(comp);
        body_len = (long)dst_len;
    } else {
        fseek(f, 0, SEEK_END);
        long fsz = ftell(f);
        fseek(f, 16, SEEK_SET);
        body_len = fsz - 16;
        body = malloc(body_len);
        fread(body, 1, body_len, f);
        fclose(f);
    }

    int ok = pdt_body_into_table(L, body, body_len);
    free(body);
    return ok;
}

static int pdt_body_into_table(lua_State *L, const uint8_t *body, long body_len) {
    if (body_len < 4) return 0;
    uint16_t ncells, cellswide;
    memcpy(&ncells, body + 0, 2);
    memcpy(&cellswide, body + 2, 2);
    long offsets_end = 4 + 4L * ncells;
    if (ncells == 0 || body_len < offsets_end) return 0;

    lua_newtable(L);
    lua_pushinteger(L, ncells);
    lua_setfield(L, -2, "_count");
    lua_pushinteger(L, cellswide);
    lua_setfield(L, -2, "_cellsWide");
    imagetable_set_methods(L);

    const uint8_t *cells = body + offsets_end;
    long cells_len = body_len - offsets_end;
    uint32_t prev = 0;
    for (int i = 0; i < ncells; i++) {
        uint32_t end;
        memcpy(&end, body + 4 + 4L * i, 4);
        if (end <= prev || (long)end > cells_len) break;
        LCDBitmap *bm = parse_pdi_cell(cells + prev, (long)(end - prev));
        if (bm) {
            push_bitmap_ud(L, bm);
            lua_rawseti(L, -2, i + 1);
        }
        prev = end;
    }
    imagetable_wrap(L);
    return 1;
}

/* ---- tilemap ---- */

static void tilemap_cell_size(lua_State *L, int self_idx, int *cw, int *ch) {
    *cw = 8; *ch = 8;
    lua_getfield(L, self_idx, "_imagetable");
    if (!lua_isnil(L, -1)) {
        int t = imagetable_data(L, -1);
        lua_rawgeti(L, t, 1);
        LCDBitmap **ud = luaL_testudata(L, -1, "pd.bitmap");
        if (ud && *ud) { *cw = (*ud)->width; *ch = (*ud)->height; }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
}

static int lua_tilemap_setImageTable(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "_imagetable");
    return 0;
}

static int lua_tilemap_setTiles(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);
    int width = (int)luaL_checkinteger(L, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "_tiles");
    lua_pushinteger(L, width);
    lua_setfield(L, 1, "_width");
    lua_Integer n = lua_rawlen(L, 2);
    lua_pushinteger(L, width > 0 ? (n + width - 1) / width : 0);
    lua_setfield(L, 1, "_height");
    return 0;
}

static int lua_tilemap_getTiles(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "_tiles");
    lua_getfield(L, 1, "_width");
    return 2;
}

static int lua_tilemap_setSize(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "_width");
    lua_pushvalue(L, 3);
    lua_setfield(L, 1, "_height");
    lua_getfield(L, 1, "_tiles");
    if (!lua_istable(L, -1)) {
        lua_newtable(L);
        lua_setfield(L, 1, "_tiles");
    }
    lua_pop(L, 1);
    return 0;
}

static int lua_tilemap_getSize(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "_width");
    lua_getfield(L, 1, "_height");
    return 2;
}

static int lua_tilemap_getPixelSize(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int cw, ch;
    tilemap_cell_size(L, 1, &cw, &ch);
    lua_getfield(L, 1, "_width");
    lua_getfield(L, 1, "_height");
    lua_pushinteger(L, lua_tointeger(L, -2) * cw);
    lua_pushinteger(L, lua_tointeger(L, -2) * ch);
    return 2;
}

static int lua_tilemap_getTileSize(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int cw, ch;
    tilemap_cell_size(L, 1, &cw, &ch);
    lua_pushinteger(L, cw);
    lua_pushinteger(L, ch);
    return 2;
}

static int lua_tilemap_setTileAtPosition(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    /* SDK tolerates float coordinates (games pass computed positions) */
    lua_Integer x = (lua_Integer)floor(luaL_checknumber(L, 2));
    lua_Integer y = (lua_Integer)floor(luaL_checknumber(L, 3));
    lua_getfield(L, 1, "_width");
    lua_Integer w = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_height");
    lua_Integer h = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_tiles");
    if (lua_istable(L, -1) && w > 0 && x >= 1 && x <= w && y >= 1 && (h <= 0 || y <= h)) {
        lua_pushvalue(L, 4);
        lua_rawseti(L, -2, (y - 1) * w + x);
    }
    lua_pop(L, 1);
    return 0;
}

static int lua_tilemap_getTileAtPosition(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer x = (lua_Integer)floor(luaL_checknumber(L, 2));
    lua_Integer y = (lua_Integer)floor(luaL_checknumber(L, 3));
    lua_getfield(L, 1, "_width");
    lua_Integer w = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_tiles");
    if (lua_istable(L, -1) && w > 0 && x >= 1 && x <= w && y >= 1) {
        lua_rawgeti(L, -1, (y - 1) * w + x);
        return 1;
    }
    lua_pop(L, 1);
    lua_pushnil(L);
    return 1;
}

static int lua_tilemap_draw(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int x = (int)luaL_optnumber(L, 2, 0);
    int y = (int)luaL_optnumber(L, 3, 0);
    int cw, ch;
    tilemap_cell_size(L, 1, &cw, &ch);
    lua_getfield(L, 1, "_width");
    lua_Integer w = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_imagetable");
    int it_raw = lua_gettop(L);
    if (lua_isnil(L, it_raw)) {
        lua_pop(L, 1);
        return 0;
    }
    int it = imagetable_data(L, it_raw);
    lua_getfield(L, 1, "_tiles");
    int tiles = lua_gettop(L);
    if (!lua_istable(L, it) || !lua_istable(L, tiles) || w <= 0) {
        lua_pop(L, 3);
        return 0;
    }
    /* tiles can be sparse (setTileAtPosition on selected cells), so rawlen
       is unreliable; iterate the full grid */
    lua_getfield(L, 1, "_height");
    lua_Integer hgt = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_Integer n = lua_rawlen(L, tiles);
    if (hgt > 0 && w * hgt > n) n = w * hgt;
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, tiles, i);
        lua_Integer idx = lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (idx <= 0) continue;
        lua_rawgeti(L, it, idx);
        LCDBitmap **ud = luaL_testudata(L, -1, "pd.bitmap");
        if (ud && *ud) {
            int col = (int)((i - 1) % w);
            int row = (int)((i - 1) / w);
            pd_draw_bitmap_at(*ud, x + col * cw, y + row * ch, kBitmapUnflipped, g_pd.draw_mode);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 3);
    return 0;
}

static int lua_tilemap_getCollisionRects(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int has_empty = lua_istable(L, 2);
    lua_getfield(L, 1, "_width");
    lua_Integer w = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_height");
    lua_Integer h = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, 1, "_tiles");
    int tiles = lua_gettop(L);
    lua_newtable(L);
    int out = lua_gettop(L);
    if (!lua_istable(L, tiles) || w <= 0) {
        lua_remove(L, tiles);
        return 1;
    }
    int nrects = 0;
    for (lua_Integer row = 0; row < h; row++) {
        lua_Integer run_start = -1;
        for (lua_Integer col = 0; col <= w; col++) {
            int solid = 0;
            if (col < w) {
                lua_rawgeti(L, tiles, row * w + col + 1);
                lua_Integer idx = lua_tointeger(L, -1);
                lua_pop(L, 1);
                solid = idx > 0;
                if (solid && has_empty) {
                    lua_Integer en = lua_rawlen(L, 2);
                    for (lua_Integer e = 1; e <= en; e++) {
                        lua_rawgeti(L, 2, e);
                        if (lua_tointeger(L, -1) == idx) solid = 0;
                        lua_pop(L, 1);
                    }
                }
            }
            if (solid && run_start < 0) run_start = col;
            if (!solid && run_start >= 0) {
                lua_newtable(L);
                lua_pushinteger(L, run_start);
                lua_setfield(L, -2, "x");
                lua_pushinteger(L, row);
                lua_setfield(L, -2, "y");
                lua_pushinteger(L, col - run_start);
                lua_setfield(L, -2, "width");
                lua_pushinteger(L, 1);
                lua_setfield(L, -2, "height");
                lua_rawseti(L, out, ++nrects);
                if (getenv("PD_TRACE"))
                    fprintf(stderr, "[colrect row=%d x=%d w=%d]", (int)row, (int)run_start, (int)(col - run_start));
                run_start = -1;
            }
        }
    }
    if (getenv("PD_TRACE")) fprintf(stderr, "[colrects map=%dx%d n=%d]\n", (int)w, (int)h, nrects);
    lua_remove(L, tiles);
    return 1;
}

static int lua_tilemap_new(lua_State *L);

/* Tilemap instances get metatable == playdate.graphics.tilemap so that
   CoreLibs' `getmetatable(x) == gfx.tilemap` type checks work. */
static void tilemap_push_class(lua_State *L) {
    if (luaL_newmetatable(L, "pd.tilemapclass")) {
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        static const luaL_Reg tm_methods[] = {
            {"setImageTable", lua_tilemap_setImageTable},
            {"setTiles", lua_tilemap_setTiles},
            {"getTiles", lua_tilemap_getTiles},
            {"setSize", lua_tilemap_setSize},
            {"getSize", lua_tilemap_getSize},
            {"getPixelSize", lua_tilemap_getPixelSize},
            {"getTileSize", lua_tilemap_getTileSize},
            {"setTileAtPosition", lua_tilemap_setTileAtPosition},
            {"getTileAtPosition", lua_tilemap_getTileAtPosition},
            {"draw", lua_tilemap_draw},
            {"drawIgnoringOffset", lua_tilemap_draw},
            {"getCollisionRects", lua_tilemap_getCollisionRects},
            {NULL, NULL}
        };
        luaL_setfuncs(L, tm_methods, 0);
        lua_pushcfunction(L, lua_tilemap_new);
        lua_setfield(L, -2, "new");
    }
}

static int lua_tilemap_new(lua_State *L) {
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "_width");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "_height");
    tilemap_push_class(L);
    lua_setmetatable(L, -2);
    return 1;
}

/* ---- nineSlice: stretchable bordered panel (dialog boxes etc.) ---- */

#define META_NINESLICE "pd.nineslice"

typedef struct {
    LCDBitmap *bm;
    int ix, iy, iw, ih; /* inner (stretchable) rect */
} PDNineSlice;

static int lua_nineslice_new(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    int ix = (int)luaL_checknumber(L, 2);
    int iy = (int)luaL_checknumber(L, 3);
    int iw = (int)luaL_checknumber(L, 4);
    int ih = (int)luaL_checknumber(L, 5);
    lua_settop(L, 0);
    lua_pushcfunction(L, lua_gfx_loadBitmap);
    lua_pushstring(L, path);
    lua_call(L, 1, 1); /* image userdata (or nil) */
    LCDBitmap **iud = luaL_testudata(L, -1, "pd.bitmap");
    if (!iud || !*iud) {
        lua_pushnil(L);
        lua_pushstring(L, "could not load nineSlice image");
        return 2;
    }
    PDNineSlice *ns = (PDNineSlice *)lua_newuserdatauv(L, sizeof(PDNineSlice), 1);
    ns->bm = *iud;
    ns->ix = ix; ns->iy = iy; ns->iw = iw > 0 ? iw : 1; ns->ih = ih > 0 ? ih : 1;
    lua_pushvalue(L, -2);
    lua_setiuservalue(L, -2, 1); /* keep the image alive */
    luaL_setmetatable(L, META_NINESLICE);
    return 1;
}

/* Map a destination coordinate to a source coordinate for one axis. */
static int nineslice_map(int d, int dlen, int slen, int inner_start, int inner_len) {
    int tail = slen - inner_start - inner_len; /* far border size */
    if (d < inner_start) return d;
    if (d >= dlen - tail) return slen - (dlen - d);
    int dst_inner = dlen - inner_start - tail;
    if (dst_inner <= 0) return inner_start;
    long s = inner_start + (long)(d - inner_start) * inner_len / dst_inner;
    if (s >= inner_start + inner_len) s = inner_start + inner_len - 1;
    return (int)s;
}

static int lua_nineslice_drawInRect(lua_State *L) {
    PDNineSlice *ns = (PDNineSlice *)luaL_checkudata(L, 1, META_NINESLICE);
    int x, y, w, h;
    PDRectInternal *r = luaL_testudata(L, 2, "pd.rect");
    if (r) {
        x = (int)r->x; y = (int)r->y; w = (int)r->width; h = (int)r->height;
    } else {
        x = (int)luaL_checknumber(L, 2);
        y = (int)luaL_checknumber(L, 3);
        w = (int)luaL_checknumber(L, 4);
        h = (int)luaL_checknumber(L, 5);
    }
    LCDBitmap *bm = ns->bm;
    if (!bm || w <= 0 || h <= 0) return 0;
    for (int py = 0; py < h; py++) {
        int sy = nineslice_map(py, h, bm->height, ns->iy, ns->ih);
        if (sy < 0 || sy >= bm->height) continue;
        for (int px = 0; px < w; px++) {
            int sx = nineslice_map(px, w, bm->width, ns->ix, ns->iw);
            if (sx < 0 || sx >= bm->width) continue;
            if (bm->mask) {
                int mbyte = sy * bm->rowbytes + (sx / 8);
                if (!((bm->mask[mbyte] >> (7 - (sx % 8))) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, sx, sy);
            draw_pixel_clip(x + px, y + py, pixel ? kColorBlack : kColorWhite);
        }
    }
    return 0;
}

static int lua_nineslice_getSize(lua_State *L) {
    PDNineSlice *ns = (PDNineSlice *)luaL_checkudata(L, 1, META_NINESLICE);
    lua_pushinteger(L, ns->bm ? ns->bm->width : 0);
    lua_pushinteger(L, ns->bm ? ns->bm->height : 0);
    return 2;
}

static int lua_nineslice_getMinSize(lua_State *L) {
    PDNineSlice *ns = (PDNineSlice *)luaL_checkudata(L, 1, META_NINESLICE);
    if (!ns->bm) { lua_pushinteger(L, 0); lua_pushinteger(L, 0); return 2; }
    lua_pushinteger(L, ns->bm->width - ns->iw + 2);
    lua_pushinteger(L, ns->bm->height - ns->ih + 2);
    return 2;
}

static int lua_gfx_newBitmapTable(lua_State *L) {
    if (lua_type(L, 1) == LUA_TNUMBER) {
        lua_Integer count = luaL_checkinteger(L, 1);
        lua_Integer cw = luaL_optinteger(L, 2, 1);
        lua_newtable(L);
        lua_pushinteger(L, count);
        lua_setfield(L, -2, "_count");
        lua_pushinteger(L, cw);
        lua_setfield(L, -2, "_cellsWide");
        imagetable_set_methods(L);
        imagetable_wrap(L);
        return 1;
    }
    const char *path = luaL_checkstring(L, 1);
    char base[1024];
    const char *dot = strrchr(path, '.');
    if (dot && (strcmp(dot, ".png") == 0 || strcmp(dot, ".pdt") == 0 || strcmp(dot, ".gif") == 0))
        snprintf(base, sizeof(base), "%.*s", (int)(dot - path), path);
    else
        snprintf(base, sizeof(base), "%s", path);

    char fullpath[1200];
    snprintf(fullpath, sizeof(fullpath), "%s/%s.pdt", g_pd.pdx_dir, base);
    if (access(fullpath, F_OK) != 0) {
        /* pdc strips "-table-<w>-<h>" from output filenames */
        char *tbl = strstr(base, "-table-");
        if (tbl) {
            *tbl = '\0';
            snprintf(fullpath, sizeof(fullpath), "%s/%s.pdt", g_pd.pdx_dir, base);
        }
    }
    if (access(fullpath, F_OK) != 0) pd_fix_path_case(fullpath);
    if (load_pdt_into_table(L, fullpath)) return 1;
    snprintf(fullpath, sizeof(fullpath), "%s/%s.pdt", g_pd.save_dir, base);
    if (load_pdt_into_table(L, fullpath)) return 1;
    PDZEntry *e = find_pdz_asset(path, 3);
    if (e && pdt_body_into_table(L, e->data, (long)e->size)) return 1;
    fprintf(stderr, "[imgtable] FAILED %s ", fullpath);
    lua_pushnil(L);
    lua_pushstring(L, "could not load imagetable");
    return 2;
}

/* image:drawSampled(x, y, w, h, cx, cy, dxx, dyx, dxy, dyy, dx, dy, z,
   tiltAngle, [tile]) — perspective plane sampler (mode-7 style).
   Normalized dest coords around (cx,cy) go through the 2x2 matrix, get a
   tilt-dependent perspective divide, then (dx,dy) picks the sample origin
   in normalized image space. */
static int lua_gfx_bitmapDrawSampled(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    int x = (int)luaL_checknumber(L, 2);
    int y = (int)luaL_checknumber(L, 3);
    int w = (int)luaL_checknumber(L, 4);
    int h = (int)luaL_checknumber(L, 5);
    float cx = (float)luaL_checknumber(L, 6);
    float cy = (float)luaL_checknumber(L, 7);
    float dxx = (float)luaL_checknumber(L, 8);
    float dyx = (float)luaL_checknumber(L, 9);
    float dxy = (float)luaL_checknumber(L, 10);
    float dyy = (float)luaL_checknumber(L, 11);
    float dx = (float)luaL_checknumber(L, 12);
    float dy = (float)luaL_checknumber(L, 13);
    float dz = (float)luaL_optnumber(L, 14, 1.0);
    float tilt = (float)luaL_optnumber(L, 15, 0);
    int tile = lua_toboolean(L, 16);
    if (!bm || w <= 0 || h <= 0) return 0;
    float tilt_sin = sinf(tilt * (float)M_PI / 180.0f);
    for (int i = 0; i < h; i++) {
        float v = (i + 0.5f) / (float)h - cy;
        float z = dz + v * tilt_sin;
        if (z <= 0.0001f) continue;
        for (int j = 0; j < w; j++) {
            float u = (j + 0.5f) / (float)w - cx;
            float su = (dxx * u + dxy * v) * dz / z + dx;
            float sv = (dyx * u + dyy * v) * dz / z + dy;
            int sx = (int)floorf(su * (float)bm->width);
            int sy = (int)floorf(sv * (float)bm->height);
            if (tile) {
                sx %= bm->width;
                if (sx < 0) sx += bm->width;
                sy %= bm->height;
                if (sy < 0) sy += bm->height;
            } else if (sx < 0 || sy < 0 || sx >= bm->width || sy >= bm->height) {
                continue;
            }
            if (bm->mask) {
                int mbyte = sy * bm->rowbytes + (sx / 8);
                if (!((bm->mask[mbyte] >> (7 - (sx % 8))) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, sx, sy);
            draw_pixel_clip(x + j, y + i, pixel ? kColorBlack : kColorWhite);
        }
    }
    return 0;
}

static const uint8_t g_bayer8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21},
};

static int lua_gfx_bitmapDrawFaded(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int x = (int)luaL_checknumber(L, 2);
    int y = (int)luaL_checknumber(L, 3);
    float alpha = (float)luaL_optnumber(L, 4, 1.0);
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    int saved_pattern = g_has_pattern;
    g_has_pattern = 0;
    for (int py = 0; py < bm->height; py++) {
        for (int px = 0; px < bm->width; px++) {
            if ((float)g_bayer8[py % 8][px % 8] >= alpha * 64.0f) continue;
            if (bm->mask) {
                int mbyte = py * bm->rowbytes + (px / 8);
                int mbit = 7 - (px % 8);
                if (!((bm->mask[mbyte] >> mbit) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, px, py);
            draw_pixel_clip(x + px, y + py, pixel ? kColorBlack : kColorWhite);
        }
    }
    g_has_pattern = saved_pattern;
    return 0;
}

static int lua_gfx_bitmapSetInverted(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int flag = lua_toboolean(L, 2);
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    if (flag != bm->inverted) {
        for (long i = 0; i < (long)bm->rowbytes * bm->height; i++)
            bm->data[i] = (uint8_t)~bm->data[i];
        bm->inverted = flag;
    }
    return 0;
}

static int lua_gfx_bitmapCopy(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *src = *ud;
    if (!src) { lua_pushnil(L); return 1; }
    LCDBitmap *bm = create_bitmap(src->width, src->height);
    memcpy(bm->data, src->data, (size_t)src->rowbytes * src->height);
    if (src->mask) memcpy(bm->mask, src->mask, (size_t)src->rowbytes * src->height);
    LCDBitmap **out = lua_newuserdata(L, sizeof(LCDBitmap *));
    *out = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int bitmap_get_mask_px(LCDBitmap *bm, int x, int y) {
    if (!bm->mask) return 1;
    if (x < 0 || x >= bm->width || y < 0 || y >= bm->height) return 0;
    int byte_idx = y * bm->rowbytes + (x / 8);
    int bit_idx = 7 - (x % 8);
    return (bm->mask[byte_idx] >> bit_idx) & 1;
}

static int push_new_bitmap(lua_State *L, LCDBitmap *bm) {
    LCDBitmap **out = lua_newuserdata(L, sizeof(LCDBitmap *));
    *out = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

/* image:scaledImage(scale [, yScale]) — nearest neighbor */
static int lua_gfx_bitmapScaledImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *src = *ud;
    float sx = (float)luaL_checknumber(L, 2);
    float sy = (float)luaL_optnumber(L, 3, sx);
    if (!src) { lua_pushnil(L); return 1; }
    if (sx <= 0 || sy <= 0) {
        /* zero/negative scale: valid fully-transparent 1x1 (device never
           returns nil here; games index the result during shrink anims) */
        LCDBitmap *bm = create_bitmap(1, 1);
        bitmap_set_mask(bm, 0, 0, 0);
        return push_new_bitmap(L, bm);
    }
    int nw = (int)(src->width * sx + 0.5f);
    int nh = (int)(src->height * sy + 0.5f);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    LCDBitmap *bm = create_bitmap(nw, nh);
    for (int y = 0; y < nh; y++) {
        int syi = (int)(y / sy);
        for (int x = 0; x < nw; x++) {
            int sxi = (int)(x / sx);
            bitmap_set_pixel(bm, x, y, bitmap_get_pixel(src, sxi, syi));
            bitmap_set_mask(bm, x, y, bitmap_get_mask_px(src, sxi, syi));
        }
    }
    return push_new_bitmap(L, bm);
}

/* image:rotatedImage(angle [, scale [, yScale]]) — output sized to fit */
static int lua_gfx_bitmapRotatedImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *src = *ud;
    float angle = (float)luaL_checknumber(L, 2);
    float sx = (float)luaL_optnumber(L, 3, 1.0);
    float sy = (float)luaL_optnumber(L, 4, sx);
    if (!src) { lua_pushnil(L); return 1; }
    if (sx <= 0 || sy <= 0) {
        LCDBitmap *bm = create_bitmap(1, 1);
        bitmap_set_mask(bm, 0, 0, 0);
        push_new_bitmap(L, bm);
        lua_pushinteger(L, 1);
        lua_pushinteger(L, 1);
        return 3;
    }
    float rad = angle * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float w = src->width * sx, h = src->height * sy;
    int nw = (int)(fabsf(w * c) + fabsf(h * s) + 0.5f);
    int nh = (int)(fabsf(w * s) + fabsf(h * c) + 0.5f);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    LCDBitmap *bm = create_bitmap(nw, nh);
    float cx = nw / 2.0f, cy = nh / 2.0f;
    float scx = src->width / 2.0f, scy = src->height / 2.0f;
    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            /* inverse rotate + inverse scale back into source space */
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float ux = ( dx * c + dy * s) / sx + scx;
            float uy = (-dx * s + dy * c) / sy + scy;
            int sxi = (int)ux, syi = (int)uy;
            if (ux < 0 || uy < 0 || sxi >= src->width || syi >= src->height) {
                bitmap_set_mask(bm, x, y, 0);
            } else {
                bitmap_set_pixel(bm, x, y, bitmap_get_pixel(src, sxi, syi));
                bitmap_set_mask(bm, x, y, bitmap_get_mask_px(src, sxi, syi));
            }
        }
    }
    push_new_bitmap(L, bm);
    lua_pushinteger(L, nw);
    lua_pushinteger(L, nh);
    return 3;
}

static int lua_gfx_bitmapInvertedImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *src = *ud;
    if (!src) { lua_pushnil(L); return 1; }
    LCDBitmap *bm = create_bitmap(src->width, src->height);
    for (long i = 0; i < (long)src->rowbytes * src->height; i++)
        bm->data[i] = (uint8_t)~src->data[i];
    if (src->mask) memcpy(bm->mask, src->mask, (size_t)src->rowbytes * src->height);
    LCDBitmap **out = lua_newuserdata(L, sizeof(LCDBitmap *));
    *out = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_setMaskImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap **mud = luaL_checkudata(L, 2, "pd.bitmap");
    LCDBitmap *bm = *ud, *mask = *mud;
    if (!bm || !mask) return 0;
    if (!bm->mask) bm->mask = malloc((size_t)bm->rowbytes * bm->height);
    for (int y = 0; y < bm->height; y++) {
        for (int x = 0; x < bm->width; x++) {
            int visible = 1;
            if (x < mask->width && y < mask->height)
                visible = !bitmap_get_pixel(mask, x, y); /* white = opaque */
            int byte_i = y * bm->rowbytes + x / 8;
            int bit_i = 7 - (x % 8);
            if (visible) bm->mask[byte_i] |= (uint8_t)(1 << bit_i);
            else bm->mask[byte_i] &= (uint8_t)~(1 << bit_i);
        }
    }
    return 0;
}

static int lua_gfx_getMaskImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    if (!bm || !bm->mask) { lua_pushnil(L); return 1; }
    LCDBitmap *m = create_bitmap(bm->width, bm->height);
    for (int y = 0; y < bm->height; y++) {
        for (int x = 0; x < bm->width; x++) {
            int byte_i = y * bm->rowbytes + x / 8;
            int bit_i = 7 - (x % 8);
            int visible = (bm->mask[byte_i] >> bit_i) & 1;
            bitmap_set_pixel(m, x, y, !visible);
        }
    }
    LCDBitmap **out = lua_newuserdata(L, sizeof(LCDBitmap *));
    *out = m;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_bitmapAddMask(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    if (!bm->mask) {
        bm->mask = malloc((size_t)bm->rowbytes * bm->height);
        if (bm->mask) memset(bm->mask, 0xFF, (size_t)bm->rowbytes * bm->height);
    }
    return 0;
}

static int lua_gfx_bitmapClearMask(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    int color = (int)luaL_optinteger(L, 2, kColorWhite);
    if (!bm->mask) bm->mask = malloc((size_t)bm->rowbytes * bm->height);
    if (bm->mask)
        memset(bm->mask, color == kColorBlack ? 0xFF : 0x00,
               (size_t)bm->rowbytes * bm->height);
    return 0;
}

static int lua_gfx_bitmapRemoveMask(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    free(bm->mask);
    bm->mask = NULL;
    return 0;
}

static int lua_gfx_bitmapHasMask(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = ud ? *ud : NULL;
    lua_pushboolean(L, bm && bm->mask != NULL);
    return 1;
}

static int lua_gfx_bitmapClear(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int color = (int)luaL_optinteger(L, 2, kColorWhite);
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    memset(bm->data, color == kColorBlack ? 0xFF : 0x00, (size_t)bm->rowbytes * bm->height);
    if (bm->mask)
        memset(bm->mask, color == kColorClear ? 0x00 : 0xFF, (size_t)bm->rowbytes * bm->height);
    return 0;
}

static int lua_gfx_freeBitmap(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    if (ud && *ud) { free_bitmap(*ud); *ud = NULL; }
    return 0;
}

static int lua_gfx_drawBitmap(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int x, y, flip;
    PDPoint *pt = luaL_testudata(L, 2, "pd.point");
    if (pt) {
        /* SDK: image:draw(point, [flip]) */
        x = (int)pt->x;
        y = (int)pt->y;
        flip = pd_flip_arg(L, 3);
    } else {
        x = (int)luaL_optnumber(L, 2, 0);
        y = (int)luaL_optnumber(L, 3, 0);
        flip = pd_flip_arg(L, 4);
    }
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    pd_draw_bitmap_at(bm, x, y, (LCDBitmapFlip)flip, g_pd.draw_mode);
    return 0;
}

static int lua_gfx_drawBitmapIgnoringOffset(lua_State *L) {
    int ox = g_pd.draw_offset_x, oy = g_pd.draw_offset_y;
    g_pd.draw_offset_x = 0;
    g_pd.draw_offset_y = 0;
    int r = lua_gfx_drawBitmap(L);
    g_pd.draw_offset_x = ox;
    g_pd.draw_offset_y = oy;
    return r;
}

static int lua_gfx_getSize(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    lua_pushinteger(L, bm->width);
    lua_pushinteger(L, bm->height);
    return 2;
}

static int lua_gfx_getPixel(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int x = (int)luaL_checknumber(L, 2);
    int y = (int)luaL_checknumber(L, 3);
    LCDBitmap *bm = *ud;
    /* mask bit clear = transparent pixel */
    if (bm->mask && x >= 0 && x < bm->width && y >= 0 && y < bm->height) {
        int byte_idx = y * bm->rowbytes + (x / 8);
        if (!((bm->mask[byte_idx] >> (7 - (x % 8))) & 1)) {
            lua_pushinteger(L, kColorClear);
            return 1;
        }
    }
    /* data bit set = black ink */
    lua_pushinteger(L, bitmap_get_pixel(bm, x, y) ? kColorBlack : kColorWhite);
    return 1;
}

static int lua_gfx_bitmapFadedImage(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    lua_pushvalue(L, 1);
    return 1;
}

static int lua_gfx_bitmapGetSizeInternal(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    lua_pushinteger(L, (*ud)->width);
    lua_pushinteger(L, (*ud)->height);
    return 2;
}

static int lua_gfx_bitmapSample(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    LCDBitmap *bm = *ud;
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    if (!bm || x < 0 || x >= bm->width || y < 0 || y >= bm->height)
        { lua_pushnil(L); return 1; }
    int byte_i = y * bm->rowbytes + (x / 8);
    int bit_i = 7 - (x % 8);
    int pixel = (bm->data[byte_i] >> bit_i) & 1;
    if (bm->mask) {
        int masked = (bm->mask[byte_i] >> bit_i) & 1;
        if (!masked) { lua_pushnil(L); return 1; }
    }
    lua_pushinteger(L, pixel ? kColorBlack : kColorWhite);
    return 1;
}

static int lua_gfx_loadFont(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_pd.pdx_dir, path);
    if (access(fullpath, F_OK) != 0)
        snprintf(fullpath, sizeof(fullpath), "%s/%s.pft", g_pd.pdx_dir, path);
    if (access(fullpath, F_OK) != 0) pd_fix_path_case(fullpath);
    LCDFont *font = pd_load_pft(fullpath);
    if (!font) {
        PDZEntry *e = find_pdz_asset(path, 7);
        if (e) font = pd_font_from_body(e->data, (long)e->size);
    }
    if (!font) {
        font = calloc(1, sizeof(LCDFont));
        font->glyph_width = 8;
        font->glyph_height = 12;
    }
    LCDFont **ud = lua_newuserdata(L, sizeof(LCDFont *));
    *ud = font;
    luaL_getmetatable(L, "pd.font");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_setFont(lua_State *L) {
    LCDFont **ud = luaL_testudata(L, 1, "pd.font");
    g_pd.current_font = ud ? *ud : NULL;
    return 0;
}

static int lua_gfx_loadFont(lua_State *L);

/* font.newFamily({[variant]=path,...}) -> table of loaded fonts */
static int lua_gfx_newFontFamily(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_newtable(L);
    lua_pushnil(L);
    while (lua_next(L, 1) != 0) {
        /* stack: key, path */
        if (lua_isstring(L, -1)) {
            lua_pushcfunction(L, lua_gfx_loadFont);
            lua_pushvalue(L, -2);
            lua_call(L, 1, 1); /* font ud or nil */
            if (!lua_isnil(L, -1)) {
                lua_pushvalue(L, -3); /* key */
                lua_insert(L, -2);    /* key, font */
                lua_settable(L, 2);
            } else {
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    return 1;
}

/* setFontFamily(family): use the normal variant as the current font */
static int lua_gfx_setFontFamily(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_rawgeti(L, 1, 0); /* kVariantNormal */
    LCDFont **ud = luaL_testudata(L, -1, "pd.font");
    if (!ud) {
        lua_pop(L, 1);
        /* fall back to any entry */
        lua_pushnil(L);
        while (lua_next(L, 1) != 0) {
            ud = luaL_testudata(L, -1, "pd.font");
            if (ud) { lua_pop(L, 2); break; }
            lua_pop(L, 1);
        }
    }
    if (ud && *ud) g_pd.current_font = *ud;
    return 0;
}

static int lua_gfx_getFont(lua_State *L) {
    static LCDFont *fallback_font = NULL;
    if (!g_pd.current_font) {
        if (!fallback_font) {
            fallback_font = calloc(1, sizeof(LCDFont));
            fallback_font->glyph_width = 8;
            fallback_font->glyph_height = 12;
        }
        LCDFont **fud = lua_newuserdata(L, sizeof(LCDFont *));
        *fud = fallback_font;
        luaL_getmetatable(L, "pd.font");
        lua_setmetatable(L, -2);
        return 1;
    }
    LCDFont **ud = lua_newuserdata(L, sizeof(LCDFont *));
    *ud = g_pd.current_font;
    luaL_getmetatable(L, "pd.font");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_getSystemFont(lua_State *L) {
    static LCDFont *sysfont = NULL;
    if (!sysfont) {
        sysfont = calloc(1, sizeof(LCDFont));
        sysfont->glyph_width = 8;
        sysfont->glyph_height = 12;
    }
    LCDFont **ud = lua_newuserdata(L, sizeof(LCDFont *));
    *ud = sysfont;
    luaL_getmetatable(L, "pd.font");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_getFontHeight(lua_State *L) {
    LCDFont **ud = luaL_testudata(L, 1, "pd.font");
    LCDFont *font = ud ? *ud : g_pd.current_font;
    lua_pushinteger(L, font && font->glyph_height > 0 ? font->glyph_height : 12);
    return 1;
}

static int lua_gfx_fontGetTextSize(lua_State *L) {
    LCDFont **ud = luaL_checkudata(L, 1, "pd.font");
    const char *text = luaL_checkstring(L, 2);
    lua_pushinteger(L, pd_font_text_width(*ud, text));
    lua_pushinteger(L, (*ud)->glyph_height);
    return 2;
}
static int lua_gfx_drawText(lua_State *L);

static int lua_gfx_fontDrawText(lua_State *L) {
    LCDFont **ud = luaL_checkudata(L, 1, "pd.font");
    LCDFont *saved = g_pd.current_font;
    g_pd.current_font = *ud;
    lua_remove(L, 1);
    int r = lua_gfx_drawText(L);
    g_pd.current_font = saved;
    return r;
}

static int lua_gfx_fontGetLeading(lua_State *L) {
    (void)luaL_checkudata(L, 1, "pd.font");
    lua_pushinteger(L, 0);
    return 1;
}

static int lua_gfx_fontGetTracking(lua_State *L) {
    (void)luaL_checkudata(L, 1, "pd.font");
    lua_pushinteger(L, 0);
    return 1;
}

static int lua_gfx_fontSetLeading(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_gfx_fontGetTextWidth(lua_State *L) {
    LCDFont **ud = luaL_checkudata(L, 1, "pd.font");
    const char *text = luaL_checkstring(L, 2);
    lua_pushinteger(L, pd_font_text_width(*ud, text));
    return 1;
}

static uint32_t utf8_next(const char *s, int *i) {
    uint8_t c = (uint8_t)s[*i];
    if (c < 0x80) { (*i)++; return c; }
    if ((c & 0xE0) == 0xC0 && (s[*i + 1] & 0xC0) == 0x80) {
        uint32_t cp = ((c & 0x1F) << 6) | (s[*i + 1] & 0x3F);
        *i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && (s[*i + 1] & 0xC0) == 0x80 && (s[*i + 2] & 0xC0) == 0x80) {
        uint32_t cp = ((c & 0x0F) << 12) | ((s[*i + 1] & 0x3F) << 6) | (s[*i + 2] & 0x3F);
        *i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && (s[*i + 1] & 0xC0) == 0x80 && (s[*i + 2] & 0xC0) == 0x80 &&
        (s[*i + 3] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(s[*i + 1] & 0x3F) << 12) |
                      ((s[*i + 2] & 0x3F) << 6) | (s[*i + 3] & 0x3F);
        *i += 4;
        return cp;
    }
    (*i)++;
    return '?';
}

/* Look up a glyph by codepoint; returns advance (0 if absent). */
static uint8_t font_glyph(LCDFont *font, uint32_t cp, LCDBitmap **bmp_out) {
    if (!font) { if (bmp_out) *bmp_out = NULL; return 0; }
    if (cp < PD_FONT_MAX_GLYPHS) {
        if (bmp_out) *bmp_out = font->glyph_bmp[cp];
        return font->glyph_adv[cp];
    }
    /* PFT pages/glyphs are decoded in codepoint order. Large CJK fonts need
       logarithmic lookup rather than scanning tens of thousands of glyphs. */
    int lo=0,hi=font->glyph_ext_count-1;
    while(lo<=hi) {int i=lo+(hi-lo)/2;uint32_t found=font->glyph_ext[i].cp;
        if(found==cp) {if(bmp_out)*bmp_out=font->glyph_ext[i].bmp;return font->glyph_ext[i].adv;}
        if(found<cp)lo=i+1;else hi=i-1;
    }
    if (bmp_out) *bmp_out = NULL;
    return 0;
}

/* The SDK system font supplies circled button symbols. Keep a small authored
   fallback when a game's chosen font omits them; do not replace real glyphs. */
static int button_glyph_width(uint32_t cp) {
    return cp == 0x24b6 || cp == 0x24b7 ? 14 : 0;
}

static void draw_button_glyph(uint32_t cp, int x, int y) {
    uint8_t data[28] = {0}, mask[28] = {0};
    LCDBitmap bm = {0};
    bm.width = bm.height = 14; bm.rowbytes = 2;
    bm.data = data; bm.mask = mask;
    for (int py = 0; py < 14; py++) {
        for (int px = 0; px < 14; px++) {
            int dx = 2 * px - 13, dy = 2 * py - 13;
            if (dx * dx + dy * dy > 169) continue;
            bitmap_set_pixel(&bm, px, py, 1);
            bitmap_set_mask(&bm, px, py, 1);
        }
    }
    const uint8_t *letter = pd_font5x7_glyph(cp == 0x24b6 ? 'A' : 'B');
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if ((letter[col] >> row) & 1)
                bitmap_set_mask(&bm, col + 5, row + 4, 0);
    pd_draw_bitmap_at(&bm, x, y, kBitmapUnflipped, g_pd.draw_mode);
}

int pd_font_text_width(LCDFont *font, const char *text) {
    int w = 0, i = 0;
    while (text[i]) {
        uint32_t cp = utf8_next(text, &i);
        /* SDK styling markup: *bold* / _italic_ toggles aren't rendered */
        if (cp == '*' || cp == '_') continue;
        if (cp == '\\' && (text[i] == '*' || text[i] == '_'))
            cp = utf8_next(text, &i);
        uint8_t adv = font_glyph(font, cp, NULL);
        if (adv)
            w += adv + font->tracking;
        else if (button_glyph_width(cp))
            w += button_glyph_width(cp) + (font ? font->tracking : 0);
        else
            w += (font && font->glyph_width > 0 ? font->glyph_width : 6) + (font ? font->tracking : 0);
    }
    return w;
}

static void draw_text_at(const char *text, int x, int y) {
    LCDFont *font = g_pd.current_font;
    if (font && !font->has_glyphs) font = NULL;
    if (!font) {
        /* built-in 5x7 font fallback */
        int cx = x;
        int i = 0;
        while (text[i]) {
            uint32_t ch = utf8_next(text, &i);
            if (ch == '\n') { cx = x; y += 9; continue; }
            if (button_glyph_width(ch)) {
                draw_button_glyph(ch, cx, y);
                cx += button_glyph_width(ch);
                continue;
            }
            if (ch > 126) { cx += 6; continue; }
            const uint8_t *g = pd_font5x7_glyph(ch);
            if (g) {
                for (int col = 0; col < 5; col++)
                    for (int row = 0; row < 7; row++)
                        if ((g[col] >> row) & 1)
                            draw_pixel_clip(cx + col, y + row,
                                            g_pd.draw_mode == kDrawModeFillWhite ? kColorWhite : kColorBlack);
            }
            cx += 6;
        }
        return;
    }
    int i = 0, cx = x;
    while (text[i]) {
        uint32_t cp = utf8_next(text, &i);
        if (cp == '\n') {
            cx = x;
            y += font->glyph_height + 2;
            continue;
        }
        /* SDK styling markup: *bold* / _italic_ toggles aren't rendered */
        if (cp == '*' || cp == '_') continue;
        if (cp == '\\' && (text[i] == '*' || text[i] == '_'))
            cp = utf8_next(text, &i);
        LCDBitmap *gb = NULL;
        uint8_t adv = font_glyph(font, cp, &gb);
        if (adv) {
            if (gb)
                pd_draw_bitmap_at(gb, cx, y, kBitmapUnflipped, g_pd.draw_mode);
            cx += adv + font->tracking;
        } else if (button_glyph_width(cp)) {
            draw_button_glyph(cp, cx, y);
            cx += button_glyph_width(cp) + font->tracking;
        } else {
            cx += (font->glyph_width > 0 ? font->glyph_width : 6) + font->tracking;
        }
    }
}

static int lua_gfx_drawText(lua_State *L) {
    int nargs = lua_gettop(L);
    const char *text = luaL_checkstring(L, 1);
    /* SDK: drawText(text, x, y, [fontFamily, leading]). The 4-arg variant
       with numbers at 4/5 is drawText(text, [width, height,] x, y) from
       older localized text call sites. */
    int arg_x = (nargs >= 5 && lua_isnumber(L, 4) && lua_isnumber(L, 5)) ? 4 : 2;
    int x = (int)luaL_checknumber(L, arg_x);
    int y = (int)luaL_checknumber(L, arg_x + 1);
    if (getenv("PD_TEXT_TRACE") && (unsigned char)text[0] >= 128)
        fprintf(stderr,"[button-text %s x=%d y=%d args=%d offset=%d,%d target=%dx%d font=%p]",
            text,x,y,nargs,g_pd.draw_offset_x,g_pd.draw_offset_y,
            g_pd.display_bitmap->width,g_pd.display_bitmap->height,(void*)g_pd.current_font);
    draw_text_at(text, x, y);
    /* SDK returns the drawn text's width and height */
    lua_pushinteger(L, pd_font_text_width(g_pd.current_font, text));
    lua_pushinteger(L, (g_pd.current_font && g_pd.current_font->glyph_height > 0)
                           ? g_pd.current_font->glyph_height : 12);
    return 2;
}

static int lua_gfx_getTextWidth(lua_State *L) {
    const char *text = lua_isstring(L, 1) ? lua_tostring(L, 1) : luaL_checkstring(L, 2);
    lua_pushinteger(L, pd_font_text_width(g_pd.current_font, text));
    return 1;
}

/* imageWithText(text, maxWidth, [maxHeight, ...]): render word-wrapped text
   into a new transparent image (SDK API used by dialogue systems). */
static int lua_gfx_imageWithText(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    int max_w = (int)luaL_checknumber(L, 2);
    int max_h = (int)luaL_optnumber(L, 3, 10000);
    if (max_w < 1) max_w = 1;
    LCDFont *font = g_pd.current_font;
    int line_h = (font && font->glyph_height > 0) ? font->glyph_height + 2 : 14;

    /* greedy word wrap into a scratch buffer of line breaks */
    char buf[2048];
    snprintf(buf, sizeof(buf), "%s", text);
    char lines[64][256];
    int nlines = 0;
    char cur[256] = "";
    char *save = NULL;
    int truncated = 0;
    for (char *tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        char *nl;
        while ((nl = strchr(tok, '\n')) != NULL) {
            *nl = '\0';
            char attempt[256];
            if (cur[0]) snprintf(attempt, sizeof(attempt), "%s %s", cur, tok);
            else snprintf(attempt, sizeof(attempt), "%s", tok);
            if (pd_font_text_width(font, attempt) <= max_w || !cur[0])
                snprintf(cur, sizeof(cur), "%s", attempt);
            else {
                if (nlines < 64) snprintf(lines[nlines++], 256, "%s", cur);
                snprintf(cur, sizeof(cur), "%s", tok);
            }
            if (nlines < 64) snprintf(lines[nlines++], 256, "%s", cur);
            cur[0] = '\0';
            tok = nl + 1;
        }
        if (!tok[0]) continue;
        char attempt[256];
        if (cur[0]) snprintf(attempt, sizeof(attempt), "%s %s", cur, tok);
        else snprintf(attempt, sizeof(attempt), "%s", tok);
        if (pd_font_text_width(font, attempt) <= max_w || !cur[0]) {
            snprintf(cur, sizeof(cur), "%s", attempt);
        } else {
            if (nlines < 64) snprintf(lines[nlines++], 256, "%s", cur);
            snprintf(cur, sizeof(cur), "%s", tok);
        }
    }
    if (cur[0] && nlines < 64) snprintf(lines[nlines++], 256, "%s", cur);
    if (nlines == 0) snprintf(lines[nlines++], 256, "%s", "");

    int height = nlines * line_h;
    if (height > max_h) { height = (max_h / line_h) * line_h; truncated = 1; }
    if (height < line_h) height = line_h;

    LCDBitmap *bm = create_bitmap(max_w, height);
    memset(bm->mask, 0x00, (size_t)bm->rowbytes * height); /* transparent */

    LCDBitmap *saved_display = g_pd.display_bitmap;
    int sox = g_pd.draw_offset_x, soy = g_pd.draw_offset_y;
    int sclip = g_pd.clip_enabled;
    g_pd.display_bitmap = bm;
    g_pd.draw_offset_x = 0;
    g_pd.draw_offset_y = 0;
    g_pd.clip_enabled = 0;
    for (int i = 0; i < nlines && (i + 1) * line_h <= height; i++)
        draw_text_at(lines[i], 0, i * line_h);
    g_pd.display_bitmap = saved_display;
    g_pd.draw_offset_x = sox;
    g_pd.draw_offset_y = soy;
    g_pd.clip_enabled = sclip;

    push_bitmap_ud(L, bm);
    lua_pushboolean(L, truncated);
    return 2;
}

static int lua_gfx_setTextTracking(lua_State *L) {
    g_pd.text_tracking = (int)luaL_checknumber(L, 1);
    return 0;
}

static int lua_gfx_setTextLeading(lua_State *L) {
    g_pd.text_leading = (int)luaL_checknumber(L, 1);
    return 0;
}

static int lua_gfx_getTextTracking(lua_State *L) {
    lua_pushinteger(L, g_pd.text_tracking);
    return 1;
}

static int lua_gfx_getTextLeading(lua_State *L) {
    lua_pushinteger(L, g_pd.text_leading);
    return 1;
}

static int lua_gfx_getFrame(lua_State *L) {
    (void)L;
    return 0;
}

static int lua_gfx_display(lua_State *L) {
    (void)L;
    update_display_bitmap();
    return 0;
}

static int lua_gfx_setColor(lua_State *L) {
    g_pd.fg_color = (LCDSolidColor)luaL_checknumber(L, 1);
    g_has_pattern = 0;
    if (getenv("PD_TRACE")) fprintf(stderr, "[setColor %d]", (int)g_pd.fg_color);
    return 0;
}

static int lua_gfx_getColor(lua_State *L) {
    lua_pushinteger(L, g_pd.fg_color);
    return 1;
}

static int lua_gfx_copyFrameBufferBitmap(lua_State *L) {
    LCDBitmap *bm = create_bitmap(PD_SCREEN_WIDTH, PD_SCREEN_HEIGHT);
    memcpy(bm->data, g_pd.display_bitmap->data, bm->rowbytes * PD_SCREEN_HEIGHT);
    LCDBitmap **ud = lua_newuserdata(L, sizeof(LCDBitmap *));
    *ud = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_fillPolygon(lua_State *L) {
    int npts;
    int *coords = NULL;
    if (lua_istable(L, 1)) {
        npts = (int)lua_rawlen(L, 1) / 2;
        coords = malloc(npts * 2 * sizeof(int));
        for (int i = 0; i < npts * 2; i++) {
            lua_rawgeti(L, 1, i + 1);
            coords[i] = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    } else if (lua_isuserdata(L, 1)) {
        Polygon *poly = luaL_testudata(L, 1, "pd.polygon");
        if (poly) {
            npts = poly->count / 2;
            coords = malloc(npts * 2 * sizeof(int));
            for (int i = 0; i < npts * 2; i++) coords[i] = (int)poly->coords[i];
        } else { return 0; }
    } else { return 0; }
    int color = (int)luaL_optinteger(L, 2, g_pd.fg_color);
    fill_polygon_scanline(npts, coords, color);
    free(coords);
    return 0;
}

static int lua_gfx_drawPolygon(lua_State *L) {
    int npts;
    int *coords = NULL;
    if (lua_istable(L, 1)) {
        npts = (int)lua_rawlen(L, 1) / 2;
        coords = malloc(npts * 2 * sizeof(int));
        for (int i = 0; i < npts * 2; i++) {
            lua_rawgeti(L, 1, i + 1);
            coords[i] = (int)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    } else if (lua_isuserdata(L, 1)) {
        Polygon *poly = luaL_testudata(L, 1, "pd.polygon");
        if (poly) {
            npts = poly->count / 2;
            coords = malloc(npts * 2 * sizeof(int));
            for (int i = 0; i < npts * 2; i++) coords[i] = (int)poly->coords[i];
        } else { return 0; }
    } else { return 0; }
    int lw = (int)luaL_optinteger(L, 2, 1);
    int color = (int)luaL_optinteger(L, 3, g_pd.fg_color);
    draw_polygon_outline(npts, coords, lw, color);
    free(coords);
    return 0;
}

static int lua_gfx_setPattern(lua_State *L) {
    LCDBitmap **iud = luaL_testudata(L, 1, "pd.bitmap");
    if (iud && *iud) {
        LCDBitmap *bm = *iud;
        int px_off = (int)luaL_optinteger(L, 2, 0);
        int py_off = (int)luaL_optinteger(L, 3, 0);
        for (int row = 0; row < 8; row++) {
            uint8_t color_byte = 0;
            for (int col = 0; col < 8; col++) {
                int sx = (col + px_off) % (bm->width > 0 ? bm->width : 8);
                int sy = (row + py_off) % (bm->height > 0 ? bm->height : 8);
                if (bitmap_get_pixel(bm, sx, sy))
                    color_byte |= (uint8_t)(1 << (7 - col));
            }
            g_current_pattern[row] = color_byte;
            g_current_pattern[row + 8] = 0xFF;
        }
        g_has_pattern = 1;
        return 0;
    }
    if (lua_istable(L, 1)) {
        int len = (int)lua_rawlen(L, 1);
        if (len >= 8) {
            for (int i = 0; i < 8 && i < len; i++) {
                lua_rawgeti(L, 1, i + 1);
                g_current_pattern[i] = (uint8_t)lua_tointeger(L, -1);
                lua_pop(L, 1);
            }
            if (len >= 16) {
                for (int i = 0; i < 8; i++) {
                    lua_rawgeti(L, 1, i + 9);
                    g_current_pattern[i + 8] = (uint8_t)lua_tointeger(L, -1);
                    lua_pop(L, 1);
                }
            } else {
                memset(g_current_pattern + 8, 0xFF, 8);
            }
            g_has_pattern = 1;
        }
    }
    return 0;
}

static int lua_gfx_setDitherPattern(lua_State *L) {
    static const uint8_t bayer8[8][8] = {
        { 0, 32,  8, 40,  2, 34, 10, 42},
        {48, 16, 56, 24, 50, 18, 58, 26},
        {12, 44,  4, 36, 14, 46,  6, 38},
        {60, 28, 52, 20, 62, 30, 54, 22},
        { 3, 35, 11, 43,  1, 33,  9, 41},
        {51, 19, 59, 27, 49, 17, 57, 25},
        {15, 47,  7, 39, 13, 45,  5, 37},
        {63, 31, 55, 23, 61, 29, 53, 21},
    };
    float alpha = (float)luaL_optnumber(L, 1, 0.5);
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    float coverage = 1.0f - alpha;
    int color_bit = (g_pd.fg_color == kColorWhite) ? 0 : 1;
    for (int row = 0; row < 8; row++) {
        uint8_t color_byte = 0, mask_byte = 0;
        for (int col = 0; col < 8; col++) {
            if ((float)bayer8[row][col] < coverage * 64.0f) {
                mask_byte |= (uint8_t)(1 << (7 - col));
                if (color_bit) color_byte |= (uint8_t)(1 << (7 - col));
            }
        }
        g_current_pattern[row] = color_byte;
        g_current_pattern[row + 8] = mask_byte;
    }
    g_has_pattern = 1;
    if (getenv("PD_TRACE")) fprintf(stderr, "[dither %f]", alpha);
    return 0;
}

static int lua_gfx_clearPattern(lua_State *L) {
    (void)L;
    g_has_pattern = 0;
    return 0;
}

static int lua_gfx_drawScaledBitmap(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int x = (int)luaL_checknumber(L, 2);
    int y = (int)luaL_checknumber(L, 3);
    float xs = (float)luaL_checknumber(L, 4);
    float ys = (float)luaL_optnumber(L, 5, xs);
    LCDBitmap *bm = *ud;
    if (!bm) return 0;
    int saved_pattern = g_has_pattern;
    g_has_pattern = 0;
    LCDBitmapDrawMode mode = g_pd.draw_mode;
    for (int py = 0; py < bm->height * ys; py++) {
        for (int px = 0; px < bm->width * xs; px++) {
            int sx = (int)(px / xs);
            int sy = (int)(py / ys);
            if (sx < 0 || sx >= bm->width || sy < 0 || sy >= bm->height) continue;
            if (bm->mask) {
                int mbyte = sy * bm->rowbytes + (sx / 8);
                if (!((bm->mask[mbyte] >> (7 - (sx % 8))) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, sx, sy);
            if (mode == kDrawModeWhiteTransparent && pixel == 0) continue;
            if (mode == kDrawModeBlackTransparent && pixel == 1) continue;
            if (mode == kDrawModeFillWhite)
                draw_pixel_clip(x + px, y + py, kColorWhite);
            else if (mode == kDrawModeFillBlack)
                draw_pixel_clip(x + px, y + py, kColorBlack);
            else if (mode == kDrawModeInverted)
                draw_pixel_clip(x + px, y + py, pixel ? kColorWhite : kColorBlack);
            else if (mode == kDrawModeXOR) {
                int dst = bitmap_get_pixel(g_pd.display_bitmap, x + px, y + py);
                draw_pixel_clip(x + px, y + py, dst ^ pixel ? kColorBlack : kColorWhite);
            } else if (mode == kDrawModeNXOR) {
                int dst = bitmap_get_pixel(g_pd.display_bitmap, x + px, y + py);
                draw_pixel_clip(x + px, y + py, dst ^ pixel ? kColorWhite : kColorBlack);
            } else
                draw_pixel_clip(x + px, y + py, pixel ? kColorBlack : kColorWhite);
        }
    }
    g_has_pattern = saved_pattern;
    return 0;
}

/* image:drawRotated(x, y, angle, [xscale, yscale]) — draws the image
   CENTERED at (x, y), rotated; inverse-mapped so there are no holes. */
static int lua_gfx_drawRotatedBitmap(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    float x = (float)luaL_checknumber(L, 2);
    float y = (float)luaL_checknumber(L, 3);
    float angle = (float)luaL_checknumber(L, 4);
    float sx = (float)luaL_optnumber(L, 5, 1.0);
    float sy = (float)luaL_optnumber(L, 6, sx);
    LCDBitmap *bm = *ud;
    if (!bm || sx <= 0 || sy <= 0) return 0;
    float rad = angle * (float)M_PI / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    float w = bm->width * sx, h = bm->height * sy;
    int half_w = (int)(fabsf(w * c) + fabsf(h * s)) / 2 + 2;
    int half_h = (int)(fabsf(w * s) + fabsf(h * c)) / 2 + 2;
    float scx = bm->width / 2.0f, scy = bm->height / 2.0f;
    int saved_pattern = g_has_pattern;
    g_has_pattern = 0;
    for (int py = -half_h; py <= half_h; py++) {
        for (int px = -half_w; px <= half_w; px++) {
            float ux = ((float)px * c + (float)py * s) / sx + scx;
            float uy = (-(float)px * s + (float)py * c) / sy + scy;
            int sxi = (int)ux, syi = (int)uy;
            if (ux < 0 || uy < 0 || sxi >= bm->width || syi >= bm->height) continue;
            if (bm->mask) {
                int mbyte = syi * bm->rowbytes + (sxi / 8);
                if (!((bm->mask[mbyte] >> (7 - (sxi % 8))) & 1)) continue;
            }
            int pixel = bitmap_get_pixel(bm, sxi, syi);
            draw_pixel_clip((int)(x + px), (int)(y + py),
                            pixel ? kColorBlack : kColorWhite);
        }
    }
    g_has_pattern = saved_pattern;
    return 0;
}

static int lua_gfx_tileBitmap(lua_State *L) {
    LCDBitmap **ud = luaL_checkudata(L, 1, "pd.bitmap");
    int x = (int)luaL_checknumber(L, 2);
    int y = (int)luaL_checknumber(L, 3);
    int w = (int)luaL_checknumber(L, 4);
    int h = (int)luaL_checknumber(L, 5);
    LCDBitmap *bm = *ud;
    if (!bm || bm->width <= 0 || bm->height <= 0) return 0;
    /* Only paint tiles that can reach the visible region; games tile huge
       scrolling areas and the off-screen portion must not cost time. */
    int tgt_w = g_pd.display_bitmap ? g_pd.display_bitmap->width : PD_SCREEN_WIDTH;
    int tgt_h = g_pd.display_bitmap ? g_pd.display_bitmap->height : PD_SCREEN_HEIGHT;
    int lo_x = -g_pd.draw_offset_x, hi_x = tgt_w - g_pd.draw_offset_x;
    int lo_y = -g_pd.draw_offset_y, hi_y = tgt_h - g_pd.draw_offset_y;
    int ty0 = 0, tx0 = 0;
    if (y < lo_y) ty0 = ((lo_y - y) / bm->height) * bm->height;
    if (x < lo_x) tx0 = ((lo_x - x) / bm->width) * bm->width;
    for (int ty = ty0; ty < h && y + ty < hi_y; ty += bm->height) {
        for (int tx = tx0; tx < w && x + tx < hi_x; tx += bm->width) {
            pd_draw_bitmap_at(bm, x + tx, y + ty, kBitmapUnflipped, g_pd.draw_mode);
        }
    }
    return 0;
}

static int lua_gfx_getDisplayImage(lua_State *L) {
    LCDBitmap *src = g_pd.display_bitmap;
    if (!src) { lua_pushnil(L); return 1; }
    LCDBitmap *bm = create_bitmap(src->width, src->height);
    memcpy(bm->data, src->data, (size_t)src->rowbytes * src->height);
    LCDBitmap **ud = lua_newuserdata(L, sizeof(LCDBitmap *));
    *ud = bm;
    luaL_getmetatable(L, "pd.bitmap");
    lua_setmetatable(L, -2);
    return 1;
}

static int lua_gfx_getTextSize(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    lua_pushinteger(L, pd_font_text_width(g_pd.current_font, text));
    lua_pushinteger(L, (g_pd.current_font && g_pd.current_font->glyph_height > 0) ? g_pd.current_font->glyph_height : 12);
    return 2;
}
/* getTextSizeForMaxWidth(text, maxWidth): approximate word-wrapped size */
static int lua_gfx_getTextSizeForMaxWidth(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    int max_w = (int)luaL_checknumber(L, 2);
    int leading_adj = (int)luaL_optnumber(L, 3, 0);
    int w = pd_font_text_width(g_pd.current_font, text);
    int line_h = (g_pd.current_font && g_pd.current_font->glyph_height > 0)
                     ? g_pd.current_font->glyph_height : 12;
    if (max_w < 1) max_w = 1;
    int lines = w > 0 ? (w + max_w - 1) / max_w : 1;
    lua_pushinteger(L, w < max_w ? w : max_w);
    lua_pushinteger(L, lines * (line_h + leading_adj));
    return 2;
}

static const luaL_Reg gfx_funcs[] = {
    {"clear", lua_gfx_clear},
    {"setBackgroundColor", lua_gfx_setBackgroundColor},
    {"getBackgroundColor", lua_gfx_getBackgroundColor},
    {"setDrawMode", lua_gfx_setDrawMode},
    {"setDrawOffset", lua_gfx_setDrawOffset},
    {"getDrawOffset", lua_gfx_getDrawOffset},
    {"setClipRect", lua_gfx_setClipRect},
    {"clearClipRect", lua_gfx_clearClipRect},
    {"getClipRect", lua_gfx_getClipRect},
    {"getScreenClipRect", lua_gfx_getScreenClipRect},
    {"setScreenClipRect", lua_gfx_setScreenClipRect},
    {"pushContext", lua_gfx_pushContext},
    {"popContext", lua_gfx_popContext},
    {"lockFocus", lua_gfx_lockFocus},
    {"unlockFocus", lua_gfx_unlockFocus},
    {"setPixel", lua_gfx_setPixel},
    {"drawPixel", lua_gfx_setPixel},
    {"drawLine", lua_gfx_drawLine},
    {"setLineWidth", lua_gfx_setLineWidth},
    {"getLineWidth", lua_gfx_getLineWidth},
    {"setStrokeLocation", lua_gfx_setStrokeLocation},
    {"setLineCapStyle", lua_gfx_setStrokeLocation},
    {"setStencilImage", lua_gfx_setStencilImage},
    {"setStencilPattern", lua_gfx_setStencilImage},
    {"clearStencil", lua_gfx_clearStencil},
    {"clearStencilImage", lua_gfx_clearStencil},
    {"fillRect", lua_gfx_fillRect},
    {"drawRect", lua_gfx_drawRect},
    {"fillTriangle", lua_gfx_fillTriangle},
    {"drawEllipse", lua_gfx_drawEllipse},
    {"fillEllipse", lua_gfx_fillEllipse},
    {"drawEllipseInRect", lua_gfx_drawEllipse},
    {"fillEllipseInRect", lua_gfx_fillEllipse},
    {"fillCircleInRect", lua_gfx_fillCircleInRect},
    {"drawCircleInRect", lua_gfx_drawCircleInRect},
    {"fillCircleAtPoint", lua_gfx_fillCircleAtPoint},
    {"drawCircleAtPoint", lua_gfx_drawCircleAtPoint},
    {"fillRoundRect", lua_gfx_fillRoundRect},
    {"drawRoundRect", lua_gfx_drawRoundRect},
    {"newBitmap", lua_gfx_newBitmap},
    {"loadBitmap", lua_gfx_loadBitmap},
    {"freeBitmap", lua_gfx_freeBitmap},
    {"drawBitmap", lua_gfx_drawBitmap},
    {"getSize", lua_gfx_getSize},
    {"getPixel", lua_gfx_getPixel},
    {"fadedImage", lua_gfx_bitmapFadedImage},
    {"getSizeInternal", lua_gfx_bitmapGetSizeInternal},
    {"loadFont", lua_gfx_loadFont},
    {"setFont", lua_gfx_setFont},
    {"setFontFamily", lua_gfx_setFontFamily},
    {"getFont", lua_gfx_getFont},
    {"getSystemFont", lua_gfx_getSystemFont},
    {"getFontHeight", lua_gfx_getFontHeight},
    {"drawText", lua_gfx_drawText},
    {"getTextWidth", lua_gfx_getTextWidth},
    {"getTextSize", lua_gfx_getTextSize},
    {"getTextSizeForMaxWidth", lua_gfx_getTextSizeForMaxWidth},
    {"imageWithText", lua_gfx_imageWithText},
    {"setTextTracking", lua_gfx_setTextTracking},
    {"getTextTracking", lua_gfx_getTextTracking},
    {"setFontTracking", lua_gfx_setTextTracking},
    {"getFontTracking", lua_gfx_getTextTracking},
    {"setTextLeading", lua_gfx_setTextLeading},
    {"getTextLeading", lua_gfx_getTextLeading},
    {"getFrame", lua_gfx_getFrame},
    {"display", lua_gfx_display},
    {"setColor", lua_gfx_setColor},
    {"getColor", lua_gfx_getColor},
    {"copyFrameBufferBitmap", lua_gfx_copyFrameBufferBitmap},
    {"getDisplayImage", lua_gfx_getDisplayImage},
    {"fillPolygon", lua_gfx_fillPolygon},
    {"drawPolygon", lua_gfx_drawPolygon},
    {"setPattern", lua_gfx_setPattern},
    {"setDitherPattern", lua_gfx_setDitherPattern},
    {"clearPattern", lua_gfx_clearPattern},
    {"drawScaledBitmap", lua_gfx_drawScaledBitmap},
    {"drawRotatedBitmap", lua_gfx_drawRotatedBitmap},
    {"tileBitmap", lua_gfx_tileBitmap},
    {"getImage", lua_gfx_loadBitmap},
    {"setImageDrawMode", lua_gfx_setDrawMode},
    {"getImageDrawMode", lua_gfx_getDrawMode},
    {"getDrawMode", lua_gfx_getDrawMode},
    {NULL, NULL}
};

static const luaL_Reg gfx_sprite_funcs[] = {
    {NULL, NULL}
};

static int lua_bitmap_index(lua_State *L) {
    if (lua_isstring(L, 2)) {
        const char *key = lua_tostring(L, 2);
        if (strcmp(key, "width") == 0 || strcmp(key, "height") == 0) {
            LCDBitmap **ud = luaL_testudata(L, 1, "pd.bitmap");
            if (ud && *ud) {
                lua_pushinteger(L, key[0] == 'w' ? (*ud)->width : (*ud)->height);
                return 1;
            }
        }
    }
    luaL_getmetatable(L, "pd.bitmap");
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

void pd_graphics_register(lua_State *L) {
    luaL_newmetatable(L, "pd.bitmap");
    lua_pushcfunction(L, lua_bitmap_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_gfx_freeBitmap);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, lua_gfx_bitmapFadedImage);
    lua_setfield(L, -2, "fadedImage");
    lua_pushcfunction(L, lua_gfx_drawBitmap);
    lua_setfield(L, -2, "draw");
    lua_pushcfunction(L, lua_gfx_bitmapClear);
    lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, lua_gfx_drawBitmapIgnoringOffset);
    lua_setfield(L, -2, "drawIgnoringOffset");
    lua_pushcfunction(L, lua_gfx_setMaskImage);
    lua_setfield(L, -2, "setMaskImage");
    lua_pushcfunction(L, lua_gfx_getMaskImage);
    lua_setfield(L, -2, "getMaskImage");
    lua_pushcfunction(L, lua_gfx_bitmapAddMask);
    lua_setfield(L, -2, "addMask");
    lua_pushcfunction(L, lua_gfx_bitmapClearMask);
    lua_setfield(L, -2, "clearMask");
    lua_pushcfunction(L, lua_gfx_bitmapRemoveMask);
    lua_setfield(L, -2, "removeMask");
    lua_pushcfunction(L, lua_gfx_bitmapHasMask);
    lua_setfield(L, -2, "hasMask");
    lua_pushcfunction(L, lua_gfx_bitmapCopy);
    lua_setfield(L, -2, "copy");
    lua_pushcfunction(L, lua_gfx_bitmapInvertedImage);
    lua_setfield(L, -2, "invertedImage");
    lua_pushcfunction(L, lua_gfx_bitmapScaledImage);
    lua_setfield(L, -2, "scaledImage");
    lua_pushcfunction(L, lua_gfx_bitmapRotatedImage);
    lua_setfield(L, -2, "rotatedImage");
    lua_pushcfunction(L, lua_gfx_bitmapDrawFaded);
    lua_setfield(L, -2, "drawFaded");
    lua_pushcfunction(L, lua_gfx_bitmapSetInverted);
    lua_setfield(L, -2, "setInverted");
    lua_pushcfunction(L, lua_gfx_tileBitmap);
    lua_setfield(L, -2, "drawTiled");
    lua_pushcfunction(L, lua_gfx_bitmapGetSizeInternal);
    lua_setfield(L, -2, "getSize");lua_pushcfunction(L, lua_gfx_drawScaledBitmap);
    lua_setfield(L, -2, "drawScaled");
    lua_pushcfunction(L, lua_gfx_drawRotatedBitmap);
    lua_setfield(L, -2, "drawRotated");
    lua_pushcfunction(L, lua_gfx_copyFrameBufferBitmap);
    lua_setfield(L, -2, "copyFrameBufferBitmap");
    lua_pushcfunction(L, lua_gfx_bitmapDrawSampled);
    lua_setfield(L, -2, "drawSampled");
    lua_pushcfunction(L, lua_gfx_bitmapLoad);
    lua_setfield(L, -2, "load");
    lua_pushcfunction(L, lua_gfx_bitmapSample);
    lua_setfield(L, -2, "sample");
    lua_pop(L, 1);

    luaL_newmetatable(L, "pd.font");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_getglobal(L, "playdate");
    lua_newtable(L);
    luaL_setfuncs(L, gfx_funcs, 0);

    luaL_getmetatable(L, "pd.bitmap");
    lua_pushcfunction(L, lua_gfx_newBitmap);
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, lua_gfx_loadBitmap);
    lua_setfield(L, -2, "loadBitmap");
    lua_pushcfunction(L, lua_gfx_bitmapFadedImage);
    lua_setfield(L, -2, "fadedImage");
    lua_pushcfunction(L, lua_gfx_bitmapGetSizeInternal);
    lua_setfield(L, -2, "getSize");
    {
        static const struct { const char *name; int value; } dithers[] = {
            {"kDitherTypeNone", 0}, {"kDitherTypeDiagonalLine", 1},
            {"kDitherTypeVerticalLine", 2}, {"kDitherTypeHorizontalLine", 3},
            {"kDitherTypeScreen", 4}, {"kDitherTypeBayer2x2", 5},
            {"kDitherTypeBayer4x4", 6}, {"kDitherTypeBayer8x8", 7},
            {"kDitherTypeFloydSteinberg", 8}, {"kDitherTypeBurkes", 9},
            {"kDitherTypeAtkinson", 10}, {NULL, 0}
        };
        for (int i = 0; dithers[i].name; i++) {
            lua_pushinteger(L, dithers[i].value);
            lua_setfield(L, -2, dithers[i].name);
        }
    }
    lua_setfield(L, -2, "image");

    lua_newtable(L);
    lua_pushcfunction(L, lua_gfx_newBitmapTable);
    lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "imagetable");

    luaL_newmetatable(L, META_NINESLICE);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, lua_nineslice_drawInRect);
    lua_setfield(L, -2, "drawInRect");
    lua_pushcfunction(L, lua_nineslice_getSize);
    lua_setfield(L, -2, "getSize");
    lua_pushcfunction(L, lua_nineslice_getMinSize);
    lua_setfield(L, -2, "getMinSize");
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushcfunction(L, lua_nineslice_new);
    lua_setfield(L, -2, "new");
    lua_setfield(L, -2, "nineSlice");

    tilemap_push_class(L);
    lua_setfield(L, -2, "tilemap");

    luaL_getmetatable(L, "pd.font");
    lua_pushcfunction(L, lua_gfx_loadFont);
    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, lua_gfx_getSystemFont);
    lua_setfield(L, -2, "getSystemFont");
    lua_pushcfunction(L, lua_gfx_newFontFamily);
    lua_setfield(L, -2, "newFamily");
    lua_pushcfunction(L, lua_gfx_fontGetTextWidth);
    lua_setfield(L, -2, "getTextWidth");
    lua_pushcfunction(L, lua_gfx_fontGetTextSize);
    lua_setfield(L, -2, "getTextSize");
    lua_pushcfunction(L, lua_gfx_getFontHeight);
    lua_setfield(L, -2, "getHeight");
    lua_pushcfunction(L, lua_gfx_fontGetLeading);
    lua_setfield(L, -2, "getLeading");
    lua_pushcfunction(L, lua_gfx_fontGetTracking);
    lua_setfield(L, -2, "getTracking");
    lua_pushcfunction(L, lua_gfx_fontSetLeading);
    lua_setfield(L, -2, "setLeading");
    lua_pushcfunction(L, lua_gfx_fontSetLeading);
    lua_setfield(L, -2, "setTracking");
    lua_pushcfunction(L, lua_gfx_fontDrawText);
    lua_setfield(L, -2, "drawText");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "kVariantNormal");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "kVariantBold");
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "kVariantItalic");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "kLanguageEnglish");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "kLanguageJapanese");
    lua_setfield(L, -2, "font");

    lua_newtable(L);
    static const struct {
        const char *name;
        int value;
    } color_constants[] = {
        {"kColorBlack", kColorBlack},
        {"kColorWhite", kColorWhite},
        {"kColorClear", kColorClear},
        {"kColorXOR", kColorXOR},
        {NULL, 0}
    };
    for (int i = 0; color_constants[i].name; i++) {
        lua_pushinteger(L, color_constants[i].value);
        lua_setfield(L, -2, color_constants[i].name);
    }
    lua_setfield(L, -2, "color");

    lua_pushinteger(L, kColorBlack);
    lua_setfield(L, -2, "kColorBlack");
    lua_pushinteger(L, kColorWhite);
    lua_setfield(L, -2, "kColorWhite");
    lua_pushinteger(L, kColorClear);
    lua_setfield(L, -2, "kColorClear");
    lua_pushinteger(L, kColorXOR);
    lua_setfield(L, -2, "kColorXOR");

    lua_pushinteger(L, kDrawModeCopy);
    lua_setfield(L, -2, "kDrawModeCopy");
    lua_pushinteger(L, kDrawModeWhiteTransparent);
    lua_setfield(L, -2, "kDrawModeWhiteTransparent");
    lua_pushinteger(L, kDrawModeBlackTransparent);
    lua_setfield(L, -2, "kDrawModeBlackTransparent");
    lua_pushinteger(L, kDrawModeXOR);
    lua_setfield(L, -2, "kDrawModeXOR");
    lua_pushinteger(L, kDrawModeFillWhite);
    lua_setfield(L, -2, "kDrawModeFillWhite");
    lua_pushinteger(L, kDrawModeFillBlack);
    lua_setfield(L, -2, "kDrawModeFillBlack");
    lua_pushinteger(L, kDrawModeNXOR);
    lua_setfield(L, -2, "kDrawModeNXOR");
    lua_pushinteger(L, kDrawModeInverted);
    lua_setfield(L, -2, "kDrawModeInverted");

    lua_pushinteger(L, kBitmapUnflipped);
    lua_setfield(L, -2, "kBitmapUnflipped");
    lua_pushinteger(L, kBitmapFlippedX);
    lua_setfield(L, -2, "kBitmapFlippedX");
    lua_pushinteger(L, kBitmapFlippedY);
    lua_setfield(L, -2, "kBitmapFlippedY");
    lua_pushinteger(L, kBitmapFlippedXY);
    lua_setfield(L, -2, "kBitmapFlippedXY");

    /* kImage* flip aliases (SDK exposes both spellings) */
    lua_pushinteger(L, kBitmapUnflipped);
    lua_setfield(L, -2, "kImageUnflipped");
    lua_pushinteger(L, kBitmapFlippedX);
    lua_setfield(L, -2, "kImageFlippedX");
    lua_pushinteger(L, kBitmapFlippedY);
    lua_setfield(L, -2, "kImageFlippedY");
    lua_pushinteger(L, kBitmapFlippedXY);
    lua_setfield(L, -2, "kImageFlippedXY");

    /* stroke locations */
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "kStrokeCentered");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "kStrokeOutside");
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "kStrokeInside");

    lua_newtable(L);
    luaL_setfuncs(L, gfx_sprite_funcs, 0);
    lua_setfield(L, -2, "sprite");

    lua_setfield(L, -2, "graphics");
    lua_pop(L, 1);
}
