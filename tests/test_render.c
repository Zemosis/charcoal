#include "render.h"
#include "test.h"

#include <stdlib.h>

// Fill an RGB24 buffer with one gray value per pixel
static void fill_gray(uint8_t *rgb, const uint8_t *gray, int n) {
    for (int i = 0; i < n; i++) {
        rgb[i * 3 + 0] = gray[i];
        rgb[i * 3 + 1] = gray[i];
        rgb[i * 3 + 2] = gray[i];
    }
}

void test_resize(void) {
    // 4x2 -> 2x1 averages each 2x2 block
    uint8_t g1[] = {0, 100, 200, 255,
                    0, 100, 200, 255};
    uint8_t src[8 * 3], dst[2 * 3];
    fill_gray(src, g1, 8);
    CHECK_INT(resize_rgb(src, 4, 2, dst, 2, 1), 0);
    CHECK_INT(dst[0], 50);
    CHECK_INT(dst[3], 228);     // (200 + 255) / 2 = 227.5, rounded

    // 3 -> 2 uses fractional weights: [0, 1.5) and [1.5, 3)
    uint8_t g2[] = {0, 90, 180};
    uint8_t src2[3 * 3], dst2[2 * 3];
    fill_gray(src2, g2, 3);
    CHECK_INT(resize_rgb(src2, 3, 1, dst2, 2, 1), 0);
    CHECK_INT(dst2[0], 30);     // (0 + 0.5 * 90) / 1.5
    CHECK_INT(dst2[3], 150);    // (0.5 * 90 + 180) / 1.5

    // Growing uses nearest neighbor, so pixel art stays crisp
    uint8_t g3[] = {10, 250};
    uint8_t src3[2 * 3], dst3[4 * 3];
    fill_gray(src3, g3, 2);
    CHECK_INT(resize_rgb(src3, 2, 1, dst3, 4, 1), 0);
    CHECK_INT(dst3[0], 10);
    CHECK_INT(dst3[3], 10);
    CHECK_INT(dst3[6], 250);
    CHECK_INT(dst3[9], 250);

    // Same size is a copy
    CHECK_INT(resize_rgb(src, 4, 2, dst3, 4, 1), 0);

    // Fitting: a 400x300 image in a 100x50 terminal with 1:2 cells
    int cols, rows;
    render_fit(400, 300, 100, 50, 2.0, MODE_ASCII, &cols, &rows);
    CHECK_INT(cols, 100);
    CHECK_INT(rows, 38);        // 100 * 300 / (400 * 2) = 37.5

    // Too tall: height decides
    render_fit(300, 900, 100, 50, 2.0, MODE_ASCII, &cols, &rows);
    CHECK_INT(rows, 50);
    CHECK_INT(cols, 33);

    // Small images are not upscaled
    render_fit(16, 16, 100, 50, 2.0, MODE_ASCII, &cols, &rows);
    CHECK_INT(cols, 16);
    CHECK_INT(rows, 8);
    render_fit(16, 16, 100, 50, 2.0, MODE_HALF, &cols, &rows);
    CHECK_INT(cols, 16);
    CHECK_INT(rows, 8);         // 8 rows x 2 sub-pixels = 16 pixels

    // Real cell aspect from the terminal is used
    render_fit(400, 300, 100, 100, 2.5, MODE_ASCII, &cols, &rows);
    CHECK_INT(rows, 30);
}

void test_glyph(void) {
    RenderOpts o;
    render_opts_default(&o);
    CHECK_INT(o.mode, MODE_HALF);
    CHECK(glyph_mode_from_name("braille") == MODE_BRAILLE);
    CHECK(glyph_mode_from_name("nope") == -1);
    CHECK(palette_from_name("detailed") == PALETTE_DETAILED);

    // ascii assumes a dark terminal: white is the densest char
    o.mode = MODE_ASCII;
    uint8_t g1[] = {0, 255, 128};
    uint8_t px[3 * 3];
    fill_gray(px, g1, 3);
    Grid *g = grid_new(3, 1);
    CHECK_INT(glyph_render(px, &o, g), 0);
    CHECK_INT(g->cells[0].ch, ' ');
    CHECK_INT(g->cells[1].ch, '@');
    CHECK_INT(g->cells[2].ch, '+');     // (255 - 128) * 10 / 256 = 4
    CHECK_INT(g->cells[0].fg, 0x000000);
    CHECK_INT(g->cells[1].fg, 0xFFFFFF);
    CHECK(g->cells[0].bg == CELL_COLOR_DEFAULT);

    // invert (light terminals) matches ascii_art: black is dense
    o.invert = 1;
    o.color = COLOR_MONO;
    glyph_render(px, &o, g);
    CHECK_INT(g->cells[0].ch, '@');
    CHECK_INT(g->cells[1].ch, ' ');
    CHECK_INT(g->cells[2].ch, '=');     // 128 * 10 / 256 = 5
    CHECK(g->cells[1].fg == CELL_COLOR_DEFAULT);

    // blocks mode uses the block ramp whatever the palette is
    o.mode = MODE_BLOCKS;
    o.invert = 0;
    glyph_render(px, &o, g);
    CHECK_INT(g->cells[1].ch, 0x2588);
    CHECK_INT(g->cells[0].ch, ' ');
    grid_free(g);

    // half: top pixel is fg, bottom is bg
    render_opts_default(&o);
    uint8_t px2[] = {255, 0, 0,     // top: red
                     0, 0, 255};    // bottom: blue
    g = grid_new(1, 1);
    glyph_render(px2, &o, g);
    CHECK_INT(g->cells[0].ch, 0x2580);
    CHECK_INT(g->cells[0].fg, 0xFF0000);
    CHECK_INT(g->cells[0].bg, 0x0000FF);

    // half mono: only light halves are drawn
    o.color = COLOR_MONO;
    uint8_t px3[] = {0, 0, 0, 255, 255, 255};
    glyph_render(px3, &o, g);
    CHECK_INT(g->cells[0].ch, 0x2584);
    grid_free(g);

    // render_frame resizes into the grid's size
    Frame *f = frame_new(4, 4);
    for (int i = 0; i < 16 * 3; i++) {
        f->rgb[i] = 200;
    }
    render_opts_default(&o);
    g = grid_new(2, 1);
    CHECK_INT(render_frame(f, &o, g), 0);
    CHECK_INT(g->cells[1].fg, 0xC8C8C8);
    CHECK_INT(g->cells[1].bg, 0xC8C8C8);
    grid_free(g);
    frame_free(f);
}

void test_export(void) {
    Grid *g = grid_new(2, 2);
    g->cells[0] = (Cell){'a', 0xFF0000, CELL_COLOR_DEFAULT, 0};
    g->cells[1] = (Cell){'b', 0xFF0000, CELL_COLOR_DEFAULT, 0};
    g->cells[2] = (Cell){0x2580, 0x010203, 0x0A0B0C, 0};
    g->cells[3] = (Cell){'d', CELL_COLOR_DEFAULT, CELL_COLOR_DEFAULT, 0};

    Buf b;
    buf_init(&b);
    export_txt(g, &b);
    buf_putc(&b, '\0');
    CHECK_STR(b.data, "ab\n\xe2\x96\x80" "d\n");

    // Repeated colors are not re-sent, each line resets at its end
    buf_clear(&b);
    export_ansi(g, &b);
    buf_putc(&b, '\0');
    CHECK_STR(b.data,
              "\033[38;2;255;0;0mab\033[0m\n"
              "\033[38;2;1;2;3m\033[48;2;10;11;12m\xe2\x96\x80"
              "\033[39m\033[49md\033[0m\n");

    // A grid with no colors produces no escape codes
    Grid *plain = grid_new(2, 1);
    buf_clear(&b);
    export_ansi(plain, &b);
    buf_putc(&b, '\0');
    CHECK_STR(b.data, "  \n");

    buf_free(&b);
    grid_free(plain);
    grid_free(g);
}
