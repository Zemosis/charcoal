#include "render.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

// xterm's default 16 colors, used until themes can supply the real ones
static const uint32_t ANSI16[16] = {
    0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd,
    0xe5e5e5, 0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff,
    0x00ffff, 0xffffff,
};

// Channel values of the 6x6x6 cube (indices 16-231)
static const int CUBE[6] = {0, 95, 135, 175, 215, 255};

typedef struct Lab {
    float l;
    float a;
    float b;
} Lab;

// sRGB channel to linear light, and the palette in OKLab, built once
static float linear_lut[256];
static Lab palette_lab[256];
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

// Prototype
static void build_tables(void);
static Lab rgb_to_oklab(uint32_t rgb);
static float lab_dist(Lab x, Lab y);
static int cube_level(int v);
static int nearest_256(uint32_t rgb, const Lab *pal);
static int nearest_16(uint32_t rgb, const Lab *pal);
static int clamp255(int v);

static void build_tables(void) {
    for (int v = 0; v < 256; v++) {
        float c = v / 255.0f;
        linear_lut[v] = c <= 0.04045f ? c / 12.92f
                                      : powf((c + 0.055f) / 1.055f, 2.4f);
    }
    for (int i = 0; i < 256; i++) {
        palette_lab[i] = rgb_to_oklab(color_index_rgb(i));
    }
}

// OKLab: a color space where distance follows how different colors look
static Lab rgb_to_oklab(uint32_t rgb) {
    float r = linear_lut[(rgb >> 16) & 0xFF];
    float g = linear_lut[(rgb >> 8) & 0xFF];
    float b = linear_lut[rgb & 0xFF];

    float l = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

    Lab out;
    out.l = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    out.a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    out.b = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
    return out;
}

static float lab_dist(Lab x, Lab y) {
    float dl = x.l - y.l, da = x.a - y.a, db = x.b - y.b;
    return dl * dl + da * da + db * db;
}

// Closest cube level (0-5) for one channel
static int cube_level(int v) {
    if (v < 48) {
        return 0;
    }
    if (v < 115) {
        return 1;
    }
    return (v - 35) / 40;
}

// Nearest of indices 16-255, searching the cube and grays around the color
static int nearest_256(uint32_t rgb, const Lab *pal) {
    int ch[3] = {(rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF};
    int lo[3], hi[3];
    for (int k = 0; k < 3; k++) {
        int l = cube_level(ch[k]);
        lo[k] = l > 0 ? l - 1 : 0;
        hi[k] = l < 5 ? l + 1 : 5;
    }

    Lab x = rgb_to_oklab(rgb);
    int best = 16;
    float best_d = 1e30f;
    for (int r = lo[0]; r <= hi[0]; r++) {
        for (int g = lo[1]; g <= hi[1]; g++) {
            for (int b = lo[2]; b <= hi[2]; b++) {
                int idx = 16 + 36 * r + 6 * g + b;
                float d = lab_dist(x, pal[idx]);
                if (d < best_d) {
                    best_d = d;
                    best = idx;
                }
            }
        }
    }

    // Gray ramp 232-255 is 8, 18, ..., 238; try the few around the luma
    int y = rgb_luma((uint8_t)ch[0], (uint8_t)ch[1], (uint8_t)ch[2]);
    int gi = (y - 8) / 10;
    for (int k = gi - 1; k <= gi + 2; k++) {
        if (k < 0 || k > 23) {
            continue;
        }
        float d = lab_dist(x, pal[232 + k]);
        if (d < best_d) {
            best_d = d;
            best = 232 + k;
        }
    }
    return best;
}

static int nearest_16(uint32_t rgb, const Lab *pal) {
    Lab x = rgb_to_oklab(rgb);
    int best = 0;
    float best_d = lab_dist(x, pal[0]);
    for (int i = 1; i < 16; i++) {
        float d = lab_dist(x, pal[i]);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

static int clamp255(int v) {
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

uint32_t color_index_rgb(int index) {
    if (index < 16) {
        return ANSI16[index < 0 ? 0 : index];
    }
    if (index < 232) {
        int i = index - 16;
        return (uint32_t)(CUBE[i / 36] << 16 | CUBE[(i / 6) % 6] << 8 |
                          CUBE[i % 6]);
    }
    int v = 8 + 10 * (index - 232);
    return (uint32_t)(v << 16 | v << 8 | v);
}

uint32_t color_nearest(uint32_t rgb, ColorDepth depth) {
    pthread_once(&tables_once, build_tables);
    int idx = depth == COLOR_16 ? nearest_16(rgb, palette_lab)
                                : nearest_256(rgb, palette_lab);
    return CELL_COLOR_INDEX | (uint32_t)idx;
}

void color_quantize(uint32_t *colors, int w, int h, ColorDepth depth,
                    DitherMode d) {
    if (depth != COLOR_256 && depth != COLOR_16) {
        return;
    }

    // Floyd-Steinberg carries the error per channel, in 1/16 units
    int *cur = NULL, *next = NULL;
    if (d == DITHER_FLOYD) {
        cur = calloc(((size_t)w + 2) * 3, sizeof(int));
        next = calloc(((size_t)w + 2) * 3, sizeof(int));
        if (!cur || !next) {
            free(cur);
            free(next);
            cur = next = NULL;
            d = DITHER_NONE;
        }
    }
    // Rough distance between neighboring palette colors, for Bayer
    int spread = depth == COLOR_16 ? 128 : 40;

    for (int y = 0; y < h; y++) {
        if (cur) {
            memset(next, 0, sizeof(int) * ((size_t)w + 2) * 3);
        }
        for (int x = 0; x < w; x++) {
            uint32_t *c = &colors[y * w + x];
            if (*c == CELL_COLOR_DEFAULT || (*c & CELL_COLOR_INDEX)) {
                continue;
            }
            int ch[3] = {(*c >> 16) & 0xFF, (*c >> 8) & 0xFF, *c & 0xFF};
            if (d == DITHER_BAYER) {
                int t = (2 * DITHER_BAYER4[y & 3][x & 3] - 15) * spread / 32;
                for (int k = 0; k < 3; k++) {
                    ch[k] = clamp255(ch[k] + t);
                }
            } else if (cur) {
                for (int k = 0; k < 3; k++) {
                    ch[k] = clamp255(ch[k] + cur[(x + 1) * 3 + k] / 16);
                }
            }

            uint32_t q = color_nearest((uint32_t)(ch[0] << 16 | ch[1] << 8 |
                                                  ch[2]),
                                       depth);
            *c = q;

            if (cur) {
                uint32_t qr = color_index_rgb((int)(q & 0xFF));
                int qc[3] = {(qr >> 16) & 0xFF, (qr >> 8) & 0xFF, qr & 0xFF};
                for (int k = 0; k < 3; k++) {
                    int err = ch[k] - qc[k];
                    cur[(x + 2) * 3 + k] += err * 7;
                    next[x * 3 + k] += err * 3;
                    next[(x + 1) * 3 + k] += err * 5;
                    next[(x + 2) * 3 + k] += err * 1;
                }
            }
        }
        if (cur) {
            int *t = cur;
            cur = next;
            next = t;
        }
    }
    free(cur);
    free(next);
}
