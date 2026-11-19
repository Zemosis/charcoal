#include "app.h"

#include <math.h>

#define MAX_BARS 512

void view_histogram(App *a) {
    Rect r = a->r_histogram;
    if (r.w < 6 || r.h < 3) {
        return;
    }
    const Theme *t = a->theme;
    draw_box(a->screen, r, t, 4, "histogram");
    draw_border_label(a->screen, r.x + 2, r.y + r.h - 1, "dark", NULL, t);
    draw_border_label(a->screen, r.x + r.w - 9, r.y + r.h - 1, "light", NULL, t);
    Rect in = rect_inner(r);
    if (in.w < 1 || in.h < 1) {
        return;
    }

    // Two bars per cell, each summing its share of the 256 luminance levels
    int bars = in.w * 2 > MAX_BARS ? MAX_BARS : in.w * 2;
    double v[MAX_BARS] = {0};
    double peak = 0;
    for (int b = 0; b < bars; b++) {
        int lo = b * 256 / bars, hi = (b + 1) * 256 / bars;
        for (int i = lo; i < hi; i++) {
            v[b] += a->histogram[i];
        }
        peak = fmax(peak, v[b]);
    }

    // Square root keeps small counts visible next to one big spike
    for (int b = 0; b < bars; b++) {
        v[b] = peak > 0 ? sqrt(v[b] / peak) : 0;
    }
    Rect area = {in.x, in.y, bars / 2, in.h};
    draw_braille_graph(a->screen, area, v, t);
}
