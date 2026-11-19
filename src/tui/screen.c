#include "tui.h"

#include <stdlib.h>
#include <string.h>

// Synchronized output: the terminal shows the whole frame at once
#define SYNC_BEGIN "\033[?2026h"
#define SYNC_END   "\033[?2026l"

// Prototype
static int color_close(uint32_t x, uint32_t y, int tol);
static int cell_same(const Cell *a, const Cell *b, int tol);
static uint32_t to_depth(uint32_t color, ColorDepth depth);
static void put_color(Buf *out, int is_bg, uint32_t color, int *first);
static void put_style(Buf *out, const Cell *c, Cell *cur, int have,
                      ColorDepth depth);
static void move_to(Buf *out, int x, int y, int cx, int cy);

// Equal, or both RGB with every channel within tol
static int color_close(uint32_t x, uint32_t y, int tol) {
    if (x == y) {
        return 1;
    }
    if (tol == 0 || ((x | y) & 0xFF000000u)) {
        return 0;
    }
    for (int shift = 0; shift <= 16; shift += 8) {
        int d = (int)((x >> shift) & 0xFF) - (int)((y >> shift) & 0xFF);
        if (d > tol || d < -tol) {
            return 0;
        }
    }
    return 1;
}

// Close enough that redrawing would not show: same glyph and attributes
static int cell_same(const Cell *a, const Cell *b, int tol) {
    return a->ch == b->ch && a->attr == b->attr &&
           color_close(a->fg, b->fg, tol) && color_close(a->bg, b->bg, tol);
}

// Bring an RGB color down to what the terminal can show
static uint32_t to_depth(uint32_t color, ColorDepth depth) {
    if (color == CELL_COLOR_DEFAULT || (color & CELL_COLOR_INDEX)) {
        return color;
    }
    if (depth == COLOR_256 || depth == COLOR_16) {
        return color_nearest(color, depth);
    }
    return color;
}

// One color parameter of an SGR sequence, ';'-separated after the first
static void put_color(Buf *out, int is_bg, uint32_t color, int *first) {
    if (!*first) {
        buf_putc(out, ';');
    }
    *first = 0;
    if (color == CELL_COLOR_DEFAULT) {
        buf_puts(out, is_bg ? "49" : "39");
    } else if (color & CELL_COLOR_INDEX) {
        unsigned idx = color & 0xFF;
        if (idx < 8) {
            buf_putu(out, (is_bg ? 40 : 30) + idx);
        } else if (idx < 16) {
            buf_putu(out, (is_bg ? 100 : 90) + idx - 8);
        } else {
            buf_puts(out, is_bg ? "48;5;" : "38;5;");
            buf_putu(out, idx);
        }
    } else {
        buf_puts(out, is_bg ? "48;2;" : "38;2;");
        buf_putu(out, (color >> 16) & 0xFF);
        buf_putc(out, ';');
        buf_putu(out, (color >> 8) & 0xFF);
        buf_putc(out, ';');
        buf_putu(out, color & 0xFF);
    }
}

// Change the terminal's style from cur to c, sending only what differs;
// attributes can only be turned off by a reset, so they force one
static void put_style(Buf *out, const Cell *c, Cell *cur, int have,
                      ColorDepth depth) {
    uint32_t fg = depth == COLOR_MONO ? CELL_COLOR_DEFAULT : to_depth(c->fg, depth);
    uint32_t bg = depth == COLOR_MONO ? CELL_COLOR_DEFAULT : to_depth(c->bg, depth);
    int reset = !have || c->attr != cur->attr;
    int set_fg = reset ? fg != CELL_COLOR_DEFAULT : fg != cur->fg;
    int set_bg = reset ? bg != CELL_COLOR_DEFAULT : bg != cur->bg;
    if (!reset && !set_fg && !set_bg) {
        return;
    }

    int first = 1;
    buf_puts(out, "\033[");
    if (reset) {
        buf_putc(out, '0');
        first = 0;
        if (c->attr & ATTR_BOLD) {
            buf_puts(out, ";1");
        }
        if (c->attr & ATTR_DIM) {
            buf_puts(out, ";2");
        }
        if (c->attr & ATTR_REVERSE) {
            buf_puts(out, ";7");
        }
    }
    if (set_fg) {
        put_color(out, 0, fg, &first);
    }
    if (set_bg) {
        put_color(out, 1, bg, &first);
    }
    buf_putc(out, 'm');
    cur->fg = fg;
    cur->bg = bg;
    cur->attr = c->attr;
}

// Move the cursor with the shortest sequence from cx, cy
static void move_to(Buf *out, int x, int y, int cx, int cy) {
    if (y == cy && x > cx) {
        buf_puts(out, "\033[");
        if (x - cx > 1) {
            buf_putu(out, (unsigned)(x - cx));
        }
        buf_putc(out, 'C');
        return;
    }
    if (y == cy + 1 && x == 0 && cy >= 0) {
        buf_puts(out, "\r\n");
        return;
    }
    buf_puts(out, "\033[");
    buf_putu(out, (unsigned)y + 1);
    buf_putc(out, ';');
    buf_putu(out, (unsigned)x + 1);
    buf_putc(out, 'H');
}

Screen *screen_new(int cols, int rows, ColorDepth depth) {
    Screen *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->depth = depth;
    if (screen_resize(s, cols, rows) != 0) {
        free(s);
        return NULL;
    }
    return s;
}

int screen_resize(Screen *s, int cols, int rows) {
    if (cols < 1) {
        cols = 1;
    }
    if (rows < 1) {
        rows = 1;
    }
    size_t n = (size_t)cols * (size_t)rows;
    Cell *front = calloc(n, sizeof(Cell));
    Cell *back = calloc(n, sizeof(Cell));
    if (!front || !back) {
        free(front);
        free(back);
        return -1;
    }
    free(s->front);
    free(s->back);
    s->front = front;
    s->back = back;
    s->cols = cols;
    s->rows = rows;
    s->full = 1;
    screen_clear(s, CELL_COLOR_DEFAULT, CELL_COLOR_DEFAULT);
    return 0;
}

void screen_free(Screen *s) {
    if (s) {
        free(s->front);
        free(s->back);
        free(s);
    }
}

void screen_clear(Screen *s, uint32_t fg, uint32_t bg) {
    for (int i = 0; i < s->cols * s->rows; i++) {
        s->back[i] = (Cell){' ', fg, bg, 0};
    }
}

Cell *screen_cell(Screen *s, int x, int y) {
    if (x < 0 || y < 0 || x >= s->cols || y >= s->rows) {
        return NULL;
    }
    return &s->back[y * s->cols + x];
}

void screen_put(Screen *s, int x, int y, uint32_t ch, uint32_t fg,
                uint32_t bg, uint8_t attr) {
    Cell *c = screen_cell(s, x, y);
    if (c) {
        *c = (Cell){ch, fg, bg, attr};
    }
}

void screen_flush(Screen *s, Buf *out) {
    buf_puts(out, SYNC_BEGIN);
    if (s->full) {
        buf_puts(out, "\033[0m\033[2J");
    }

    // Writing over the right half of a wide character on screen erases the
    // whole character, so its left half must be redrawn too
    if (!s->full) {
        for (int i = 1; i < s->cols * s->rows; i++) {
            if (s->front[i].ch == CELL_WIDE_TAIL && i % s->cols > 0 &&
                !cell_same(&s->front[i], &s->back[i], 0)) {
                s->front[i - 1].ch = UINT32_MAX;
            }
        }
    }

    int cx = -1, cy = -1;
    int have = 0;
    Cell style = {0};
    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            int i = y * s->cols + x;
            Cell *b = &s->back[i];

            // A wide character and its tail cell go out as one: the tail is
            // written with its head, and a head without a tail (or a tail
            // without a head) became a space when something drew over it
            int wide = b->ch != CELL_WIDE_TAIL && cp_width(b->ch) == 2;
            int tail_ok = wide && x + 1 < s->cols &&
                          s->back[i + 1].ch == CELL_WIDE_TAIL;
            if (b->ch == CELL_WIDE_TAIL && x > 0 && s->back[i - 1].ch &&
                cp_width(s->back[i - 1].ch) == 2) {
                continue;
            }
            int dirty = s->full || !cell_same(&s->front[i], b, s->tolerance) ||
                        (tail_ok && !cell_same(&s->front[i + 1], &s->back[i + 1],
                                               0));
            if (!dirty) {
                continue;
            }
            if (cx != x || cy != y) {
                move_to(out, x, y, cx, cy);
            }
            put_style(out, b, &style, have, s->depth);
            have = 1;
            uint32_t ch = b->ch < 0x20 || (wide && !tail_ok) ? ' ' : b->ch;
            buf_utf8(out, ch);
            s->front[i] = *b;
            cx = x + 1;
            if (tail_ok) {
                s->front[i + 1] = s->back[i + 1];
                cx = x + 2;
            }
            cy = y;
        }
    }
    buf_puts(out, SYNC_END);
    s->full = 0;
}
