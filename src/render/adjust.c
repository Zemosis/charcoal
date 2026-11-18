#include "render.h"

#include <math.h>

// Prototype
static int is_neutral(const RenderOpts *o);
static void build_lut(const RenderOpts *o, uint8_t *lut);
static uint8_t clamp_u8(double v);

static int is_neutral(const RenderOpts *o) {
    return o->brightness == 0.0 && o->contrast == 1.0 && o->gamma == 1.0 &&
           o->saturation == 1.0;
}

static uint8_t clamp_u8(double v) {
    if (v <= 0.0) {
        return 0;
    }
    if (v >= 255.0) {
        return 255;
    }
    return (uint8_t)(v + 0.5);
}

// One table for brightness, contrast and gamma, applied to each channel
static void build_lut(const RenderOpts *o, uint8_t *lut) {
    double gamma = o->gamma > 0.0 ? o->gamma : 1.0;
    for (int i = 0; i < 256; i++) {
        double v = i / 255.0;
        v = pow(v, 1.0 / gamma);
        v = (v - 0.5) * o->contrast + 0.5;
        v += o->brightness;
        lut[i] = clamp_u8(v * 255.0);
    }
}

void adjust_rgb(uint8_t *rgb, int n, const RenderOpts *o) {
    if (is_neutral(o)) {
        return;
    }
    uint8_t lut[256];
    build_lut(o, lut);
    double s = o->saturation;

    for (int i = 0; i < n; i++, rgb += 3) {
        if (s != 1.0) {
            // Push each channel away from (or toward) the pixel's gray
            double y = rgb_luma(rgb[0], rgb[1], rgb[2]);
            rgb[0] = clamp_u8(y + (rgb[0] - y) * s);
            rgb[1] = clamp_u8(y + (rgb[1] - y) * s);
            rgb[2] = clamp_u8(y + (rgb[2] - y) * s);
        }
        rgb[0] = lut[rgb[0]];
        rgb[1] = lut[rgb[1]];
        rgb[2] = lut[rgb[2]];
    }
}
