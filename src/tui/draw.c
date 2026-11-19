#include "tui.h"

#include <string.h>

#define BOX_TL 0x256D   // ╭
#define BOX_TR 0x256E   // ╮
#define BOX_BL 0x2570   // ╰
#define BOX_BR 0x256F   // ╯
#define SQ_TL 0x250C    // ┌
#define SQ_TR 0x2510    // ┐
#define SQ_BL 0x2514    // └
#define SQ_BR 0x2518    // ┘
#define BOX_H  0x2500   // ─
#define BOX_V  0x2502   // │
#define BOX_LABEL_L 0x2510  // ┐
#define BOX_LABEL_R 0x250C  // ┌

static int rounded = 1;

// Braille dot bits from the bottom row up: left column, right column
static const uint8_t DOTS_LEFT[4] = {0x40, 0x04, 0x02, 0x01};
static const uint8_t DOTS_RIGHT[4] = {0x80, 0x20, 0x10, 0x08};

// Superscript digits for panel hotkeys, ⁰ to ⁹
static const uint32_t SUPERSCRIPT[10] = {
    0x2070, 0x00B9, 0x00B2, 0x00B3, 0x2074,
    0x2075, 0x2076, 0x2077, 0x2078, 0x2079,
};

void draw_set_rounded(int on) {
    rounded = on;
}

Rect rect_inner(Rect r) {
    Rect in = {r.x + 1, r.y + 1, r.w - 2, r.h - 2};
    if (in.w < 0) {
        in.w = 0;
    }
    if (in.h < 0) {
        in.h = 0;
    }
    return in;
}

void draw_fill(Screen *s, Rect r, uint32_t fg, uint32_t bg) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            screen_put(s, x, y, ' ', fg, bg, 0);
        }
    }
}

int text_width(const char *text) {
    int n = 0;
    uint32_t cp;
    int used;
    while ((used = utf8_decode(text, &cp)) > 0) {
        text += used;
        n++;
    }
    return n;
}

int draw_text(Screen *s, int x, int y, int max_w, const char *text,
              uint32_t fg, uint32_t bg, uint8_t attr) {
    int n = 0;
    uint32_t cp;
    int used;
    while (n < max_w && (used = utf8_decode(text, &cp)) > 0) {
        // Control characters (from file names) are shown as '?'
        screen_put(s, x + n, y, cp < 0x20 || cp == 0x7F ? '?' : cp, fg, bg,
                   attr);
        text += used;
        n++;
    }
    return n;
}

void draw_box(Screen *s, Rect r, const Theme *t, int hotkey,
              const char *title) {
    if (r.w < 2 || r.h < 2) {
        return;
    }
    uint32_t bc = t->box_border, bg = t->main_bg;
    int x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;

    for (int x = r.x + 1; x < x1; x++) {
        screen_put(s, x, r.y, BOX_H, bc, bg, 0);
        screen_put(s, x, y1, BOX_H, bc, bg, 0);
    }
    for (int y = r.y + 1; y < y1; y++) {
        screen_put(s, r.x, y, BOX_V, bc, bg, 0);
        screen_put(s, x1, y, BOX_V, bc, bg, 0);
    }
    screen_put(s, r.x, r.y, rounded ? BOX_TL : SQ_TL, bc, bg, 0);
    screen_put(s, x1, r.y, rounded ? BOX_TR : SQ_TR, bc, bg, 0);
    screen_put(s, r.x, y1, rounded ? BOX_BL : SQ_BL, bc, bg, 0);
    screen_put(s, x1, y1, rounded ? BOX_BR : SQ_BR, bc, bg, 0);

    // ╭─¹title──
    int x = r.x + 2;
    if (hotkey >= 0 && hotkey <= 9 && x < x1) {
        screen_put(s, x++, r.y, SUPERSCRIPT[hotkey], t->hi_fg, bg, 0);
    }
    if (title) {
        draw_text(s, x, r.y, x1 - x - 1, title, t->title, bg, ATTR_BOLD);
    }
}

int draw_border_label(Screen *s, int x, int y, const char *label,
                      const char *hot_keys, const Theme *t) {
    uint32_t bg = t->main_bg;
    screen_put(s, x, y, BOX_LABEL_L, t->box_border, bg, 0);
    int n = 1;
    uint32_t cp;
    int used;
    while ((used = utf8_decode(label, &cp)) > 0) {
        int hot = hot_keys && cp < 0x80 && cp && strchr(hot_keys, (int)cp);
        screen_put(s, x + n, y, cp, hot ? t->hi_fg : t->main_fg, bg,
                   hot ? ATTR_BOLD : 0);
        label += used;
        n++;
    }
    screen_put(s, x + n, y, BOX_LABEL_R, t->box_border, bg, 0);
    return n + 1;
}

void draw_grid(Screen *s, const Grid *g, int x, int y, const Theme *t) {
    for (int gy = 0; gy < g->rows; gy++) {
        for (int gx = 0; gx < g->cols; gx++) {
            const Cell *c = &g->cells[gy * g->cols + gx];
            uint32_t fg = c->fg == CELL_COLOR_DEFAULT ? t->main_fg : c->fg;
            uint32_t bg = c->bg == CELL_COLOR_DEFAULT ? t->main_bg : c->bg;
            screen_put(s, x + gx, y + gy, c->ch, fg, bg, c->attr);
        }
    }
}

void draw_braille_graph(Screen *s, Rect r, const double *values,
                        const Theme *t) {
    int dots_h = r.h * 4;
    for (int cx = 0; cx < r.w; cx++) {
        // Bar heights in dots for the two bars of this cell
        int hl = (int)(values[cx * 2] * dots_h + 0.5);
        int hr = (int)(values[cx * 2 + 1] * dots_h + 0.5);
        for (int row = 0; row < r.h; row++) {
            int base = (r.h - 1 - row) * 4;     // dots below this cell
            uint8_t bits = 0;
            for (int k = 0; k < 4; k++) {
                if (hl > base + k) {
                    bits |= DOTS_LEFT[k];
                }
                if (hr > base + k) {
                    bits |= DOTS_RIGHT[k];
                }
            }
            if (!bits) {
                continue;
            }
            // Low, mid and high thirds take the graph colors, like btop
            double level = (double)(r.h - row) / r.h;
            uint32_t fg = level <= 1.0 / 3 ? t->graph_low
                        : level <= 2.0 / 3 ? t->graph_mid
                                           : t->graph_high;
            screen_put(s, r.x + cx, r.y + row, 0x2800 + bits, fg, t->main_bg, 0);
        }
    }
}
