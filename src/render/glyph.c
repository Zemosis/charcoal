#include "render.h"

#define MAX_RAMP 256

#define CH_UPPER_HALF 0x2580    // ▀
#define CH_LOWER_HALF 0x2584    // ▄
#define CH_FULL_BLOCK 0x2588    // █

// Prototype
static int ramp_decode(const char *palette, uint32_t *ramp);
static uint32_t pack_rgb(const uint8_t *p, int invert);
static void glyph_ascii(const uint8_t *rgb, const RenderOpts *o, Grid *g);
static void glyph_half(const uint8_t *rgb, const RenderOpts *o, Grid *g);

// Decode a UTF-8 palette into code points, returns the count
static int ramp_decode(const char *palette, uint32_t *ramp) {
    int n = 0;
    int used;
    uint32_t cp;
    while (n < MAX_RAMP && (used = utf8_decode(palette, &cp)) > 0) {
        ramp[n++] = cp;
        palette += used;
    }
    return n;
}

// Pack an RGB pixel into 0xRRGGBB, flipped if inverted
static uint32_t pack_rgb(const uint8_t *p, int invert) {
    uint32_t r = p[0], g = p[1], b = p[2];
    if (invert) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
    }
    return (r << 16) | (g << 8) | b;
}

// One pixel per cell, luminance picks a character from the ramp
static void glyph_ascii(const uint8_t *rgb, const RenderOpts *o, Grid *g) {
    uint32_t ramp[MAX_RAMP];
    const char *palette = o->mode == MODE_BLOCKS ? PALETTE_BLOCKS : o->palette;
    int n = ramp_decode(palette, ramp);
    if (n == 0) {
        ramp[0] = '#';
        n = 1;
    }

    for (int i = 0; i < g->cols * g->rows; i++) {
        const uint8_t *p = rgb + (size_t)i * 3;
        int luma = rgb_luma(p[0], p[1], p[2]);
        if (o->invert) {
            luma = 255 - luma;
        }
        Cell *c = &g->cells[i];
        c->ch = ramp[luma * n / 256];
        c->fg = o->color == COLOR_MONO ? CELL_COLOR_DEFAULT : pack_rgb(p, 0);
        c->bg = CELL_COLOR_DEFAULT;
        c->attr = 0;
    }
}

// Two pixels per cell: ▀ with the top pixel as fg and the bottom as bg
static void glyph_half(const uint8_t *rgb, const RenderOpts *o, Grid *g) {
    size_t stride = (size_t)g->cols * 3;
    for (int y = 0; y < g->rows; y++) {
        const uint8_t *top = rgb + (size_t)(y * 2) * stride;
        const uint8_t *bot = top + stride;
        for (int x = 0; x < g->cols; x++, top += 3, bot += 3) {
            Cell *c = &g->cells[y * g->cols + x];
            c->attr = 0;
            if (o->color != COLOR_MONO) {
                c->ch = CH_UPPER_HALF;
                c->fg = pack_rgb(top, o->invert);
                c->bg = pack_rgb(bot, o->invert);
                continue;
            }

            // No color: light pixels become filled halves
            int lt = rgb_luma(top[0], top[1], top[2]) >= 128;
            int lb = rgb_luma(bot[0], bot[1], bot[2]) >= 128;
            if (o->invert) {
                lt = !lt;
                lb = !lb;
            }
            c->ch = lt ? (lb ? CH_FULL_BLOCK : CH_UPPER_HALF)
                       : (lb ? CH_LOWER_HALF : ' ');
            c->fg = CELL_COLOR_DEFAULT;
            c->bg = CELL_COLOR_DEFAULT;
        }
    }
}

void glyph_render(const uint8_t *rgb, const RenderOpts *o, Grid *g) {
    switch (o->mode) {
    case MODE_HALF:
        glyph_half(rgb, o, g);
        break;
    case MODE_ASCII:
    case MODE_BLOCKS:
    case MODE_EDGES:
        // edges falls back to plain ascii until M2 adds the Sobel pass
        glyph_ascii(rgb, o, g);
        break;
    default:
        // quad and braille arrive in M2 and leave the grid untouched for now
        break;
    }
}
