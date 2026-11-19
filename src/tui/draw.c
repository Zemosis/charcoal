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

int rect_contains(Rect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
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

// [first, last] code point ranges
typedef struct Range {
    uint32_t lo;
    uint32_t hi;
} Range;

static const Range ZERO_WIDTH[] = {
    {0x0300, 0x036F}, {0x0483, 0x0489}, {0x0591, 0x05BD}, {0x0610, 0x061A},
    {0x064B, 0x065F}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E},
    {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x2028, 0x202E},
    {0x2060, 0x2064}, {0x20D0, 0x20FF}, {0x302A, 0x302F}, {0x3099, 0x309A},
    {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF}, {0x1F3FB, 0x1F3FF},
    {0xE0000, 0xE01EF},
};

static const Range WIDE[] = {
    {0x1100, 0x115F}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC},
    {0x23F0, 0x23F0}, {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615},
    {0x2648, 0x2653}, {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1},
    {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5}, {0x26CE, 0x26CE},
    {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F5}, {0x26FA, 0x26FD},
    {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
    {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
    {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50},
    {0x2B55, 0x2B55}, {0x2E80, 0x303E}, {0x3041, 0x33FF}, {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF}, {0xA000, 0xA4CF}, {0xA960, 0xA97F}, {0xAC00, 0xD7A3},
    {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F}, {0xFF00, 0xFF60},
    {0xFFE0, 0xFFE6}, {0x16FE0, 0x16FE4}, {0x17000, 0x18CFF},
    {0x1B000, 0x1B2FF}, {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF},
    {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F251},
    {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C},
    {0x1F37E, 0x1F393}, {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3},
    {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E},
    {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D},
    {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A},
    {0x1F595, 0x1F596}, {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F},
    {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2},
    {0x1F6D5, 0x1F6D7}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC},
    {0x1F7E0, 0x1F7EB}, {0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945},
    {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FAFF}, {0x20000, 0x2FFFD},
    {0x30000, 0x3FFFD},
};

// Prototype
static int in_ranges(uint32_t cp, const Range *r, int n);
static int put_cp(Screen *s, int x, int y, int room, uint32_t cp, uint32_t fg,
                  uint32_t bg, uint8_t attr);

static int in_ranges(uint32_t cp, const Range *r, int n) {
    // Binary search; the tables are sorted
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cp < r[mid].lo) {
            hi = mid - 1;
        } else if (cp > r[mid].hi) {
            lo = mid + 1;
        } else {
            return 1;
        }
    }
    return 0;
}

int cp_width(uint32_t cp) {
    if (cp < 0x300) {
        return 1;
    }
    if (in_ranges(cp, ZERO_WIDTH, (int)(sizeof(ZERO_WIDTH) / sizeof(Range)))) {
        return 0;
    }
    return in_ranges(cp, WIDE, (int)(sizeof(WIDE) / sizeof(Range))) ? 2 : 1;
}

int text_width(const char *text) {
    int n = 0;
    uint32_t cp;
    int used;
    while ((used = utf8_decode(text, &cp)) > 0) {
        text += used;
        n += cp_width(cp);
    }
    return n;
}

// One code point in at most room cells; a wide one that does not fit
// becomes a space. Returns the cells used
static int put_cp(Screen *s, int x, int y, int room, uint32_t cp, uint32_t fg,
                  uint32_t bg, uint8_t attr) {
    // Control characters (from file names) are shown as '?'
    if (cp < 0x20 || cp == 0x7F) {
        cp = '?';
    }
    int w = cp_width(cp);
    if (w == 0 || room < 1) {
        return 0;
    }
    if (w == 2 && room < 2) {
        screen_put(s, x, y, ' ', fg, bg, attr);
        return 1;
    }
    screen_put(s, x, y, cp, fg, bg, attr);
    if (w == 2) {
        screen_put(s, x + 1, y, CELL_WIDE_TAIL, fg, bg, attr);
    }
    return w;
}

int draw_text(Screen *s, int x, int y, int max_w, const char *text,
              uint32_t fg, uint32_t bg, uint8_t attr) {
    int n = 0;
    uint32_t cp;
    int used;
    while (n < max_w && (used = utf8_decode(text, &cp)) > 0) {
        n += put_cp(s, x + n, y, max_w - n, cp, fg, bg, attr);
        text += used;
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
        n += put_cp(s, x + n, y, 2, cp, hot ? t->hi_fg : t->main_fg, bg,
                    hot ? ATTR_BOLD : 0);
        label += used;
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
