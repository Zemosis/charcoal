#include "tui.h"

#include <string.h>

#define BOX_TL 0x256D   // ╭
#define BOX_TR 0x256E   // ╮
#define BOX_BL 0x2570   // ╰
#define BOX_BR 0x256F   // ╯
#define BOX_H  0x2500   // ─
#define BOX_V  0x2502   // │
#define BOX_LABEL_L 0x2510  // ┐
#define BOX_LABEL_R 0x250C  // ┌

// Superscript digits for panel hotkeys, ⁰ to ⁹
static const uint32_t SUPERSCRIPT[10] = {
    0x2070, 0x00B9, 0x00B2, 0x00B3, 0x2074,
    0x2075, 0x2076, 0x2077, 0x2078, 0x2079,
};

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
    screen_put(s, r.x, r.y, BOX_TL, bc, bg, 0);
    screen_put(s, x1, r.y, BOX_TR, bc, bg, 0);
    screen_put(s, r.x, y1, BOX_BL, bc, bg, 0);
    screen_put(s, x1, y1, BOX_BR, bc, bg, 0);

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
                      char hotkey, const Theme *t) {
    uint32_t bg = t->main_bg;
    screen_put(s, x, y, BOX_LABEL_L, t->box_border, bg, 0);
    int n = 1;
    int lit = 0;
    uint32_t cp;
    int used;
    while ((used = utf8_decode(label, &cp)) > 0) {
        int hot = !lit && hotkey && cp == (uint32_t)(unsigned char)hotkey;
        lit |= hot;
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
