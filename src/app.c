#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIDE_W 30
#define INFO_H 8
#define MIN_COLS 80
#define MIN_ROWS 24
#define ZOOM_STEP 1.25
#define ZOOM_MAX 64.0

// Prototype
static int app_load(App *a, const char *path);
static void app_free(App *a);
static void layout(App *a);
static void draw(App *a);
static void pan(App *a, int dx, int dy);
static void reset_view(App *a);
static void select_control(App *a, int control, int dir);
static void handle_key(App *a, int key);
static void handle_resize(App *a);

// Decode the file at full resolution, returns 0 or prints why not
static int app_load(App *a, const char *path) {
    char err[512];
    MediaFile *m = media_open(path, err, sizeof(err));
    if (!m) {
        fprintf(stderr, "charcoal: %s\n", err);
        return -1;
    }
    snprintf(a->codec, sizeof(a->codec), "%s", media_codec_name(m));
    a->frame = media_decode_first(m, 0, 0, err, sizeof(err));
    media_close(m);
    if (!a->frame) {
        fprintf(stderr, "charcoal: %s: %s\n", path, err);
        return -1;
    }

    const char *base = strrchr(path, '/');
    snprintf(a->name, sizeof(a->name), "%s", base && base[1] ? base + 1 : path);
    return 0;
}

static void app_free(App *a) {
    frame_free(a->frame);
    grid_free(a->art);
    screen_free(a->screen);
    free(a->pixels);
    free(a->work);
}

// Place the panels for the current screen size
static void layout(App *a) {
    int cols = a->screen->cols, rows = a->screen->rows;
    Rect none = {0, 0, 0, 0};
    a->r_viewer = a->r_info = a->r_controls = a->r_status = none;
    a->compact = cols < MIN_COLS || rows < MIN_ROWS;

    // Small terminal: the art and a one-line status bar
    if (a->compact) {
        a->r_viewer = (Rect){0, 0, cols, rows - 1};
        a->r_status = (Rect){0, rows - 1, cols, 1};
        a->dirty = 1;
        return;
    }

    int side = a->show[PANEL_INFO] || a->show[PANEL_CONTROLS];
    int side_x = 0, side_w = cols;
    if (a->show[PANEL_VIEWER]) {
        int vw = side ? cols - SIDE_W : cols;
        a->r_viewer = (Rect){0, 0, vw, rows};
        side_x = vw;
        side_w = cols - vw;
    }
    if (!side) {
        a->dirty = 1;
        return;
    }

    // Info on top, controls take the rest of the column
    int y = 0;
    if (a->show[PANEL_INFO]) {
        int h = a->show[PANEL_CONTROLS] ? INFO_H : rows;
        a->r_info = (Rect){side_x, 0, side_w, h};
        y = h;
    }
    if (a->show[PANEL_CONTROLS]) {
        a->r_controls = (Rect){side_x, y, side_w, rows - y};
    }
    a->dirty = 1;
}

static void draw(App *a) {
    const Theme *t = a->theme;
    viewer_update(a);
    screen_clear(a->screen, t->main_fg, t->main_bg);
    if (a->overlay == OVERLAY_HELP) {
        view_help(a);
    } else {
        if (a->show[PANEL_VIEWER] || a->compact) {
            view_viewer(a);
        }
        if (!a->compact) {
            view_info(a);
            view_controls(a);
        }
        view_status(a);
    }
    Buf out;
    buf_init(&out);
    screen_flush(a->screen, &out);
    term_write(&out);
    buf_free(&out);
}

// Move the view by a fraction of what is visible
static void pan(App *a, int dx, int dy) {
    double step_x = a->frame->width / a->zoom / 8;
    double step_y = a->frame->height / a->zoom / 8;
    a->pan_x += dx * step_x;
    a->pan_y += dy * step_y;
    a->dirty = 1;
}

static void reset_view(App *a) {
    a->zoom = 1.0;
    a->pan_x = a->frame->width / 2.0;
    a->pan_y = a->frame->height / 2.0;
    a->opts.brightness = a->start_opts.brightness;
    a->opts.contrast = a->start_opts.contrast;
    a->opts.gamma = a->start_opts.gamma;
    a->opts.saturation = a->start_opts.saturation;
    a->dirty = 1;
}

// Hotkeys change their control and move the cursor to it
static void select_control(App *a, int control, int dir) {
    a->selected = control;
    controls_change(a, control, dir);
}

static void handle_key(App *a, int key) {
    a->message[0] = '\0';
    if (a->overlay == OVERLAY_HELP) {
        if (key == KEY_CTRL_C) {
            a->running = 0;
        } else if (help_key(a, key)) {
            a->overlay = OVERLAY_NONE;
        }
        return;
    }

    int vim = a->vim_keys;
    switch (key) {
    case 'q':
    case KEY_CTRL_C:
        a->running = 0;
        break;
    case KEY_CTRL_Z:
        term_suspend();
        a->screen->full = 1;
        break;
    case KEY_CTRL_R:
        a->screen->full = 1;
        break;
    case '?':
    case KEY_F1:
        a->overlay = OVERLAY_HELP;
        a->help_page = 0;
        break;
    case 'o':
    case KEY_F2:
        snprintf(a->message, sizeof(a->message), "options arrive in M5");
        break;
    case '1':
    case '2':
    case '3':
        a->show[key - '1'] = !a->show[key - '1'];
        layout(a);
        break;
    case KEY_UP:
        a->selected = (a->selected + CTL_COUNT - 1) % CTL_COUNT;
        break;
    case KEY_DOWN:
        a->selected = (a->selected + 1) % CTL_COUNT;
        break;
    case KEY_LEFT:
        controls_change(a, a->selected, -1);
        break;
    case KEY_RIGHT:
        controls_change(a, a->selected, 1);
        break;
    case 'm':
    case 'M':
        select_control(a, CTL_MODE, key == 'm' ? 1 : -1);
        break;
    case 'p':
    case 'P':
        select_control(a, CTL_PALETTE, key == 'p' ? 1 : -1);
        break;
    case 'c':
    case 'C':
        select_control(a, CTL_COLOR, key == 'c' ? 1 : -1);
        break;
    case 'd':
        select_control(a, CTL_DITHER, 1);
        break;
    case 'i':
        select_control(a, CTL_INVERT, 1);
        break;
    case 'f':
        select_control(a, CTL_FIT, 1);
        break;
    case '+':
    case '=':
        a->zoom = a->zoom * ZOOM_STEP > ZOOM_MAX ? ZOOM_MAX : a->zoom * ZOOM_STEP;
        a->dirty = 1;
        break;
    case '-':
        a->zoom = a->zoom / ZOOM_STEP < 1.0 ? 1.0 : a->zoom / ZOOM_STEP;
        a->dirty = 1;
        break;
    case KEY_SHIFT_LEFT:
        pan(a, -1, 0);
        break;
    case KEY_SHIFT_RIGHT:
        pan(a, 1, 0);
        break;
    case KEY_SHIFT_UP:
        pan(a, 0, -1);
        break;
    case KEY_SHIFT_DOWN:
        pan(a, 0, 1);
        break;
    case 'r':
        reset_view(a);
        break;
    default:
        // Vim keys: hjkl work the controls, HJKL pan; otherwise h is help
        if (vim && key == 'k') {
            handle_key(a, KEY_UP);
        } else if (vim && key == 'j') {
            handle_key(a, KEY_DOWN);
        } else if (vim && key == 'h') {
            handle_key(a, KEY_LEFT);
        } else if (vim && key == 'l') {
            handle_key(a, KEY_RIGHT);
        } else if (vim && key == 'H') {
            pan(a, -1, 0);
        } else if (vim && key == 'L') {
            pan(a, 1, 0);
        } else if (vim && key == 'K') {
            pan(a, 0, -1);
        } else if (vim && key == 'J') {
            pan(a, 0, 1);
        } else if (!vim && key == 'h') {
            handle_key(a, '?');
        }
        break;
    }
}

static void handle_resize(App *a) {
    int cols, rows;
    term_size(&cols, &rows, &a->cell_aspect);
    if (cols != a->screen->cols || rows != a->screen->rows) {
        screen_resize(a->screen, cols, rows);
    }
    a->screen->full = 1;
    layout(a);
}

int app_run(const char *path, const RenderOpts *opts, ColorDepth ui_depth) {
    App a;
    memset(&a, 0, sizeof(a));
    memset(a.pixels_key, -1, sizeof(a.pixels_key));
    a.opts = *opts;
    a.start_opts = *opts;
    if (opts->palette != PALETTE_STANDARD && opts->palette != PALETTE_DETAILED &&
        opts->palette != PALETTE_SIMPLE && opts->palette != PALETTE_BLOCKS) {
        a.custom_palette = opts->palette;
    }
    a.fit = FIT_CONTAIN;
    a.vim_keys = 1;
    a.theme = theme_default();
    for (int i = 0; i < PANEL_COUNT; i++) {
        a.show[i] = 1;
    }

    if (app_load(&a, path) != 0) {
        app_free(&a);
        return 1;
    }
    reset_view(&a);

    if (term_init() != 0) {
        fprintf(stderr, "charcoal: the viewer needs a terminal\n");
        app_free(&a);
        return 1;
    }
    int cols, rows;
    term_size(&cols, &rows, &a.cell_aspect);
    a.screen = screen_new(cols, rows, ui_depth);
    if (!a.screen) {
        term_restore();
        fprintf(stderr, "charcoal: out of memory\n");
        app_free(&a);
        return 1;
    }
    layout(&a);

    // Event driven: nothing runs until a key, a resize or a signal
    a.running = 1;
    while (a.running) {
        draw(&a);
        int ev = term_wait(-1);
        if (ev & TERM_EV_QUIT) {
            break;
        }
        if (ev & TERM_EV_RESIZE) {
            handle_resize(&a);
        }
        int key;
        while (a.running && (key = term_read_key()) != KEY_NONE) {
            handle_key(&a, key);
        }
    }

    term_restore();
    app_free(&a);
    return 0;
}
