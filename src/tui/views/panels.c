#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LABEL_W CONTROLS_LABEL_W

// Built-in palettes in the order p cycles through them
static const char *const PALETTES[] = {
    PALETTE_STANDARD, PALETTE_DETAILED, PALETTE_SIMPLE, PALETTE_BLOCKS,
};
static const char *const PALETTE_NAMES[] = {
    "standard", "detailed", "simple", "blocks",
};
#define PALETTE_COUNT 4

static const char *const CONTROL_LABELS[CTL_COUNT] = {
    "mode", "palette", "color", "dither", "invert", "fit",
    "brightness", "contrast", "gamma", "saturation",
};

// Where each label's hotkey letter is: m p c d i f b t g s
static const int HOTKEY_POS[CTL_COUNT] = {0, 0, 0, 0, 0, 0, 0, 3, 0, 0};

// Prototype
static int palette_index(const App *a);
static double step_value(double v, int dir, double step, double lo, double hi);
static void control_value(const App *a, int control, char *out, size_t n);
static void info_row(App *a, Rect in, int row, const char *label,
                     const char *value);

// Index into PALETTES, or PALETTE_COUNT for a custom ramp
static int palette_index(const App *a) {
    for (int i = 0; i < PALETTE_COUNT; i++) {
        if (a->opts.palette == PALETTES[i]) {
            return i;
        }
    }
    return PALETTE_COUNT;
}

// Add one step and snap to the step grid, so values never drift
static double step_value(double v, int dir, double step, double lo, double hi) {
    v = round((v + dir * step) / step) * step;
    return v < lo ? lo : v > hi ? hi : v;
}

void controls_change(App *a, int control, int dir) {
    RenderOpts *o = &a->opts;
    switch (control) {
    case CTL_MODE:
        o->mode = (GlyphMode)((o->mode + dir + MODE_COUNT) % MODE_COUNT);
        break;
    case CTL_PALETTE: {
        // The custom ramp from the command line joins the cycle
        int n = a->custom_palette ? PALETTE_COUNT + 1 : PALETTE_COUNT;
        int i = (palette_index(a) + dir + n) % n;
        o->palette = i < PALETTE_COUNT ? PALETTES[i] : a->custom_palette;
        break;
    }
    case CTL_COLOR:
        o->color = (ColorDepth)((o->color + dir + COLOR_COUNT) % COLOR_COUNT);
        break;
    case CTL_DITHER:
        o->dither = (DitherMode)((o->dither + dir + DITHER_COUNT) % DITHER_COUNT);
        break;
    case CTL_INVERT:
        o->invert = !o->invert;
        break;
    case CTL_FIT:
        a->fit = (FitMode)((a->fit + dir + FIT_COUNT) % FIT_COUNT);
        break;
    case CTL_BRIGHTNESS:
        o->brightness = step_value(o->brightness, dir, 0.05, -1, 1);
        break;
    case CTL_CONTRAST:
        o->contrast = step_value(o->contrast, dir, 0.1, 0, 4);
        break;
    case CTL_GAMMA:
        o->gamma = step_value(o->gamma, dir, 0.1, 0.1, 5);
        break;
    case CTL_SATURATION:
        o->saturation = step_value(o->saturation, dir, 0.1, 0, 4);
        break;
    }
    a->dirty = 1;
}

static void control_value(const App *a, int control, char *out, size_t n) {
    const RenderOpts *o = &a->opts;
    switch (control) {
    case CTL_MODE:
        snprintf(out, n, "%s", glyph_mode_name(o->mode));
        break;
    case CTL_PALETTE: {
        int i = palette_index(a);
        snprintf(out, n, "%s", i < PALETTE_COUNT ? PALETTE_NAMES[i] : "custom");
        break;
    }
    case CTL_COLOR:
        snprintf(out, n, "%s", color_depth_name(o->color));
        break;
    case CTL_DITHER:
        snprintf(out, n, "%s", dither_mode_name(o->dither));
        break;
    case CTL_INVERT:
        snprintf(out, n, "%s", o->invert ? "on" : "off");
        break;
    case CTL_FIT:
        snprintf(out, n, "%s", fit_name(a->fit));
        break;
    case CTL_BRIGHTNESS:
        snprintf(out, n, "%+.2f", o->brightness);
        break;
    case CTL_CONTRAST:
        snprintf(out, n, "%.2f", o->contrast);
        break;
    case CTL_GAMMA:
        snprintf(out, n, "%.2f", o->gamma);
        break;
    case CTL_SATURATION:
        snprintf(out, n, "%.2f", o->saturation);
        break;
    default:
        out[0] = '\0';
    }
}

static void info_row(App *a, Rect in, int row, const char *label,
                     const char *value) {
    if (row >= in.h) {
        return;
    }
    const Theme *t = a->theme;
    draw_text(a->screen, in.x + 1, in.y + row, in.w - 1, label, t->inactive_fg,
              t->main_bg, 0);
    draw_text(a->screen, in.x + 9, in.y + row, in.w - 10, value, t->main_fg,
              t->main_bg, 0);
}

void view_info(App *a) {
    Rect r = a->r_info;
    if (r.w < 4 || r.h < 3) {
        return;
    }
    draw_box(a->screen, r, a->theme, 2, "info");
    Rect in = rect_inner(r);
    char v[64];
    info_row(a, in, 0, "file", a->name);
    info_row(a, in, 1, "codec", a->codec);
    if (a->kind == MEDIA_VIDEO && a->fps > 0) {
        snprintf(v, sizeof(v), "video · %.4g fps", a->fps);
    } else {
        snprintf(v, sizeof(v), "%s", a->kind == MEDIA_STILL      ? "still"
                                     : a->kind == MEDIA_ANIMATED ? "animated"
                                                                 : "video");
    }
    info_row(a, in, 2, "kind", v);
    snprintf(v, sizeof(v), "%d×%d", a->src_w, a->src_h);
    info_row(a, in, 3, "size", v);
    if (a->art) {
        snprintf(v, sizeof(v), "%d×%d cells", a->art->cols, a->art->rows);
        info_row(a, in, 4, "view", v);
    }
    snprintf(v, sizeof(v), "%.2f×", a->zoom);
    info_row(a, in, 5, "zoom", v);
    snprintf(v, sizeof(v), "%.1f ms", a->render_ms);
    info_row(a, in, 6, "render", v);
    if (a->kind != MEDIA_STILL) {
        snprintf(v, sizeof(v), "%d", a->dropped);
        info_row(a, in, 7, "dropped", v);
        snprintf(v, sizeof(v), "%.0f fps \u00b7 %.0f ms", a->out_fps,
                 a->term_lag_ms);
        info_row(a, in, 8, "output", v);
    }
}

void view_controls(App *a) {
    Rect r = a->r_controls;
    if (r.w < 4 || r.h < 3) {
        return;
    }
    const Theme *t = a->theme;
    draw_box(a->screen, r, t, 3, "controls");
    Rect in = rect_inner(r);

    // Keep the selected row in view
    if (a->selected < a->scroll) {
        a->scroll = a->selected;
    }
    if (a->selected >= a->scroll + in.h) {
        a->scroll = a->selected - in.h + 1;
    }

    for (int row = 0; row < in.h; row++) {
        int c = a->scroll + row;
        if (c >= CTL_COUNT) {
            break;
        }
        int y = in.y + row;
        int sel = c == a->selected;
        uint32_t fg = sel ? t->selected_fg : t->main_fg;
        uint32_t label_fg = sel ? t->selected_fg : t->inactive_fg;
        uint32_t bg = sel ? t->selected_bg : t->main_bg;
        uint8_t attr = sel ? ATTR_BOLD : 0;
        if (sel && a->screen->depth == COLOR_MONO) {
            attr |= ATTR_REVERSE;
        }
        draw_fill(a->screen, (Rect){in.x, y, in.w, 1}, fg, bg);

        // Every row has a hotkey; its letter is lit in the label
        const char *label = CONTROL_LABELS[c];
        int x = in.x + 1;
        int hk = HOTKEY_POS[c];
        draw_text(a->screen, x, y, LABEL_W, label, label_fg, bg, attr);
        char letter[2] = {label[hk], '\0'};
        draw_text(a->screen, x + hk, y, 1, letter, sel ? fg : t->hi_fg, bg,
                  attr | ATTR_BOLD);

        char value[32];
        control_value(a, c, value, sizeof(value));
        int vx = in.x + 1 + LABEL_W;
        draw_text(a->screen, vx + 2, y, in.w - LABEL_W - 5, value, fg, bg, attr);
        if (sel) {
            screen_put(a->screen, vx, y, 0x2190, fg, bg, attr);
            screen_put(a->screen, in.x + in.w - 2, y, 0x2192, fg, bg, attr);
        }
    }
}

void view_status(App *a) {
    Rect r = a->r_status;
    if (r.w < 1 || r.h < 1) {
        return;
    }
    const Theme *t = a->theme;
    char line[160];
    char value[32];
    control_value(a, a->selected, value, sizeof(value));
    snprintf(line, sizeof(line), " %s · %s · %s · %.2f× · %s %s",
             glyph_mode_name(a->opts.mode), color_depth_name(a->opts.color),
             dither_mode_name(a->opts.dither), a->zoom,
             CONTROL_LABELS[a->selected], value);
    draw_fill(a->screen, r, t->main_fg, t->main_bg);
    int n = draw_text(a->screen, r.x, r.y, r.w, line, t->inactive_fg,
                      t->main_bg, 0);
    const char *hint = a->message[0] ? a->message : "? help";
    int hw = text_width(hint);
    if (r.w - n > hw + 1) {
        draw_text(a->screen, r.x + r.w - hw - 1, r.y, hw, hint, t->hi_fg,
                  t->main_bg, 0);
    }
}
