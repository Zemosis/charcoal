#include "render.h"

#include <math.h>
#include <stdlib.h>

#define MAX_RAMP 256

#define CH_UPPER_HALF 0x2580    // ▀
#define CH_LOWER_HALF 0x2584    // ▄
#define CH_FULL_BLOCK 0x2588    // █
#define CH_BRAILLE    0x2800    // ⠀, dots are added as bits

#define PI 3.14159265358979323846

// Sobel magnitude above which a cell counts as an edge
#define EDGE_THRESHOLD 256.0

// Quadrant characters by mask: 1 top-left, 2 top-right, 4 bottom-left,
// 8 bottom-right
static const uint32_t QUAD_CHARS[16] = {
    ' ',    0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
    0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588,
};

// Braille dot bit for each position in the 2x4 cell, [y][x]
static const uint8_t BRAILLE_BITS[4][2] = {
    {0x01, 0x08},
    {0x02, 0x10},
    {0x04, 0x20},
    {0x40, 0x80},
};

// Quad masks to try for a 2-color split; the top-left pixel is always fg,
// so each split is tried once, and the solid cell first so it wins ties
static const int QUAD_SPLITS[8] = {15, 1, 3, 5, 7, 9, 11, 13};

// Scratch planes shared by the mode functions
typedef struct Planes {
    int w;              // sub-pixel image size
    int h;
    uint8_t *luma;      // w * h, already inverted when asked
    uint8_t *level;     // w * h, quantized luma
} Planes;

// Prototype
static int ramp_decode(const char *palette, uint32_t *ramp);
static uint32_t pack_rgb(const uint8_t *p, int invert);
static void glyph_ascii(const uint8_t *rgb, const Planes *pl,
                        const RenderOpts *o, Grid *g);
static void glyph_edges(const Planes *pl, const RenderOpts *o, Grid *g);
static void glyph_half(const uint8_t *rgb, const Planes *pl,
                       const RenderOpts *o, Grid *g);
static void glyph_quad(const uint8_t *rgb, const Planes *pl,
                       const RenderOpts *o, Grid *g);
static void glyph_braille(const uint8_t *rgb, const Planes *pl,
                          const RenderOpts *o, Grid *g);
static int quantize_cells(const RenderOpts *o, Grid *g);

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
static void glyph_ascii(const uint8_t *rgb, const Planes *pl,
                        const RenderOpts *o, Grid *g) {
    uint32_t ramp[MAX_RAMP];
    const char *palette = o->mode == MODE_BLOCKS ? PALETTE_BLOCKS : o->palette;
    int n = ramp_decode(palette, ramp);
    if (n == 0) {
        ramp[0] = '#';
        n = 1;
    }
    dither_levels(pl->luma, pl->w, pl->h, n, o->dither, pl->level);

    for (int i = 0; i < g->cols * g->rows; i++) {
        Cell *c = &g->cells[i];
        c->ch = ramp[pl->level[i]];
        c->fg = o->color == COLOR_MONO ? CELL_COLOR_DEFAULT
                                       : pack_rgb(rgb + (size_t)i * 3, 0);
        c->bg = CELL_COLOR_DEFAULT;
        c->attr = 0;
    }
}

// Strong edges become / \ | - by direction, everything else is ascii
static void glyph_edges(const Planes *pl, const RenderOpts *o, Grid *g) {
    const uint8_t *l = pl->luma;
    int w = pl->w, h = pl->h;
    double aspect = o->cell_aspect > 0 ? o->cell_aspect : 2.0;

    for (int y = 0; y < h; y++) {
        int y0 = y > 0 ? y - 1 : 0, y1 = y < h - 1 ? y + 1 : h - 1;
        for (int x = 0; x < w; x++) {
            int x0 = x > 0 ? x - 1 : 0, x1 = x < w - 1 ? x + 1 : w - 1;
            int gx = (l[y0 * w + x1] + 2 * l[y * w + x1] + l[y1 * w + x1]) -
                     (l[y0 * w + x0] + 2 * l[y * w + x0] + l[y1 * w + x0]);
            int gy = (l[y1 * w + x0] + 2 * l[y1 * w + x] + l[y1 * w + x1]) -
                     (l[y0 * w + x0] + 2 * l[y0 * w + x] + l[y0 * w + x1]);

            // Cells are taller than wide, so a row step covers more distance
            double gyn = gy / aspect;
            if (gx * (double)gx + gyn * gyn < EDGE_THRESHOLD * EDGE_THRESHOLD) {
                continue;
            }

            // The edge runs across the gradient; y grows downward
            double a = atan2(gyn, gx) * 180.0 / PI;
            if (a < 0) {
                a += 180.0;
            }
            uint32_t ch = a < 22.5 || a >= 157.5 ? '|'
                        : a < 67.5               ? '/'
                        : a < 112.5              ? '-'
                                                 : '\\';
            g->cells[y * w + x].ch = ch;
        }
    }
}

// Two pixels per cell: ▀ with the top pixel as fg and the bottom as bg
static void glyph_half(const uint8_t *rgb, const Planes *pl,
                       const RenderOpts *o, Grid *g) {
    if (o->color == COLOR_MONO) {
        dither_levels(pl->luma, pl->w, pl->h, 2, o->dither, pl->level);
    }

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
            int lt = pl->level[(y * 2) * pl->w + x];
            int lb = pl->level[(y * 2 + 1) * pl->w + x];
            c->ch = lt ? (lb ? CH_FULL_BLOCK : CH_UPPER_HALF)
                       : (lb ? CH_LOWER_HALF : ' ');
            c->fg = CELL_COLOR_DEFAULT;
            c->bg = CELL_COLOR_DEFAULT;
        }
    }
}

// Four pixels per cell: the quadrant shape and 2 colors that fit them best
static void glyph_quad(const uint8_t *rgb, const Planes *pl,
                       const RenderOpts *o, Grid *g) {
    if (o->color == COLOR_MONO) {
        dither_levels(pl->luma, pl->w, pl->h, 2, o->dither, pl->level);
    }

    for (int y = 0; y < g->rows; y++) {
        for (int x = 0; x < g->cols; x++) {
            Cell *c = &g->cells[y * g->cols + x];
            c->attr = 0;

            // Pixel k: bit 1 << k, so k = 0 top-left .. 3 bottom-right
            int px[4], py[4];
            for (int k = 0; k < 4; k++) {
                px[k] = x * 2 + (k & 1);
                py[k] = y * 2 + (k >> 1);
            }

            if (o->color == COLOR_MONO) {
                int mask = 0;
                for (int k = 0; k < 4; k++) {
                    if (pl->level[py[k] * pl->w + px[k]]) {
                        mask |= 1 << k;
                    }
                }
                c->ch = QUAD_CHARS[mask];
                c->fg = CELL_COLOR_DEFAULT;
                c->bg = CELL_COLOR_DEFAULT;
                continue;
            }

            int p[4][3];
            long sq = 0;
            for (int k = 0; k < 4; k++) {
                uint32_t v = pack_rgb(rgb + ((size_t)py[k] * pl->w + px[k]) * 3,
                                      o->invert);
                p[k][0] = (v >> 16) & 0xFF;
                p[k][1] = (v >> 8) & 0xFF;
                p[k][2] = v & 0xFF;
                sq += (long)p[k][0] * p[k][0] + (long)p[k][1] * p[k][1] +
                      (long)p[k][2] * p[k][2];
            }

            // Error of a split = sum of squares - |sum|^2 / count per group
            double best_err = -1;
            int best = 15;
            int fg[3] = {0}, bg[3] = {0};
            for (int s = 0; s < 8; s++) {
                int mask = QUAD_SPLITS[s];
                long in[3] = {0}, out[3] = {0};
                int nin = 0;
                for (int k = 0; k < 4; k++) {
                    long *dst = (mask >> k) & 1 ? in : out;
                    nin += (mask >> k) & 1;
                    for (int ch = 0; ch < 3; ch++) {
                        dst[ch] += p[k][ch];
                    }
                }
                int nout = 4 - nin;
                double err = (double)sq;
                for (int ch = 0; ch < 3; ch++) {
                    err -= (double)in[ch] * in[ch] / nin;
                    if (nout) {
                        err -= (double)out[ch] * out[ch] / nout;
                    }
                }
                if (best_err < 0 || err < best_err - 1e-6) {
                    best_err = err;
                    best = mask;
                    for (int ch = 0; ch < 3; ch++) {
                        fg[ch] = (int)((in[ch] + nin / 2) / nin);
                        bg[ch] = nout ? (int)((out[ch] + nout / 2) / nout)
                                      : fg[ch];
                    }
                }
            }
            c->ch = QUAD_CHARS[best];
            c->fg = (uint32_t)(fg[0] << 16 | fg[1] << 8 | fg[2]);
            c->bg = (uint32_t)(bg[0] << 16 | bg[1] << 8 | bg[2]);
        }
    }
}

// Eight dots per cell, lit where the (dithered) luminance is light
static void glyph_braille(const uint8_t *rgb, const Planes *pl,
                          const RenderOpts *o, Grid *g) {
    dither_levels(pl->luma, pl->w, pl->h, 2, o->dither, pl->level);

    for (int y = 0; y < g->rows; y++) {
        for (int x = 0; x < g->cols; x++) {
            Cell *c = &g->cells[y * g->cols + x];
            int bits = 0, lit = 0;
            long sum[3] = {0};
            for (int dy = 0; dy < 4; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    size_t i = (size_t)(y * 4 + dy) * pl->w + x * 2 + dx;
                    if (!pl->level[i]) {
                        continue;
                    }
                    bits |= BRAILLE_BITS[dy][dx];
                    lit++;
                    for (int ch = 0; ch < 3; ch++) {
                        sum[ch] += rgb[i * 3 + ch];
                    }
                }
            }
            c->ch = CH_BRAILLE + (uint32_t)bits;
            c->bg = CELL_COLOR_DEFAULT;
            c->attr = 0;

            // The dots take the average color of the pixels they stand for
            if (o->color == COLOR_MONO || lit == 0) {
                c->fg = CELL_COLOR_DEFAULT;
            } else {
                c->fg = (uint32_t)((sum[0] / lit) << 16 | (sum[1] / lit) << 8 |
                                   (sum[2] / lit));
            }
        }
    }
}

// Reduce cell colors to the 256 or 16 color palette
static int quantize_cells(const RenderOpts *o, Grid *g) {
    if (o->color != COLOR_256 && o->color != COLOR_16) {
        return 0;
    }
    int n = g->cols * g->rows;
    uint32_t *plane = malloc(sizeof(uint32_t) * (size_t)n * 2);
    if (!plane) {
        return -1;
    }

    if (o->mode == MODE_HALF) {
        // fg and bg are the image's rows, so dither them as one image
        for (int y = 0; y < g->rows; y++) {
            for (int x = 0; x < g->cols; x++) {
                Cell *c = &g->cells[y * g->cols + x];
                plane[(y * 2) * g->cols + x] = c->fg;
                plane[(y * 2 + 1) * g->cols + x] = c->bg;
            }
        }
        color_quantize(plane, g->cols, g->rows * 2, o->color, o->dither);
        for (int y = 0; y < g->rows; y++) {
            for (int x = 0; x < g->cols; x++) {
                Cell *c = &g->cells[y * g->cols + x];
                c->fg = plane[(y * 2) * g->cols + x];
                c->bg = plane[(y * 2 + 1) * g->cols + x];
            }
        }
        free(plane);
        return 0;
    }

    // Quad and braille colors are already per-cell averages, so no dither
    int split = o->mode == MODE_QUAD || o->mode == MODE_BRAILLE;
    DitherMode d = split ? DITHER_NONE : o->dither;
    for (int i = 0; i < n; i++) {
        plane[i] = g->cells[i].fg;
        plane[n + i] = g->cells[i].bg;
    }
    color_quantize(plane, g->cols, g->rows, o->color, d);
    color_quantize(plane + n, g->cols, g->rows, o->color, DITHER_NONE);
    for (int i = 0; i < n; i++) {
        g->cells[i].fg = plane[i];
        g->cells[i].bg = plane[n + i];
    }
    free(plane);
    return 0;
}

int glyph_render(const uint8_t *rgb, const RenderOpts *o, Grid *g) {
    Planes pl;
    int sub_w, sub_h;
    mode_subpixels(o->mode, &sub_w, &sub_h);
    pl.w = g->cols * sub_w;
    pl.h = g->rows * sub_h;
    size_t n = (size_t)pl.w * (size_t)pl.h;
    pl.luma = malloc(n);
    pl.level = malloc(n);
    if (!pl.luma || !pl.level) {
        free(pl.luma);
        free(pl.level);
        return -1;
    }
    // Ramps list dense glyphs first and terminals are assumed dark, so the
    // ramp modes look up bright pixels as "dense"; invert undoes it for light
    // terminals (and then matches ascii_art exactly)
    int ramp_mode = o->mode == MODE_ASCII || o->mode == MODE_BLOCKS ||
                    o->mode == MODE_EDGES;
    int flip = ramp_mode ? !o->invert : o->invert;
    for (size_t i = 0; i < n; i++) {
        uint8_t l = rgb_luma(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
        pl.luma[i] = flip ? 255 - l : l;
    }

    switch (o->mode) {
    case MODE_ASCII:
    case MODE_BLOCKS:
        glyph_ascii(rgb, &pl, o, g);
        break;
    case MODE_EDGES:
        glyph_ascii(rgb, &pl, o, g);
        glyph_edges(&pl, o, g);
        break;
    case MODE_HALF:
        glyph_half(rgb, &pl, o, g);
        break;
    case MODE_QUAD:
        glyph_quad(rgb, &pl, o, g);
        break;
    case MODE_BRAILLE:
        glyph_braille(rgb, &pl, o, g);
        break;
    default:
        break;
    }
    free(pl.luma);
    free(pl.level);
    return quantize_cells(o, g);
}
