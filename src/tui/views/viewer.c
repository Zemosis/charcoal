#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Stills with more pixels than this get a smaller working copy
#define PROXY_MIN_PIXELS 2000000

// Prototype
static double now_ms(void);
static void update_proxy(App *a);
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

// Keep a copy of a big still at about twice the pixels the whole screen
// can show in braille (2 x 4 per cell); resizing that is much faster than
// resizing the original on every mode or setting change
static void update_proxy(App *a) {
    Frame *f = a->frame;
    int cols = a->screen->cols, rows = a->screen->rows;
    if (a->player || !f || (long)f->width * f->height < PROXY_MIN_PIXELS) {
        frame_free(a->proxy);
        a->proxy = NULL;
        return;
    }
    if (a->proxy && a->proxy_serial == a->frame_serial && cols <= a->proxy_cols &&
        rows <= a->proxy_rows) {
        return;     // still good
    }
    double s = fmax(cols * 2.0 * 2 / f->width, rows * 4.0 * 2 / f->height);
    frame_free(a->proxy);
    a->proxy = NULL;
    if (s > 0.5) {
        return;     // would not be much smaller than the original
    }
    int pw = (int)ceil(f->width * s), ph = (int)ceil(f->height * s);
    Frame *p = frame_new(pw, ph);
    if (!p || resize_rgb(f->rgb, f->width, f->height, p->rgb, pw, ph) != 0) {
        frame_free(p);
        return;
    }
    a->proxy = p;
    a->proxy_serial = a->frame_serial;
    a->proxy_cols = cols;
    a->proxy_rows = rows;
}

void viewer_update(App *a) {
    if (!a->dirty) {
        return;
    }
    a->dirty = 0;
    Rect in = rect_inner(a->r_viewer);
    if (a->compact) {
        in = a->r_viewer;
    }
    if (in.w < 1 || in.h < 1 || a->src_w < 1 || a->src_h < 1) {
        return;     // nothing open yet
    }
    double t0 = now_ms();

    // Scale in cell widths per image pixel; a cell is cell_aspect tall
    double W = a->src_w, H = a->src_h, A = a->cell_aspect;
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
    double vis_w = fmin(cols / kx, W), vis_h = fmin(rows * A / ky, H);
    a->pan_x = fmin(fmax(a->pan_x, vis_w / 2), W - vis_w / 2);
    a->pan_y = fmin(fmax(a->pan_y, vis_h / 2), H - vis_h / 2);
    double vis_x = a->pan_x - vis_w / 2, vis_y = a->pan_y - vis_h / 2;
    a->vis_x = vis_x;
    a->vis_y = vis_y;
    a->vis_w = vis_w;
    a->vis_h = vis_h;

    int sub_w, sub_h;
    mode_subpixels(a->opts.mode, &sub_w, &sub_h);
    int out_w = cols * sub_w, out_h = rows * sub_h;

    // Ask the player for frames just big enough for this view
    if (a->player) {
        double f = fmin(1.0, fmax(out_w / vis_w, out_h / vis_h));
        int want_w = clampi((int)ceil(W * f), 1, (int)W);
        int want_h = clampi((int)ceil(H * f), 1, (int)H);
        if (want_w != a->want_w || want_h != a->want_h) {
            a->want_w = want_w;
            a->want_h = want_h;
            player_set_target(a->player, want_w, want_h);
            a->target_changed = 1;
        }
    }
    if (!a->frame) {
        return;
    }

    // Resize from the smaller copy when it has enough pixels for this view;
    // zoomed far in, only the original does
    update_proxy(a);
    const Frame *src = a->frame;
    int use_proxy = 0;
    if (a->proxy && vis_w * a->proxy->width / W >= out_w &&
        vis_h * a->proxy->height / H >= out_h) {
        src = a->proxy;
        use_proxy = 1;
    }

    // The same region in the source's pixels (video frames are smaller)
    int fw = src->width, fh = src->height;
    double sx = fw / W, sy = fh / H;
    int rx = clampi((int)lround(vis_x * sx), 0, fw - 1);
    int ry = clampi((int)lround(vis_y * sy), 0, fh - 1);
    int rw = clampi((int)lround(vis_w * sx), 1, fw - rx);
    int rh = clampi((int)lround(vis_h * sy), 1, fh - ry);

    size_t bytes = (size_t)out_w * (size_t)out_h * 3;
    if (ensure_buffers(a, bytes) != 0) {
        snprintf(a->message, sizeof(a->message), "out of memory");
        return;
    }

    // Resizing a big image is the slow step, so it is cached per frame
    int key[8] = {rx, ry, rw, rh, out_w, out_h, a->frame_serial, use_proxy};
    if (memcmp(key, a->pixels_key, sizeof(key)) != 0) {
        if (resize_rgb_rect(src->rgb, fw, rx, ry, rw, rh, a->pixels, out_w,
                            out_h) != 0) {
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

    // Brightness spread of exactly what is shown, for the histogram panel
    int counts[256] = {0};
    int npx = out_w * out_h;
    for (int i = 0; i < npx; i++) {
        const uint8_t *p = a->work + (size_t)i * 3;
        counts[rgb_luma(p[0], p[1], p[2])]++;
    }
    for (int i = 0; i < 256; i++) {
        a->histogram[i] = (double)counts[i] / npx;
    }
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
    if (!a->path[0]) {
        // Started without a file: say how to get one
        Rect in = rect_inner(r);
        const char *lines[3] = {"No file open", "O  open a file",
                                "n  the first one here"};
        for (int i = 0; i < 3; i++) {
            int w = text_width(lines[i]);
            int y = in.y + in.h / 2 - 1 + i + (i > 0);
            int x = in.x + (in.w - (i ? 21 : w)) / 2;
            draw_text(a->screen, x, y, in.w, lines[i],
                      i ? t->main_fg : t->title, t->main_bg, i ? 0 : ATTR_BOLD);
            if (i) {
                draw_text(a->screen, x, y, 1, lines[i], t->hi_fg, t->main_bg,
                          ATTR_BOLD);
            }
        }
    }

    // File name at the right of the top border, note at the bottom
    // "name  3/12" when the folder has more to flip through with n / N
    char named[300];
    if (a->file_count > 1) {
        snprintf(named, sizeof(named), "%s  %d/%d", a->name, a->file_index,
                 a->file_count);
    } else {
        snprintf(named, sizeof(named), "%s", a->name);
    }
    int name_w = text_width(named);
    int max_name = r.w - 16;
    if (max_name > 4 && a->name[0]) {
        char shown[320];
        if (name_w > max_name) {
            // Keep the end of long names, where the extension is
            const char *p = named;
            int skip = name_w - (max_name - 1);
            uint32_t cp;
            while (skip-- > 0) {
                p += utf8_decode(p, &cp);
            }
            snprintf(shown, sizeof(shown), "…%s", p);
        } else {
            snprintf(shown, sizeof(shown), "%s", named);
        }
        draw_border_label(a->screen, r.x + r.w - 2 - text_width(shown) - 2, r.y,
                          shown, NULL, t);
    }
    if (a->message[0]) {
        draw_border_label(a->screen, r.x + 2, r.y + r.h - 1, a->message, NULL, t);
    }
}
