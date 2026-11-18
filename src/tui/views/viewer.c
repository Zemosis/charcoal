#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Prototype
static double now_ms(void);
static int ensure_buffers(App *a, size_t bytes);
static int clampi(int v, int lo, int hi);

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int ensure_buffers(App *a, size_t bytes) {
    if (bytes <= a->pixels_cap) {
        return 0;
    }
    uint8_t *p = realloc(a->pixels, bytes);
    if (!p) {
        return -1;
    }
    a->pixels = p;
    p = realloc(a->work, bytes);
    if (!p) {
        return -1;
    }
    a->work = p;
    a->pixels_cap = bytes;
    memset(a->pixels_key, -1, sizeof(a->pixels_key));
    return 0;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void viewer_update(App *a) {
    if (!a->dirty) {
        return;
    }
    a->dirty = 0;
    Rect in = rect_inner(a->r_viewer);
    if (in.w < 1 || in.h < 1) {
        return;
    }
    double t0 = now_ms();

    // Scale in cell widths per image pixel; a cell is cell_aspect tall
    double W = a->frame->width, H = a->frame->height, A = a->cell_aspect;
    double fx = in.w / W, fy = in.h * A / H;
    double kx, ky;
    if (a->fit == FIT_STRETCH) {
        kx = fx;
        ky = fy;
    } else {
        kx = ky = a->fit == FIT_COVER ? fmax(fx, fy) : fmin(fx, fy);
    }
    kx *= a->zoom;
    ky *= a->zoom;

    // Cells used, then the part of the image they show
    int cols = clampi((int)lround(W * kx), 1, in.w);
    int rows = clampi((int)lround(H * ky / A), 1, in.h);
    double src_w = fmin(cols / kx, W), src_h = fmin(rows * A / ky, H);
    a->pan_x = fmin(fmax(a->pan_x, src_w / 2), W - src_w / 2);
    a->pan_y = fmin(fmax(a->pan_y, src_h / 2), H - src_h / 2);
    int rw = clampi((int)lround(src_w), 1, (int)W);
    int rh = clampi((int)lround(src_h), 1, (int)H);
    int rx = clampi((int)lround(a->pan_x - src_w / 2), 0, (int)W - rw);
    int ry = clampi((int)lround(a->pan_y - src_h / 2), 0, (int)H - rh);

    int sub_w, sub_h;
    mode_subpixels(a->opts.mode, &sub_w, &sub_h);
    int out_w = cols * sub_w, out_h = rows * sub_h;
    size_t bytes = (size_t)out_w * (size_t)out_h * 3;
    if (ensure_buffers(a, bytes) != 0) {
        snprintf(a->message, sizeof(a->message), "out of memory");
        return;
    }

    // Resizing the full image is the slow step, so it is cached
    int key[6] = {rx, ry, rw, rh, out_w, out_h};
    if (memcmp(key, a->pixels_key, sizeof(key)) != 0) {
        if (resize_rgb_rect(a->frame->rgb, a->frame->width, rx, ry, rw, rh,
                            a->pixels, out_w, out_h) != 0) {
            snprintf(a->message, sizeof(a->message), "out of memory");
            return;
        }
        memcpy(a->pixels_key, key, sizeof(key));
    }

    if (!a->art || a->art->cols != cols || a->art->rows != rows) {
        grid_free(a->art);
        a->art = grid_new(cols, rows);
        if (!a->art) {
            return;
        }
    }
    memcpy(a->work, a->pixels, bytes);
    a->opts.cell_aspect = A;
    adjust_rgb(a->work, out_w * out_h, &a->opts);
    if (glyph_render(a->work, &a->opts, a->art) != 0) {
        snprintf(a->message, sizeof(a->message), "out of memory");
    }

    a->art_x = in.x + (in.w - cols) / 2;
    a->art_y = in.y + (in.h - rows) / 2;
    a->render_ms = now_ms() - t0;
}

void view_viewer(App *a) {
    Rect r = a->r_viewer;
    if (r.w < 2 || r.h < 2) {
        return;
    }
    const Theme *t = a->theme;
    if (a->compact) {
        // No border when space is tight: the art gets every cell
        if (a->art) {
            draw_grid(a->screen, a->art, a->art_x, a->art_y, t);
        }
        return;
    }
    draw_box(a->screen, r, t, 1, "viewer");
    if (a->art) {
        draw_grid(a->screen, a->art, a->art_x, a->art_y, t);
    }

    // File name at the right of the top border, note at the bottom
    int name_w = text_width(a->name);
    int max_name = r.w - 16;
    if (max_name > 4) {
        char shown[272];
        if (name_w > max_name) {
            // Keep the end of long names, where the extension is
            const char *p = a->name;
            int skip = name_w - (max_name - 1);
            uint32_t cp;
            while (skip-- > 0) {
                p += utf8_decode(p, &cp);
            }
            snprintf(shown, sizeof(shown), "…%s", p);
        } else {
            snprintf(shown, sizeof(shown), "%s", a->name);
        }
        draw_border_label(a->screen, r.x + r.w - 2 - text_width(shown) - 2, r.y,
                          shown, 0, t);
    }
    if (a->message[0]) {
        draw_border_label(a->screen, r.x + 2, r.y + r.h - 1, a->message, 0, t);
    }
}
