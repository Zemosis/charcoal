#include "tui.h"

#include <stdlib.h>
#include <string.h>

// Synchronized output: the terminal shows the whole frame at once
#define SYNC_BEGIN "\033[?2026h"
#define SYNC_END   "\033[?2026l"

// Prototype
static int cell_eq(const Cell *a, const Cell *b);
static uint32_t to_depth(uint32_t color, ColorDepth depth);
static void put_color(Buf *out, int is_bg, uint32_t color);
static void put_style(Buf *out, const Cell *c, ColorDepth depth);
static void move_to(Buf *out, int x, int y);

static int cell_eq(const Cell *a, const Cell *b) {
    return a->ch == b->ch && a->fg == b->fg && a->bg == b->bg &&
           a->attr == b->attr;
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

static void put_color(Buf *out, int is_bg, uint32_t color) {
    if (color == CELL_COLOR_DEFAULT) {
        buf_puts(out, is_bg ? ";49" : ";39");
    } else if (color & CELL_COLOR_INDEX) {
        unsigned idx = color & 0xFF;
        buf_putc(out, ';');
        if (idx < 8) {
            buf_putu(out, (is_bg ? 40 : 30) + idx);
        } else if (idx < 16) {
            buf_putu(out, (is_bg ? 100 : 90) + idx - 8);
        } else {
            buf_puts(out, is_bg ? "48;5;" : "38;5;");
            buf_putu(out, idx);
        }
    } else {
        buf_puts(out, is_bg ? ";48;2;" : ";38;2;");
        buf_putu(out, (color >> 16) & 0xFF);
        buf_putc(out, ';');
        buf_putu(out, (color >> 8) & 0xFF);
        buf_putc(out, ';');
        buf_putu(out, color & 0xFF);
    }
}

// One SGR sequence that sets everything about a cell's look
static void put_style(Buf *out, const Cell *c, ColorDepth depth) {
    buf_puts(out, "\033[0");
    if (c->attr & ATTR_BOLD) {
        buf_puts(out, ";1");
    }
    if (c->attr & ATTR_DIM) {
        buf_puts(out, ";2");
    }
    if (c->attr & ATTR_REVERSE) {
        buf_puts(out, ";7");
    }
    if (depth != COLOR_MONO) {
        put_color(out, 0, to_depth(c->fg, depth));
        put_color(out, 1, to_depth(c->bg, depth));
    }
    buf_putc(out, 'm');
}

static void move_to(Buf *out, int x, int y) {
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

    int cx = -1, cy = -1;
    int styled = 0;
    Cell style = {0};
    for (int y = 0; y < s->rows; y++) {
        for (int x = 0; x < s->cols; x++) {
            int i = y * s->cols + x;
            Cell *b = &s->back[i];
            if (!s->full && cell_eq(&s->front[i], b)) {
                continue;
            }
            if (cx != x || cy != y) {
                move_to(out, x, y);
            }
            // Same look as the cell before: no escape code needed
            if (!styled || style.fg != b->fg || style.bg != b->bg ||
                style.attr != b->attr) {
                put_style(out, b, s->depth);
                style = *b;
                styled = 1;
            }
            buf_utf8(out, b->ch < 0x20 ? ' ' : b->ch);
            s->front[i] = *b;
            cx = x + 1;
            cy = y;
        }
    }
    buf_puts(out, SYNC_END);
    s->full = 0;
}
