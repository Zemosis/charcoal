#include "render.h"

#include <stdlib.h>
#include <string.h>

const int DITHER_BAYER4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Prototype
static int nearest_level(int v, int levels);
static void levels_floyd(const uint8_t *luma, int w, int h, int levels,
                         uint8_t *out);

// Round v (0-255) to the closest of levels evenly spaced steps
static int nearest_level(int v, int levels) {
    int k = (v * (levels - 1) + 127) / 255;
    return k < 0 ? 0 : k >= levels ? levels - 1 : k;
}

// Floyd-Steinberg error diffusion, two rows of error kept at a time
static void levels_floyd(const uint8_t *luma, int w, int h, int levels,
                         uint8_t *out) {
    // One spare slot on each side so neighbors never need bounds checks
    int *cur = calloc((size_t)w + 2, sizeof(int));
    int *next = calloc((size_t)w + 2, sizeof(int));
    if (!cur || !next) {
        free(cur);
        free(next);
        dither_levels(luma, w, h, levels, DITHER_NONE, out);
        return;
    }

    for (int y = 0; y < h; y++) {
        memset(next, 0, sizeof(int) * ((size_t)w + 2));
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            // Errors are kept in 1/16 units
            int v = luma[i] + cur[x + 1] / 16;
            v = v < 0 ? 0 : v > 255 ? 255 : v;
            int k = nearest_level(v, levels);
            out[i] = (uint8_t)k;

            int err = v - k * 255 / (levels - 1);
            cur[x + 2] += err * 7;
            next[x] += err * 3;
            next[x + 1] += err * 5;
            next[x + 2] += err * 1;
        }
        int *t = cur;
        cur = next;
        next = t;
    }
    free(cur);
    free(next);
}

void dither_levels(const uint8_t *luma, int w, int h, int levels,
                   DitherMode d, uint8_t *out) {
    if (levels < 2) {
        memset(out, 0, (size_t)w * (size_t)h);
        return;
    }

    if (d == DITHER_FLOYD) {
        levels_floyd(luma, w, h, levels, out);
        return;
    }

    int step = 255 / (levels - 1);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            if (d == DITHER_BAYER) {
                // Nudge by up to half a step either way, same pattern each frame
                int t = DITHER_BAYER4[y & 3][x & 3];
                int v = luma[i] + (2 * t - 15) * step / 32;
                out[i] = (uint8_t)nearest_level(v, levels);
            } else {
                // Equal-width bins, as ascii_art picked characters
                out[i] = (uint8_t)(luma[i] * levels / 256);
            }
        }
    }
}
