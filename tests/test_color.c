#include "render.h"
#include "test.h"

#include <stdlib.h>

// Set every channel of n pixels to one gray value
static void fill_rgb(uint8_t *rgb, int n, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < n; i++) {
        rgb[i * 3 + 0] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
    }
}

// Run glyph_render on a w x h sub-pixel image and return the grid
static Grid *render_px(const uint8_t *px, int cols, int rows,
                       const RenderOpts *o) {
    Grid *g = grid_new(cols, rows);
    CHECK_INT(glyph_render(px, o, g), 0);
    return g;
}

static void test_adjust(void) {
    RenderOpts o;
    render_opts_default(&o);
    uint8_t px[3] = {100, 150, 200};

    // Neutral settings change nothing
    adjust_rgb(px, 1, &o);
    CHECK_INT(px[0], 100);
    CHECK_INT(px[2], 200);

    o.brightness = 0.2;     // + 51
    adjust_rgb(px, 1, &o);
    CHECK_INT(px[0], 151);
    CHECK_INT(px[2], 251);

    // Contrast spreads values around the middle, and clamps
    render_opts_default(&o);
    o.contrast = 2.0;
    uint8_t px2[3] = {60, 128, 250};
    adjust_rgb(px2, 1, &o);
    CHECK_INT(px2[0], 0);
    CHECK_INT(px2[1], 129);
    CHECK_INT(px2[2], 255);

    // Gamma above 1 lifts midtones, keeps black and white
    render_opts_default(&o);
    o.gamma = 2.0;
    uint8_t px3[3] = {0, 64, 255};
    adjust_rgb(px3, 1, &o);
    CHECK_INT(px3[0], 0);
    CHECK_INT(px3[1], 128);
    CHECK_INT(px3[2], 255);

    // Saturation 0 is grayscale
    render_opts_default(&o);
    o.saturation = 0.0;
    uint8_t px4[3] = {255, 0, 0};
    adjust_rgb(px4, 1, &o);
    CHECK_INT(px4[0], 76);
    CHECK_INT(px4[1], 76);
    CHECK_INT(px4[2], 76);
}

static void test_dither(void) {
    uint8_t luma[64], out[64];
    for (int i = 0; i < 64; i++) {
        luma[i] = 128;
    }

    // Plain bins: 128 of 2 levels is the upper bin everywhere
    dither_levels(luma, 8, 8, 2, DITHER_NONE, out);
    int on = 0;
    for (int i = 0; i < 64; i++) {
        on += out[i];
    }
    CHECK_INT(on, 64);

    // Dithering a 50% gray lights about half the pixels
    dither_levels(luma, 8, 8, 2, DITHER_FLOYD, out);
    on = 0;
    for (int i = 0; i < 64; i++) {
        on += out[i];
    }
    CHECK(on >= 28 && on <= 36);

    dither_levels(luma, 8, 8, 2, DITHER_BAYER, out);
    on = 0;
    for (int i = 0; i < 64; i++) {
        on += out[i];
    }
    CHECK_INT(on, 32);

    // Black and white stay put whatever the dither
    uint8_t ends[2] = {0, 255}, eo[2];
    for (int d = 0; d < DITHER_COUNT; d++) {
        dither_levels(ends, 2, 1, 10, (DitherMode)d, eo);
        CHECK_INT(eo[0], 0);
        CHECK_INT(eo[1], 9);
    }
}

static void test_palette(void) {
    CHECK_INT(color_index_rgb(1), 0xcd0000);
    CHECK_INT(color_index_rgb(16), 0x000000);
    CHECK_INT(color_index_rgb(196), 0xff0000);
    CHECK_INT(color_index_rgb(231), 0xffffff);
    CHECK_INT(color_index_rgb(232), 0x080808);
    CHECK_INT(color_index_rgb(255), 0xeeeeee);

    // Palette colors map to themselves (256 never picks 0-15)
    CHECK_INT(color_nearest(0xff0000, COLOR_256), CELL_COLOR_INDEX | 196);
    CHECK_INT(color_nearest(0x5f87af, COLOR_256), CELL_COLOR_INDEX | 67);
    CHECK_INT(color_nearest(0x808080, COLOR_256), CELL_COLOR_INDEX | 244);
    CHECK_INT(color_nearest(0xff0000, COLOR_16), CELL_COLOR_INDEX | 9);
    CHECK_INT(color_nearest(0x000000, COLOR_16), CELL_COLOR_INDEX | 0);
    CHECK_INT(color_nearest(0x0000ee, COLOR_16), CELL_COLOR_INDEX | 4);

    // Quantizing leaves default colors alone
    uint32_t plane[2] = {0xff0000, CELL_COLOR_DEFAULT};
    color_quantize(plane, 2, 1, COLOR_256, DITHER_FLOYD);
    CHECK_INT(plane[0], CELL_COLOR_INDEX | 196);
    CHECK(plane[1] == CELL_COLOR_DEFAULT);
}

static void test_quad(void) {
    RenderOpts o;
    render_opts_default(&o);
    o.mode = MODE_QUAD;

    // Left column red, right column blue: ▌ with red fg, blue bg
    uint8_t px[4 * 3] = {255, 0, 0, 0, 0, 255,
                         255, 0, 0, 0, 0, 255};
    Grid *g = render_px(px, 1, 1, &o);
    CHECK_INT(g->cells[0].ch, 0x258C);
    CHECK_INT(g->cells[0].fg, 0xFF0000);
    CHECK_INT(g->cells[0].bg, 0x0000FF);
    grid_free(g);

    // A solid cell is one full block
    fill_rgb(px, 4, 10, 20, 30);
    g = render_px(px, 1, 1, &o);
    CHECK_INT(g->cells[0].ch, 0x2588);
    CHECK_INT(g->cells[0].fg, 0x0A141E);
    grid_free(g);

    // Mono: only the light bottom-right pixel is drawn, ▗
    o.color = COLOR_MONO;
    fill_rgb(px, 4, 0, 0, 0);
    fill_rgb(px + 9, 1, 255, 255, 255);
    g = render_px(px, 1, 1, &o);
    CHECK_INT(g->cells[0].ch, 0x2597);
    grid_free(g);
}

static void test_braille(void) {
    RenderOpts o;
    render_opts_default(&o);
    o.mode = MODE_BRAILLE;

    // Light left column of the 2x4 cell: dots 1, 2, 3, 7
    uint8_t px[8 * 3];
    fill_rgb(px, 8, 0, 0, 0);
    for (int y = 0; y < 4; y++) {
        fill_rgb(px + (y * 2) * 3, 1, 250, 200, 50);
    }
    Grid *g = render_px(px, 1, 1, &o);
    CHECK_INT(g->cells[0].ch, 0x2800 + 0x47);
    CHECK_INT(g->cells[0].fg, 0xFAC832);
    grid_free(g);

    // All dark: empty braille with no color
    fill_rgb(px, 8, 0, 0, 0);
    g = render_px(px, 1, 1, &o);
    CHECK_INT(g->cells[0].ch, 0x2800);
    CHECK(g->cells[0].fg == CELL_COLOR_DEFAULT);
    grid_free(g);
}

static void test_edges(void) {
    RenderOpts o;
    render_opts_default(&o);
    o.mode = MODE_EDGES;
    o.color = COLOR_MONO;

    // Left half dark, right half light: a vertical edge
    uint8_t px[6 * 4 * 3];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 6; x++) {
            uint8_t v = x < 3 ? 0 : 255;
            fill_rgb(px + (y * 6 + x) * 3, 1, v, v, v);
        }
    }
    Grid *g = render_px(px, 6, 4, &o);
    CHECK_INT(g->cells[1 * 6 + 2].ch, '|');
    CHECK_INT(g->cells[1 * 6 + 0].ch, ' ');     // dark, flat
    CHECK_INT(g->cells[1 * 6 + 5].ch, '@');     // light, flat
    grid_free(g);

    // Top dark, bottom light: a horizontal edge
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 6; x++) {
            uint8_t v = y < 2 ? 0 : 255;
            fill_rgb(px + (y * 6 + x) * 3, 1, v, v, v);
        }
    }
    g = render_px(px, 6, 4, &o);
    CHECK_INT(g->cells[1 * 6 + 3].ch, '-');
    grid_free(g);

    // A diagonal: light below the line y = x
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 6; x++) {
            uint8_t v = y * 2 > x ? 255 : 0;
            fill_rgb(px + (y * 6 + x) * 3, 1, v, v, v);
        }
    }
    g = render_px(px, 6, 4, &o);
    CHECK_INT(g->cells[2 * 6 + 3].ch, '\\');
    grid_free(g);
}

static void test_color_export(void) {
    // 256 and 16 colors come out as palette codes
    RenderOpts o;
    render_opts_default(&o);
    uint8_t px[2 * 3] = {255, 0, 0, 0, 0, 0};
    o.color = COLOR_16;
    Grid *g = render_px(px, 1, 1, &o);
    CHECK_INT(g->cells[0].fg, CELL_COLOR_INDEX | 9);
    CHECK_INT(g->cells[0].bg, CELL_COLOR_INDEX | 0);

    Buf b;
    buf_init(&b);
    export_ansi(g, &b);
    buf_putc(&b, '\0');
    CHECK_STR(b.data, "\033[91m\033[40m\xe2\x96\x80\033[0m\n");
    grid_free(g);

    o.color = COLOR_256;
    g = render_px(px, 1, 1, &o);
    buf_clear(&b);
    export_ansi(g, &b);
    buf_putc(&b, '\0');
    CHECK_STR(b.data, "\033[38;5;196m\033[48;5;16m\xe2\x96\x80\033[0m\n");
    grid_free(g);
    buf_free(&b);
}

void test_color(void) {
    test_adjust();
    test_dither();
    test_palette();
    test_quad();
    test_braille();
    test_edges();
    test_color_export();
}
