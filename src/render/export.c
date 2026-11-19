#include "render.h"

#include <stdio.h>

// Prototype
static void sgr_color(Buf *out, int is_bg, uint32_t color);
static uint32_t html_rgb(uint32_t color, uint32_t fallback);
static void css_color(Buf *out, const char *prop, uint32_t rgb);
static int block_mask(uint32_t ch);

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

// Which quarters of the cell a block character fills (bits: top left 1,
// top right 2, bottom left 4, bottom right 8), -1 for other characters
static int block_mask(uint32_t ch) {
    switch (ch) {
    case 0x2598: return 1;      // ▘
    case 0x259D: return 2;      // ▝
    case 0x2580: return 3;      // ▀
    case 0x2596: return 4;      // ▖
    case 0x258C: return 5;      // ▌
    case 0x259E: return 6;      // ▞
    case 0x259B: return 7;      // ▛
    case 0x2597: return 8;      // ▗
    case 0x259A: return 9;      // ▚
    case 0x2590: return 10;     // ▐
    case 0x259C: return 11;     // ▜
    case 0x2584: return 12;     // ▄
    case 0x2599: return 13;     // ▙
    case 0x259F: return 14;     // ▟
    case 0x2588: return 15;     // █
    default: return -1;
    }
}

void export_html(const Grid *g, Buf *out, uint32_t fg, uint32_t bg,
                 double cell_aspect) {
    // A theme may use the terminal's colors; a page needs real ones
    fg = html_rgb(fg, 0xd4be98);
    bg = html_rgb(bg, 0x282828);
    if (cell_aspect < 1.0 || cell_aspect > 4.0) {
        cell_aspect = 2.0;
    }

    // Cells are 1ch wide and cell_aspect ch tall, as in the terminal, and
    // the art is sized to fit the window (a monospace ch is about 0.6em)
    char line[512];
    buf_puts(out, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                  "<meta name=\"viewport\" content=\"width=device-width\">\n"
                  "<title>charcoal</title>\n<style>\n"
                  "body { margin: 0; min-height: 100vh; display: flex; "
                  "align-items: center; justify-content: center; ");
    css_color(out, "background", bg);
    buf_puts(out, "; }\n.art { font-family: \"DejaVu Sans Mono\", "
                  "\"Cascadia Mono\", \"Noto Sans Mono\", Menlo, Consolas, "
                  "ui-monospace, monospace; padding: 1ch; ");
    css_color(out, "color", fg);
    snprintf(line, sizeof(line),
             "; font-size: min(%.3fvw, %.3fvh); }\n"
             ".art div { white-space: pre; height: %.3fch; "
             "line-height: %.3fch; overflow: hidden; }\n",
             96.0 / (0.6 * (g->cols + 2)),
             96.0 / (0.6 * (g->rows * cell_aspect + 2)), cell_aspect,
             cell_aspect);
    buf_puts(out, line);
    buf_puts(out, ".art span { display: inline-block; height: 100%; "
                  "vertical-align: top; }\n"
                  ".art i { display: inline-block; width: 1ch; height: 100%; "
                  "vertical-align: top; font-style: normal; "
                  "text-align: center; }\n"
                  // Block characters are painted, filling the whole cell;
                  // the character stays (invisible) for copying
                  ".art .q { -webkit-text-fill-color: transparent; "
                  "background-repeat: no-repeat; background-size: 50% 50%; }\n");
    static const char *const POS[4] = {"0 0", "100% 0", "0 100%", "100% 100%"};
    for (int m = 1; m < 16; m++) {
        Buf imgs, poss;
        buf_init(&imgs);
        buf_init(&poss);
        for (int q = 0; q < 4; q++) {
            if (m & (1 << q)) {
                buf_puts(&imgs, imgs.len ? ", " : "");
                buf_puts(&imgs, "linear-gradient(currentColor, currentColor)");
                buf_puts(&poss, poss.len ? ", " : "");
                buf_puts(&poss, POS[q]);
            }
        }
        buf_putc(&imgs, '\0');
        buf_putc(&poss, '\0');
        snprintf(line, sizeof(line), ".art .q%d { background-image: %s; "
                 "background-position: %s; }\n", m, imgs.data, poss.data);
        buf_puts(out, line);
        buf_free(&imgs);
        buf_free(&poss);
    }
    buf_puts(out, "</style>\n</head>\n<body>\n<div class=\"art\">\n");

    for (int y = 0; y < g->rows; y++) {
        buf_puts(out, "<div>");
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
            int mask = block_mask(c->ch);
            if (c->ch == '<') {
                buf_puts(out, "&lt;");
            } else if (c->ch == '>') {
                buf_puts(out, "&gt;");
            } else if (c->ch == '&') {
                buf_puts(out, "&amp;");
            } else if (c->ch < 0x20) {
                buf_putc(out, ' ');
            } else if (c->ch < 0x80) {
                buf_putc(out, (char)c->ch);
            } else if (mask > 0) {
                snprintf(line, sizeof(line), "<i class=\"q q%d\">", mask);
                buf_puts(out, line);
                buf_utf8(out, c->ch);
                buf_puts(out, "</i>");
            } else {
                // Braille and other symbols often come from a wider fallback
                // font; a 1ch box keeps the columns straight
                buf_puts(out, "<i>");
                buf_utf8(out, c->ch);
                buf_puts(out, "</i>");
            }
        }
        if (open) {
            buf_puts(out, "</span>");
        }
        buf_puts(out, "</div>\n");
    }
    buf_puts(out, "</div>\n</body>\n</html>\n");
}
