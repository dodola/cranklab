#include "cranklab_menu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pd_runtime.h"

#define W 400
#define H 240
#define PANEL_X 200
#define ROW_H 36
#define ROW_TOP 44

static LCDFont *zh_font;

void cranklab_menu_load_font(const char *path) {
    if (zh_font || !path) return;
    zh_font = pd_load_pft_detached(path);
}
void cranklab_menu_free_font(void) {
    if (zh_font) pd_font_release(zh_font);
    zh_font = NULL;
}

/* ---- JSON ------------------------------------------------------------- */

typedef struct { const char *p; int failed; } Cursor;

static void skip_space(Cursor *c) { while (*c->p && isspace((unsigned char)*c->p)) c->p++; }
static int eat(Cursor *c, char ch) { skip_space(c); if (*c->p == ch) { c->p++; return 1; } return 0; }

static int put_utf8(char *out, size_t cap, size_t *n, uint32_t cp) {
    char b[4]; int len;
    if (cp < 0x80) { b[0] = (char)cp; len = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); len = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 63)); b[2] = (char)(0x80 | (cp & 63)); len = 3; }
    else { b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | ((cp >> 12) & 63)); b[2] = (char)(0x80 | ((cp >> 6) & 63)); b[3] = (char)(0x80 | (cp & 63)); len = 4; }
    if (*n + len >= cap) return 0;
    memcpy(out + *n, b, len); *n += len; return 1;
}

static unsigned hex4(const char *p) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        if (!isxdigit((unsigned char)p[i])) return 0xFFFFFFFFu;
        v = v * 16 + (unsigned)(isdigit((unsigned char)p[i]) ? p[i] - '0' : (tolower((unsigned char)p[i]) - 'a' + 10));
    }
    return v;
}

/* Reads a JSON string into out (truncating long text); returns 1 on success. */
static int read_string(Cursor *c, char *out, size_t cap) {
    size_t n = 0;
    skip_space(c);
    if (*c->p != '"') return 0;
    c->p++;
    while (*c->p && *c->p != '"') {
        uint32_t cp;
        if (*c->p == '\\') {
            c->p++;
            switch (*c->p) {
            case 'n': cp = ' '; break;
            case 't': cp = ' '; break;
            case 'u': {
                unsigned v = hex4(c->p + 1);
                if (v == 0xFFFFFFFFu) return 0;
                c->p += 4; cp = v;
                if (v >= 0xD800 && v < 0xDC00 && c->p[1] == '\\' && c->p[2] == 'u') {
                    unsigned lo = hex4(c->p + 3);
                    if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00); c->p += 6; }
                }
                break;
            }
            case 0: return 0;
            default: cp = (unsigned char)*c->p; break;
            }
            c->p++;
            if (out) put_utf8(out, cap, &n, cp);
        } else {
            if (out && n + 1 < cap) out[n++] = *c->p;
            c->p++;
        }
    }
    if (*c->p != '"') return 0;
    c->p++;
    if (out && cap) out[n < cap ? n : cap - 1] = 0;
    return 1;
}

static int skip_value(Cursor *c, int depth);

static int skip_container(Cursor *c, char close, int depth) {
    if (depth > 16) return 0;
    if (eat(c, close)) return 1;
    for (;;) {
        skip_space(c);
        if (close == '}') { if (!read_string(c, NULL, 0) || !eat(c, ':')) return 0; }
        if (!skip_value(c, depth + 1)) return 0;
        if (eat(c, ',')) continue;
        return eat(c, close);
    }
}

static int skip_value(Cursor *c, int depth) {
    skip_space(c);
    if (*c->p == '"') return read_string(c, NULL, 0);
    if (*c->p == '{') { c->p++; return skip_container(c, '}', depth); }
    if (*c->p == '[') { c->p++; return skip_container(c, ']', depth); }
    const char *start = c->p;
    while (*c->p && !strchr(",}] \t\r\n", *c->p)) c->p++;
    return c->p != start;
}

static int read_number(Cursor *c, int *out) {
    skip_space(c);
    if (!strncmp(c->p, "true", 4)) { *out = 1; c->p += 4; return 1; }
    if (!strncmp(c->p, "false", 5)) { *out = 0; c->p += 5; return 1; }
    char *end;
    double v = strtod(c->p, &end);
    if (end == c->p) return 0;
    *out = (int)v; c->p = end; return 1;
}

static int parse_options(Cursor *c, CrankMenuItem *item) {
    if (!eat(c, '[')) return skip_value(c, 0); /* null or anything else */
    if (eat(c, ']')) return 1;
    for (;;) {
        char text[48];
        skip_space(c);
        if (*c->p == '"') {
            if (!read_string(c, text, sizeof(text))) return 0;
            if (item->option_count < MENU_MAX_OPTIONS) snprintf(item->options[item->option_count++], sizeof(item->options[0]), "%s", text);
        } else if (!skip_value(c, 1)) return 0;
        if (eat(c, ',')) continue;
        return eat(c, ']');
    }
}

static int parse_item(Cursor *c, CrankMenuItem *item) {
    if (!eat(c, '{')) return 0;
    if (eat(c, '}')) return 1;
    for (;;) {
        char key[32];
        if (!read_string(c, key, sizeof(key)) || !eat(c, ':')) return 0;
        if (!strcmp(key, "id")) { if (!read_number(c, &item->id)) return 0; }
        else if (!strcmp(key, "kind")) { if (!read_number(c, &item->kind)) return 0; }
        else if (!strcmp(key, "value")) { if (!read_number(c, &item->value)) return 0; }
        else if (!strcmp(key, "title")) { skip_space(c); if (*c->p == '"') { if (!read_string(c, item->title, sizeof(item->title))) return 0; } else if (!skip_value(c, 1)) return 0; }
        else if (!strcmp(key, "options")) { if (!parse_options(c, item)) return 0; }
        else if (!skip_value(c, 1)) return 0;
        if (eat(c, ',')) continue;
        return eat(c, '}');
    }
}

int cranklab_menu_parse(const char *json, CrankMenuModel *model) {
    Cursor c = { json, 0 };
    memset(model, 0, sizeof(*model));
    if (!json || !eat(&c, '{')) return 0;
    if (eat(&c, '}')) return 1;
    for (;;) {
        char key[32];
        if (!read_string(&c, key, sizeof(key)) || !eat(&c, ':')) return 0;
        if (!strcmp(key, "items")) {
            if (!eat(&c, '[')) return 0;
            if (!eat(&c, ']')) {
                for (;;) {
                    CrankMenuItem item;
                    memset(&item, 0, sizeof(item));
                    if (!parse_item(&c, &item)) return 0;
                    if (model->count < MENU_MAX_ITEMS) model->items[model->count++] = item;
                    if (eat(&c, ',')) continue;
                    if (!eat(&c, ']')) return 0;
                    break;
                }
            }
        } else if (!strcmp(key, "imageOffset")) { if (!read_number(&c, &model->image_offset)) return 0; }
        else if (!strcmp(key, "hasImage")) { if (!read_number(&c, &model->has_image)) return 0; }
        else if (!skip_value(&c, 1)) return 0;
        if (eat(&c, ',')) continue;
        return eat(&c, '}');
    }
}

int cranklab_menu_rows(const CrankMenuModel *model) { return model->count + 1; }

/* ---- drawing ---------------------------------------------------------- */

typedef struct { uint32_t *pixels; uint32_t black, white; } Canvas;

static void put(Canvas *cv, int x, int y, int black) {
    if (x >= 0 && x < W && y >= 0 && y < H) cv->pixels[y * W + x] = black ? cv->black : cv->white;
}
static void fill(Canvas *cv, int x0, int y0, int x1, int y1, int black) {
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) put(cv, x, y, black);
}

static uint32_t next_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t cp = *p;
    int extra = 0;
    if (cp < 0x80) extra = 0;
    else if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; extra = 1; }
    else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; extra = 2; }
    else if ((cp & 0xF8) == 0xF0) { cp &= 0x07; extra = 3; }
    else { *s += 1; return '?'; }
    for (int i = 1; i <= extra; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return '?'; }
        cp = cp << 6 | (p[i] & 0x3F);
    }
    *s += extra + 1;
    return cp;
}

/* Draws one glyph (ASCII from the built-in 5x7 face at 2x, anything else from the PFT).
   Returns the advance in pixels; draw == 0 only measures. */
static int glyph(Canvas *cv, int x, int y, uint32_t cp, int ink_black, int draw) {
    if (cp >= 32 && cp < 127) {
        if (draw) {
            const uint8_t *g = pd_font5x7_glyph((int)cp);
            for (int col = 0; g && col < 5; col++) for (int row = 0; row < 7; row++)
                if ((g[col] >> row) & 1) fill(cv, x + col * 2, y + 3 + row * 2, x + col * 2 + 2, y + 3 + row * 2 + 2, ink_black);
        }
        return 12;
    }
    if (zh_font) {
        const uint8_t *data, *mask;
        int w, h, rowbytes;
        int advance = pd_font_glyph_bitmap(zh_font, cp, &data, &mask, &w, &h, &rowbytes);
        if (advance) {
            if (draw && data && mask) {
                for (int gy = 0; gy < h; gy++) for (int gx = 0; gx < w; gx++) {
                    int bit = 7 - (gx & 7), at = gy * rowbytes + (gx >> 3);
                    if (!((mask[at] >> bit) & 1)) continue;
                    /* Glyph "white" is the letterform, painted in the text colour; masked-in
                       black (outline fonts) takes the opposite colour. */
                    int letterform = (data[at] >> bit) & 1;
                    put(cv, x + gx, y + gy - 1, letterform ? ink_black : !ink_black);
                }
            }
            return advance;
        }
    }
    if (draw) { /* hollow box for glyphs no font provides */
        fill(cv, x + 1, y + 4, x + 9, y + 5, ink_black); fill(cv, x + 1, y + 15, x + 9, y + 16, ink_black);
        fill(cv, x + 1, y + 4, x + 2, y + 16, ink_black); fill(cv, x + 8, y + 4, x + 9, y + 16, ink_black);
    }
    return 12;
}

static int text_width(const char *s) {
    Canvas none = { 0 };
    int w = 0;
    while (*s) w += glyph(&none, 0, 0, next_cp(&s), 0, 0);
    return w;
}

/* Draws s, shortening with ".." so it fits max_width. */
static void text(Canvas *cv, int x, int y, const char *s, int max_width, int ink_black) {
    int total = text_width(s), used = 0;
    int clip = total > max_width;
    while (*s) {
        uint32_t cp = next_cp(&s);
        int adv = glyph(cv, x + used, y, cp, ink_black, 0);
        if (clip && used + adv + 24 > max_width) {
            glyph(cv, x + used, y, '.', ink_black, 1); glyph(cv, x + used + 12, y, '.', ink_black, 1);
            return;
        }
        glyph(cv, x + used, y, cp, ink_black, 1);
        used += adv;
    }
}

static void draw_background(Canvas *cv, const uint8_t *bgra, int offset) {
    if (!bgra) { for (int i = 0; i < W * H; i++) cv->pixels[i] = cv->white; return; }
    if (offset < 0) offset = 0;
    if (offset > 200) offset = 200;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int sx = x + offset;
        cv->pixels[y * W + x] = sx < W && bgra[(y * W + sx) * 4] <= 127 ? cv->black : cv->white;
    }
}

void cranklab_menu_draw(uint32_t *pixels, const CrankMenuModel *model, int selected,
                        const uint8_t *background, int background_offset,
                        uint32_t black, uint32_t white) {
    Canvas cv = { pixels, black, white };
    int rows = cranklab_menu_rows(model);
    draw_background(&cv, background, background_offset);
    for (int y = 0; y < H; y++) for (int x = PANEL_X; x < W; x++) pixels[y * W + x] = white;
    fill(&cv, PANEL_X, 0, PANEL_X + 2, H, 1);
    text(&cv, PANEL_X + 14, 12, "MENU", 160, 1);
    fill(&cv, PANEL_X + 12, 34, W - 12, 36, 1);
    for (int row = 0; row < rows; row++) {
        int y = ROW_TOP + row * ROW_H, on = row == selected;
        const CrankMenuItem *item = row < model->count ? &model->items[row] : NULL;
        char value[64] = "";
        int value_w = 0;
        if (on) fill(&cv, PANEL_X + 8, y, W - 8, y + ROW_H - 4, 1);
        if (item && item->kind == 1) snprintf(value, sizeof(value), "%s", item->value ? "[x]" : "[ ]");
        else if (item && item->kind == 2) {
            int idx = item->value - 1;
            snprintf(value, sizeof(value), "< %s >", idx >= 0 && idx < item->option_count ? item->options[idx] : "?");
        }
        value_w = value[0] ? text_width(value) : 0;
        text(&cv, PANEL_X + 16, y + 6, item ? (item->title[0] ? item->title : "-") : "Resume",
             (W - 16) - (PANEL_X + 16) - (value_w ? value_w + 8 : 0), !on);
        if (value[0]) text(&cv, W - 16 - value_w, y + 6, value, value_w + 2, !on);
    }
}
