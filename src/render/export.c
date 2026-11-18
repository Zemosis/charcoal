#include "render.h"

// Prototype
static void sgr_color(Buf *out, int is_bg, uint32_t color);

// Append an SGR sequence that sets the fg or bg color
static void sgr_color(Buf *out, int is_bg, uint32_t color) {
    if (color == CELL_COLOR_DEFAULT) {
        buf_puts(out, is_bg ? "\033[49m" : "\033[39m");
        return;
    }
    buf_puts(out, is_bg ? "\033[48;2;" : "\033[38;2;");
    buf_putu(out, (color >> 16) & 0xFF);
    buf_putc(out, ';');
    buf_putu(out, (color >> 8) & 0xFF);
    buf_putc(out, ';');
    buf_putu(out, color & 0xFF);
    buf_putc(out, 'm');
}

void export_ansi(const Grid *g, Buf *out) {
    for (int y = 0; y < g->rows; y++) {
        // Each line starts from the terminal defaults
        uint32_t fg = CELL_COLOR_DEFAULT;
        uint32_t bg = CELL_COLOR_DEFAULT;
        int colored = 0;

        for (int x = 0; x < g->cols; x++) {
            const Cell *c = &g->cells[y * g->cols + x];
            if (c->fg != fg) {
                sgr_color(out, 0, c->fg);
                fg = c->fg;
                colored = 1;
            }
            if (c->bg != bg) {
                sgr_color(out, 1, c->bg);
                bg = c->bg;
                colored = 1;
            }
            buf_utf8(out, c->ch);
        }
        if (colored) {
            buf_puts(out, "\033[0m");
        }
        buf_putc(out, '\n');
    }
}

void export_txt(const Grid *g, Buf *out) {
    for (int y = 0; y < g->rows; y++) {
        for (int x = 0; x < g->cols; x++) {
            buf_utf8(out, g->cells[y * g->cols + x].ch);
        }
        buf_putc(out, '\n');
    }
}
