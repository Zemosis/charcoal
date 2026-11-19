#include "render.h"

#include <stdio.h>

// Prototype
static void sgr_color(Buf *out, int is_bg, uint32_t color);
static uint32_t html_rgb(uint32_t color, uint32_t fallback);
static void css_color(Buf *out, const char *prop, uint32_t rgb);

// Append an SGR sequence that sets the fg or bg color
static void sgr_color(Buf *out, int is_bg, uint32_t color) {
    if (color == CELL_COLOR_DEFAULT) {
        buf_puts(out, is_bg ? "\033[49m" : "\033[39m");
        return;
    }
    if (color & CELL_COLOR_INDEX) {
        // 16 colors use the classic codes, which every terminal knows
        unsigned idx = color & 0xFF;
        buf_puts(out, "\033[");
        if (idx < 8) {
            buf_putu(out, (is_bg ? 40 : 30) + idx);
        } else if (idx < 16) {
            buf_putu(out, (is_bg ? 100 : 90) + idx - 8);
        } else {
            buf_puts(out, is_bg ? "48;5;" : "38;5;");
            buf_putu(out, idx);
        }
        buf_putc(out, 'm');
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

// A cell color as 0xRRGGBB for HTML; default colors become fallback
static uint32_t html_rgb(uint32_t color, uint32_t fallback) {
    if (color == CELL_COLOR_DEFAULT) {
        return fallback;
    }
    if (color & CELL_COLOR_INDEX) {
        return color_index_rgb((int)(color & 0xFF));
    }
    return color;
}

static void css_color(Buf *out, const char *prop, uint32_t rgb) {
    char css[24];
    snprintf(css, sizeof(css), "%s:#%06x", prop, (unsigned)(rgb & 0xFFFFFF));
    buf_puts(out, css);
}

void export_html(const Grid *g, Buf *out, uint32_t fg, uint32_t bg) {
    // A theme may use the terminal's colors; a page needs real ones
    fg = html_rgb(fg, 0xd4be98);
    bg = html_rgb(bg, 0x282828);

    buf_puts(out, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                  "<title>charcoal</title>\n<style>\nbody { margin: 0; ");
    css_color(out, "background", bg);
    buf_puts(out, "; }\npre { margin: 0; padding: 1em; line-height: 1; "
                  "font-family: ui-monospace, monospace; ");
    css_color(out, "color", fg);
    buf_puts(out, "; }\n</style>\n</head>\n<body>\n<pre>");

    for (int y = 0; y < g->rows; y++) {
        // One span per run of cells that look the same
        uint32_t run_fg = 0, run_bg = 0;
        int open = 0;
        for (int x = 0; x < g->cols; x++) {
            const Cell *c = &g->cells[y * g->cols + x];
            uint32_t cf = html_rgb(c->fg, fg), cb = html_rgb(c->bg, bg);
            if (!open || cf != run_fg || cb != run_bg) {
                if (open) {
                    buf_puts(out, "</span>");
                }
                buf_puts(out, "<span style=\"");
                css_color(out, "color", cf);
                if (cb != bg) {
                    buf_putc(out, ';');
                    css_color(out, "background", cb);
                }
                buf_puts(out, "\">");
                run_fg = cf;
                run_bg = cb;
                open = 1;
            }
            if (c->ch == '<') {
                buf_puts(out, "&lt;");
            } else if (c->ch == '>') {
                buf_puts(out, "&gt;");
            } else if (c->ch == '&') {
                buf_puts(out, "&amp;");
            } else {
                buf_utf8(out, c->ch);
            }
        }
        if (open) {
            buf_puts(out, "</span>");
        }
        buf_putc(out, '\n');
    }
    buf_puts(out, "</pre>\n</body>\n</html>\n");
}
