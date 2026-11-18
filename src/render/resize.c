#include "render.h"

#include <math.h>
#include <stdlib.h>

// Source pixels and weights that make up each destination pixel on one axis
typedef struct AxisMap {
    int *start;     // first source index per destination index
    int *count;     // how many source indices it covers
    float *weight;  // count weights per destination index, summing to 1
    int *offset;    // where each destination index's weights begin
} AxisMap;

// Prototype
static int axis_map_build(AxisMap *m, int src_n, int dst_n);
static void axis_map_free(AxisMap *m);
static int clamp_min1(double v);

// Build the mapping for one axis: area weights when shrinking, nearest when growing
static int axis_map_build(AxisMap *m, int src_n, int dst_n) {
    double scale = (double)src_n / dst_n;
    int max_span = scale > 1.0 ? (int)ceil(scale) + 1 : 1;

    m->start = malloc(sizeof(int) * (size_t)dst_n);
    m->count = malloc(sizeof(int) * (size_t)dst_n);
    m->offset = malloc(sizeof(int) * (size_t)dst_n);
    m->weight = malloc(sizeof(float) * (size_t)dst_n * (size_t)max_span);
    if (!m->start || !m->count || !m->offset || !m->weight) {
        axis_map_free(m);
        return -1;
    }

    int used = 0;
    for (int i = 0; i < dst_n; i++) {
        m->offset[i] = used;
        if (scale <= 1.0) {
            // Growing (or same size): nearest source pixel to the center
            int s = (int)((i + 0.5) * scale);
            m->start[i] = s < src_n ? s : src_n - 1;
            m->count[i] = 1;
            m->weight[used++] = 1.0f;
            continue;
        }

        // Shrinking: average the source span [lo, hi), partial edges weighted
        double lo = i * scale;
        double hi = (i + 1) * scale;
        int first = (int)lo;
        int last = (int)ceil(hi) - 1;
        if (last >= src_n) {
            last = src_n - 1;
        }
        m->start[i] = first;
        m->count[i] = last - first + 1;
        for (int s = first; s <= last; s++) {
            double a = s > lo ? s : lo;
            double b = s + 1 < hi ? s + 1 : hi;
            m->weight[used++] = (float)((b - a) / scale);
        }
    }
    return 0;
}

static void axis_map_free(AxisMap *m) {
    free(m->start);
    free(m->count);
    free(m->weight);
    free(m->offset);
}

static int clamp_min1(double v) {
    int n = (int)(v + 0.5);
    return n < 1 ? 1 : n;
}

int resize_rgb(const uint8_t *src, int src_w, int src_h, uint8_t *dst,
               int dst_w, int dst_h) {
    AxisMap mx = {0}, my = {0};
    float *tmp = NULL;
    int rc = -1;

    if (axis_map_build(&mx, src_w, dst_w) != 0 ||
        axis_map_build(&my, src_h, dst_h) != 0) {
        goto done;
    }

    // Horizontal pass: src_w x src_h -> dst_w x src_h, kept as floats
    tmp = malloc(sizeof(float) * (size_t)dst_w * (size_t)src_h * 3);
    if (!tmp) {
        goto done;
    }
    for (int y = 0; y < src_h; y++) {
        const uint8_t *row = src + (size_t)y * src_w * 3;
        float *out = tmp + (size_t)y * dst_w * 3;
        for (int x = 0; x < dst_w; x++) {
            const float *w = mx.weight + mx.offset[x];
            const uint8_t *p = row + (size_t)mx.start[x] * 3;
            float r = 0, g = 0, b = 0;
            for (int k = 0; k < mx.count[x]; k++, p += 3) {
                r += w[k] * p[0];
                g += w[k] * p[1];
                b += w[k] * p[2];
            }
            out[x * 3 + 0] = r;
            out[x * 3 + 1] = g;
            out[x * 3 + 2] = b;
        }
    }

    // Vertical pass: dst_w x src_h -> dst_w x dst_h
    for (int y = 0; y < dst_h; y++) {
        const float *w = my.weight + my.offset[y];
        uint8_t *out = dst + (size_t)y * dst_w * 3;
        for (int x = 0; x < dst_w * 3; x++) {
            const float *p = tmp + (size_t)my.start[y] * dst_w * 3 + x;
            float v = 0;
            for (int k = 0; k < my.count[y]; k++, p += (size_t)dst_w * 3) {
                v += w[k] * *p;
            }
            int iv = (int)(v + 0.5f);
            out[x] = (uint8_t)(iv > 255 ? 255 : iv < 0 ? 0 : iv);
        }
    }
    rc = 0;

done:
    free(tmp);
    axis_map_free(&mx);
    axis_map_free(&my);
    return rc;
}

int render_rows_for_cols(int img_w, int img_h, int cols, double cell_aspect) {
    return clamp_min1((double)cols * img_h / ((double)img_w * cell_aspect));
}

int render_cols_for_rows(int img_w, int img_h, int rows, double cell_aspect) {
    return clamp_min1((double)rows * cell_aspect * img_w / img_h);
}

void render_fit(int img_w, int img_h, int max_cols, int max_rows,
                double cell_aspect, GlyphMode mode, int *cols, int *rows) {
    int sub_w, sub_h;
    mode_subpixels(mode, &sub_w, &sub_h);
    if (max_cols < 1) {
        max_cols = 1;
    }
    if (max_rows < 1) {
        max_rows = 1;
    }

    // Native size: one image pixel per sub-pixel, rounded up like ascii_art
    int native_cols = (img_w + sub_w - 1) / sub_w;
    int c = max_cols < native_cols ? max_cols : native_cols;
    int r = render_rows_for_cols(img_w, img_h, c, cell_aspect);

    // Too tall: let the height decide instead
    if (r > max_rows) {
        r = max_rows;
        c = render_cols_for_rows(img_w, img_h, r, cell_aspect);
        if (c > max_cols) {
            c = max_cols;
        }
    }
    *cols = c;
    *rows = r;
}
