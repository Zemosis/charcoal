#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SIDE_W 30
#define INFO_H 11
#define TIMELINE_H 3
#define MIN_COLS 80
#define MIN_ROWS 24
#define ZOOM_STEP 1.25
#define ZOOM_MAX 64.0

// Largest per-channel color change not redrawn during playback
#define PLAY_TOLERANCE 8

// Flow control: after a frame, ask the terminal to report back (ESC [5n)
// and send nothing new until it does, or until this many ms passed
#define ACK_TIMEOUT_MS 250
#define STATUS_REQUEST "\033[5n"

// Playback speeds < and > step through
static const double SPEEDS[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0};
#define SPEED_COUNT ((int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])))

// Prototype
static double now_ms(void);
static int app_load(App *a, const char *path);
static void app_free(App *a);
static void layout(App *a);
static void draw(App *a);
static void pan(App *a, int dx, int dy);
static void reset_view(App *a);
static void select_control(App *a, int control, int dir);
static int64_t clock_now(const App *a);
static void clock_set(App *a, int64_t play_us);
static void show_frame(App *a, Frame *f);
static int advance(App *a);
static void seek_to(App *a, int64_t media_us);
static void step_frame(App *a, int dir);
static void set_speed(App *a, int dir);
static int handle_playback_key(App *a, int key);
static void handle_key(App *a, int key);
static void handle_resize(App *a);

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// Decode the first frame at full size; animations and video get a player
static int app_load(App *a, const char *path) {
    char err[512];
    MediaFile *m = media_open(path, err, sizeof(err));
    if (!m) {
        fprintf(stderr, "charcoal: %s\n", err);
        return -1;
    }
    snprintf(a->codec, sizeof(a->codec), "%s", media_codec_name(m));
    media_size(m, &a->src_w, &a->src_h);
    a->frame = media_decode_first(m, 0, 0, err, sizeof(err));
    a->kind = media_kind(m);
    a->duration_us = media_duration_us(m);
    a->fps = media_fps(m);
    media_close(m);
    if (!a->frame) {
        fprintf(stderr, "charcoal: %s: %s\n", path, err);
        return -1;
    }

    if (a->kind != MEDIA_STILL) {
        a->player = player_open(path, a->loop, err, sizeof(err));
        if (!a->player) {
            fprintf(stderr, "charcoal: %s: %s\n", path, err);
            return -1;
        }
        a->playing = 1;
        a->need_first = 1;
    }

    const char *base = strrchr(path, '/');
    snprintf(a->name, sizeof(a->name), "%s", base && base[1] ? base + 1 : path);
    return 0;
}

static void app_free(App *a) {
    player_close(a->player);
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
    a->r_timeline = none;
    a->compact = cols < MIN_COLS || rows < MIN_ROWS;
    a->dirty = 1;

    // Small terminal: the art and a one-line status bar
    if (a->compact) {
        a->r_viewer = (Rect){0, 0, cols, rows - 1};
        a->r_status = (Rect){0, rows - 1, cols, 1};
        return;
    }

    // The timeline runs along the bottom for anything that moves
    if (a->kind != MEDIA_STILL && a->show[PANEL_TIMELINE]) {
        a->r_timeline = (Rect){0, rows - TIMELINE_H, cols, TIMELINE_H};
        rows -= TIMELINE_H;
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
}

static void draw(App *a) {
    const Theme *t = a->theme;
    double now = now_ms();

    // A terminal that is still busy with the last frame would only queue
    // this one, and the picture would fall behind: wait for its answer
    if (a->awaiting_ack && now - a->ack_sent_ms < ACK_TIMEOUT_MS) {
        return;
    }
    a->awaiting_ack = 0;
    // Video noise flickers flat areas by a few steps each frame; sending
    // those cells is costly and invisible. Paused frames are exact again.
    a->screen->tolerance = a->playing ? PLAY_TOLERANCE : 0;
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
            view_timeline(a);
        }
        view_status(a);
    }
    Buf out;
    buf_init(&out);
    screen_flush(a->screen, &out);
    // More than the empty synchronized-output markers: something changed
    if (out.len > 16) {
        buf_puts(&out, STATUS_REQUEST);
        a->awaiting_ack = 1;
        a->ack_sent_ms = now;
    }
    term_write(&out);
    buf_free(&out);

    if (!a->frame_drawn) {
        a->frame_drawn = 1;
        a->frames_out++;
    }
    if (now - a->fps_window_ms >= 1000.0) {
        a->out_fps = a->frames_out * 1000.0 / (now - a->fps_window_ms);
        a->frames_out = 0;
        a->fps_window_ms = now;
    }
}

// Move the view by a fraction of what is visible
static void pan(App *a, int dx, int dy) {
    a->pan_x += dx * a->src_w / a->zoom / 8;
    a->pan_y += dy * a->src_h / a->zoom / 8;
    a->dirty = 1;
}

static void reset_view(App *a) {
    a->zoom = 1.0;
    a->pan_x = a->src_w / 2.0;
    a->pan_y = a->src_h / 2.0;
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

// Playback time now; it stands still while paused
static int64_t clock_now(const App *a) {
    if (!a->playing) {
        return a->clock_play_us;
    }
    return a->clock_play_us +
           (int64_t)((now_ms() - a->clock_wall_ms) * 1000.0 * a->speed);
}

static void clock_set(App *a, int64_t play_us) {
    a->clock_play_us = play_us;
    a->clock_wall_ms = now_ms();
}

// Put a frame from the player on screen
static void show_frame(App *a, Frame *f) {
    if (a->frame && !a->frame_drawn) {
        a->dropped++;   // replaced before it ever reached the terminal
    }
    a->frame_drawn = 0;
    frame_free(a->frame);
    a->frame = f;
    a->frame_serial++;
    a->dirty = 1;
}

// Take every frame that is due, keep the newest, and say how long until
// the next one (ms), or -1 to wait for the player
static int advance(App *a) {
    if (!a->player) {
        return -1;
    }
    Frame *f;
    int64_t now = clock_now(a);
    while ((f = player_peek(a->player)) &&
           (a->need_first || (a->playing && f->play_us <= now))) {
        f = player_pop(a->player);
        if (a->need_first) {
            // After a seek or at the start the clock follows the frame
            clock_set(a, f->play_us);
            now = f->play_us;
            a->need_first = 0;
        }
        show_frame(a, f);
    }

    if (a->playing && !f && player_at_end(a->player)) {
        // Not looping and out of frames: stop on the last one
        a->playing = 0;
        clock_set(a, a->frame ? a->frame->play_us : 0);
    }
    if (!a->playing || !f) {
        return -1;
    }
    double wait = (f->play_us - now) / 1000.0 / a->speed;
    return wait < 0 ? 0 : (int)ceil(wait);
}

static void seek_to(App *a, int64_t media_us) {
    if (!a->player) {
        return;
    }
    if (a->duration_us > 0 && media_us >= a->duration_us) {
        media_us = a->duration_us - 1;
    }
    player_seek(a->player, media_us < 0 ? 0 : media_us);
    a->need_first = 1;
}

// One frame forward or back, pausing playback
static void step_frame(App *a, int dir) {
    if (!a->player || !a->frame) {
        return;
    }
    a->playing = 0;
    if (dir > 0) {
        Frame *f = player_pop(a->player);
        if (f) {
            clock_set(a, f->play_us);
            show_frame(a, f);
        }
    } else {
        // The frame that covers the moment just before this one
        seek_to(a, a->frame->pts_us - 1);
    }
}

static void set_speed(App *a, int dir) {
    int i = 0;
    while (i < SPEED_COUNT - 1 && SPEEDS[i] < a->speed) {
        i++;
    }
    i += dir;
    i = i < 0 ? 0 : i >= SPEED_COUNT ? SPEED_COUNT - 1 : i;
    clock_set(a, clock_now(a));
    a->speed = SPEEDS[i];
}

// Keys that only mean something while playing, returns 1 when handled
static int handle_playback_key(App *a, int key) {
    if (!a->player) {
        return 0;
    }
    int64_t pos = a->frame ? a->frame->pts_us : 0;
    switch (key) {
    case ' ':
        clock_set(a, clock_now(a));
        a->playing = !a->playing;
        if (a->playing && player_at_end(a->player)) {
            seek_to(a, 0);      // play again from the start
        }
        return 1;
    case '.':
        step_frame(a, 1);
        return 1;
    case ',':
        step_frame(a, -1);
        return 1;
    case '[':
        seek_to(a, pos - 5000000);
        return 1;
    case ']':
        seek_to(a, pos + 5000000);
        return 1;
    case '{':
        seek_to(a, pos - 30000000);
        return 1;
    case '}':
        seek_to(a, pos + 30000000);
        return 1;
    case KEY_HOME:
        seek_to(a, 0);
        return 1;
    case '<':
        set_speed(a, -1);
        return 1;
    case '>':
        set_speed(a, 1);
        return 1;
    case 'R':
        a->loop = !a->loop;
        player_set_loop(a->player, a->loop);
        if (a->loop && !a->playing && player_at_end(a->player)) {
            a->need_first = 1;
        }
        return 1;
    }
    return 0;
}

static void handle_key(App *a, int key) {
    if (key == KEY_TERM_OK) {
        // Smoothed so the info panel does not jitter
        double lag = now_ms() - a->ack_sent_ms;
        a->term_lag_ms = a->term_lag_ms > 0 ? 0.8 * a->term_lag_ms + 0.2 * lag
                                            : lag;
        a->awaiting_ack = 0;
        return;
    }
    a->message[0] = '\0';
    if (a->overlay == OVERLAY_HELP) {
        if (key == KEY_CTRL_C) {
            a->running = 0;
        } else if (help_key(a, key)) {
            a->overlay = OVERLAY_NONE;
        }
        return;
    }
    if (handle_playback_key(a, key)) {
        return;
    }

    int vim = a->vim_keys;
    switch (key) {
    case 'q':
    case KEY_CTRL_C:
        a->running = 0;
        break;
    case KEY_CTRL_Z: {
        // Time spent stopped does not count as playback
        int64_t t = clock_now(a);
        term_suspend();
        a->screen->full = 1;
        a->awaiting_ack = 0;
        clock_set(a, t);
        break;
    }
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
    case '5':
        a->show[key - '1'] = !a->show[key - '1'];
        layout(a);
        break;
    case '4':
        snprintf(a->message, sizeof(a->message), "histogram arrives in M5");
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

int app_run(const char *path, const RenderOpts *opts, ColorDepth ui_depth,
            int loop) {
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
    a.loop = loop;
    a.speed = 1.0;
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

    // Event driven: sleeps until a key, a resize, a signal or a frame is due
    a.running = 1;
    int timeout = -1;
    while (a.running) {
        draw(&a);
        if (a.target_changed) {
            // Paused frames are not replaced on their own: fetch this one
            // again at the new size
            a.target_changed = 0;
            if (a.player && !a.playing && a.frame) {
                seek_to(&a, a.frame->pts_us);
            }
        }
        int ev = term_wait(timeout, a.player ? player_fd(a.player) : -1);
        if (ev & TERM_EV_QUIT) {
            break;
        }
        if (ev & TERM_EV_RESIZE) {
            handle_resize(&a);
        }
        if (ev & TERM_EV_EXTRA) {
            player_drain(a.player);
        }
        int key;
        while (a.running && (key = term_read_key()) != KEY_NONE) {
            handle_key(&a, key);
        }
        timeout = advance(&a);
        if (a.awaiting_ack) {
            // Wake up in time to give up on a terminal that never answers
            int left = (int)(ACK_TIMEOUT_MS - (now_ms() - a.ack_sent_ms)) + 1;
            timeout = timeout < 0 || left < timeout ? (left < 0 ? 0 : left)
                                                    : timeout;
        }
    }

    term_restore();
    app_free(&a);
    return 0;
}
