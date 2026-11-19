#include "render.h"

#include <math.h>
#include <stdlib.h>

#define MAX_RAMP 256

#define CH_UPPER_HALF 0x2580    // ▀
#define CH_LOWER_HALF 0x2584    // ▄
#define CH_FULL_BLOCK 0x2588    // █
#define CH_BRAILLE    0x2800    // ⠀, dots are added as bits

#define PI 3.14159265358979323846

// Edges: the strongest share of gradients count, but never weaker ones
// than EDGE_MIN (a flat picture has no edges); magnitudes are binned for it
#define EDGE_SHARE 0.15
#define EDGE_MIN 96.0
#define EDGE_BINS 256
#define EDGE_BIN_W 8.0

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
static int glyph_edges(const Planes *pl, const RenderOpts *o, Grid *g);
static int edges_base(const uint8_t *rgb, const Planes *pl, const RenderOpts *o,
                      Grid *g);
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
    int n = ramp_decode(o->palette, ramp);
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

// Edge lines from 2x2 samples per cell, Canny-style: Sobel gradients,
// thinned to their peaks, against a threshold taken from the picture
// itself (its strongest EDGE_SHARE of gradients, never below EDGE_MIN).
// Each cell shows its strongest edge as / \ | -, or keeps its ascii glyph.
// Returns 0, or -1 if out of memory
static int glyph_edges(const Planes *pl, const RenderOpts *o, Grid *g) {
    const uint8_t *l = pl->luma;
    int w = pl->w, h = pl->h;
    size_t n = (size_t)w * (size_t)h;
    double aspect = o->cell_aspect > 0 ? o->cell_aspect : 2.0;
    float *mag = malloc(sizeof(float) * n);
    uint8_t *dir = malloc(n);
    if (!mag || !dir) {
        free(mag);
        free(dir);
        return -1;
    }

    // Gradients; samples are cell_aspect times taller than wide, like cells
    int hist[EDGE_BINS] = {0};
    for (int y = 0; y < h; y++) {
        int y0 = y > 0 ? y - 1 : 0, y1 = y < h - 1 ? y + 1 : h - 1;
        for (int x = 0; x < w; x++) {
            int x0 = x > 0 ? x - 1 : 0, x1 = x < w - 1 ? x + 1 : w - 1;
            int gx = (l[y0 * w + x1] + 2 * l[y * w + x1] + l[y1 * w + x1]) -
                     (l[y0 * w + x0] + 2 * l[y * w + x0] + l[y1 * w + x0]);
            int gy = (l[y1 * w + x0] + 2 * l[y1 * w + x] + l[y1 * w + x1]) -
                     (l[y0 * w + x0] + 2 * l[y0 * w + x] + l[y0 * w + x1]);
            double gyn = gy / aspect;
            double m = sqrt(gx * (double)gx + gyn * gyn);
            mag[y * w + x] = (float)m;
            int bin = (int)(m / EDGE_BIN_W);
            hist[bin < EDGE_BINS ? bin : EDGE_BINS - 1]++;

            // Gradient direction in 4 steps; the edge runs across it
            double a = atan2(gyn, gx) * 180.0 / PI;
            if (a < 0) {
                a += 180.0;
            }
            dir[y * w + x] = a < 22.5 || a >= 157.5 ? 0
                           : a < 67.5               ? 1
                           : a < 112.5              ? 2
                                                    : 3;
        }
    }

    // Threshold: what the strongest EDGE_SHARE of samples exceed
    double thr = EDGE_MIN;
    size_t above = 0;
    for (int b = EDGE_BINS - 1; b >= 0; b--) {
        above += (size_t)hist[b];
        if (above >= (size_t)(n * EDGE_SHARE)) {
            double t = b * EDGE_BIN_W;
            thr = t > thr ? t : thr;
            break;
        }
    }

    // Neighbors along the gradient for each direction, to keep only peaks
    static const int NX[4] = {1, 1, 0, -1};
    static const int NY[4] = {0, 1, 1, 1};
    static const uint32_t CHARS[4] = {'|', '/', '-', '\\'};
    for (int cy = 0; cy < g->rows; cy++) {
        for (int cx = 0; cx < g->cols; cx++) {
            float best = 0;
            int best_dir = -1;
            for (int k = 0; k < 4; k++) {
                int x = cx * 2 + (k & 1), y = cy * 2 + (k >> 1);
                if (x >= w || y >= h) {
                    continue;
                }
                float m = mag[y * w + x];
                if (m < thr || m <= best) {
                    continue;
                }
                int d = dir[y * w + x];
                int ax = x + NX[d], ay = y + NY[d], bx = x - NX[d], by = y - NY[d];
                float ma = ax >= 0 && ay >= 0 && ax < w && ay < h ? mag[ay * w + ax]
                                                                  : 0;
                float mb = bx >= 0 && by >= 0 && bx < w && by < h ? mag[by * w + bx]
                                                                  : 0;
                if (m >= ma && m >= mb) {
                    best = m;
                    best_dir = d;
                }
            }
            if (best_dir >= 0) {
                g->cells[cy * g->cols + cx].ch = CHARS[best_dir];
            }
        }
    }
    free(mag);
    free(dir);
    return 0;
}

// The ascii glyph under the edges: each cell's 2x2 samples averaged
static int edges_base(const uint8_t *rgb, const Planes *pl, const RenderOpts *o,
                      Grid *g) {
    int cols = g->cols, rows = g->rows;
    size_t n = (size_t)cols * (size_t)rows;
    uint8_t *crgb = malloc(n * 3);
    Planes cp = {cols, rows, malloc(n), malloc(n)};
    if (!crgb || !cp.luma || !cp.level) {
        free(crgb);
        free(cp.luma);
        free(cp.level);
        return -1;
    }
    for (int cy = 0; cy < rows; cy++) {
        for (int cx = 0; cx < cols; cx++) {
            int sum[3] = {0}, lsum = 0;
            for (int k = 0; k < 4; k++) {
                size_t i = (size_t)(cy * 2 + (k >> 1)) * pl->w + cx * 2 + (k & 1);
                for (int c = 0; c < 3; c++) {
                    sum[c] += rgb[i * 3 + c];
                }
                lsum += pl->luma[i];
            }
            size_t j = (size_t)cy * cols + cx;
            for (int c = 0; c < 3; c++) {
                crgb[j * 3 + c] = (uint8_t)((sum[c] + 2) / 4);
            }
            cp.luma[j] = (uint8_t)((lsum + 2) / 4);
        }
    }
    glyph_ascii(crgb, &cp, o, g);
    free(crgb);
    free(cp.luma);
    free(cp.level);
    return 0;
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
    int ramp_mode = o->mode == MODE_ASCII || o->mode == MODE_EDGES;
    int flip = ramp_mode ? !o->invert : o->invert;
    for (size_t i = 0; i < n; i++) {
        uint8_t l = rgb_luma(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
        pl.luma[i] = flip ? 255 - l : l;
    }

    switch (o->mode) {
    case MODE_ASCII:
        glyph_ascii(rgb, &pl, o, g);
        break;
    case MODE_EDGES:
        if (edges_base(rgb, &pl, o, g) != 0 || glyph_edges(&pl, o, g) != 0) {
            free(pl.luma);
            free(pl.level);
            return -1;
        }
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
