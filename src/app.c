#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SIDE_W 30
#define INFO_H 11
#define CONTROLS_H (CTL_COUNT + 2)
#define MIN_HISTOGRAM_H 5
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
static int app_load(App *a, const char *path, char *err, int err_len);
static void open_neighbor(App *a, int dir);
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
static void start_export(App *a);
static void finish_export(App *a);
static void end_export(App *a);
static int prompt_key(App *a, int key);
static void reload_config(App *a);

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// Open path into the app: first frame at full size, a player for anything
// that moves. Loads into temporaries first, so a file that fails to open
// leaves the current one alone. Returns 0, or -1 with the reason in err
static int app_load(App *a, const char *path, char *err, int err_len) {
    char why[400];
    const char *name = path_name(path);
    MediaFile *m = media_open(path, why, sizeof(why));
    if (!m) {
        // media_open starts its message with the path; the name is enough
        size_t plen = strlen(path);
        const char *reason = strncmp(why, path, plen) == 0 && why[plen] == ':'
                                 ? why + plen + 2
                                 : why;
        snprintf(err, (size_t)err_len, "%s: %s", name, reason);
        return -1;
    }
    Frame *frame = media_decode_first(m, 0, 0, why, sizeof(why));
    int src_w, src_h;
    media_size(m, &src_w, &src_h);      // after decoding: EXIF may turn it
    MediaKind kind = media_kind(m);
    int64_t duration = media_duration_us(m);
    double fps = media_fps(m);
    char codec[32];
    snprintf(codec, sizeof(codec), "%s", media_codec_name(m));
    media_close(m);
    if (!frame) {
        snprintf(err, (size_t)err_len, "%s: %s", name, why);
        return -1;
    }
    Player *player = NULL;
    if (kind != MEDIA_STILL) {
        player = player_open(path, a->loop, why, sizeof(why));
        if (!player) {
            snprintf(err, (size_t)err_len, "%s: %s", name, why);
            frame_free(frame);
            return -1;
        }
    }

    // It worked: swap the new file in
    player_close(a->player);
    frame_free(a->frame);
    a->player = player;
    a->frame = frame;
    a->frame_serial++;
    a->frame_drawn = 0;
    a->src_w = src_w;
    a->src_h = src_h;
    a->kind = kind;
    a->duration_us = duration;
    a->fps = fps;
    snprintf(a->codec, sizeof(a->codec), "%s", codec);
    snprintf(a->path, sizeof(a->path), "%s", path);
    snprintf(a->name, sizeof(a->name), "%s", *name ? name : path);
    a->playing = player != NULL;
    a->need_first = player != NULL;
    a->dropped = 0;
    a->want_w = a->want_h = 0;      // the new player needs a size
    a->target_changed = 0;
    a->zoom = 1.0;
    a->pan_x = src_w / 2.0;
    a->pan_y = src_h / 2.0;
    clock_set(a, 0);
    memset(a->pixels_key, -1, sizeof(a->pixels_key));
    a->dirty = 1;

    // Where it sits among its folder's media files, for n / N
    char ignore[4096];
    if (files_neighbor(path, 0, ignore, sizeof(ignore), &a->file_index,
                       &a->file_count) != 0) {
        a->file_index = a->file_count = 0;
    }
    return 0;
}

int app_open_file(App *a, const char *path) {
    char err[480];
    if (app_load(a, path, err, sizeof(err)) != 0) {
        snprintf(a->message, sizeof(a->message), "cannot open %.230s", err);
        return -1;
    }
    a->message[0] = '\0';
    layout(a);      // the timeline comes and goes with the kind
    return 0;
}

// Next or previous media file in the same folder; files that do not open
// are skipped, so one broken file never blocks the rest
static void open_neighbor(App *a, int dir) {
    char cand[4096], next[4096], skipped[256] = "";
    int index, count, tried = 0;
    snprintf(cand, sizeof(cand), "%s", a->path);
    do {
        if (files_neighbor(cand, dir, next, sizeof(next), &index, &count) != 0) {
            snprintf(a->message, sizeof(a->message), "no other files here");
            return;
        }
        if (count == 1) {
            snprintf(a->message, sizeof(a->message), "the only file here");
            return;
        }
        if (strcmp(path_name(next), a->name) == 0) {
            break;      // went all the way round
        }
        char err[480];
        if (app_load(a, next, err, sizeof(err)) == 0) {
            layout(a);
            if (skipped[0]) {
                snprintf(a->message, sizeof(a->message), "skipped %.200s", skipped);
            } else {
                a->message[0] = '\0';
            }
            return;
        }
        if (!skipped[0]) {
            snprintf(skipped, sizeof(skipped), "%.200s", err);
        }
        snprintf(cand, sizeof(cand), "%s", next);
    } while (++tried < count);
    snprintf(a->message, sizeof(a->message), "cannot open %.200s", skipped);
}

static void app_free(App *a) {
    player_close(a->player);
    picker_close(a);
    grid_free(a->export_art);
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
    a->r_timeline = a->r_histogram = none;
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

    int side = a->show[PANEL_INFO] || a->show[PANEL_CONTROLS] ||
               a->show[PANEL_HISTOGRAM];
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

    // Info on top, then controls, then the histogram takes what is left;
    // whichever panel is last stretches to the bottom
    int y = 0;
    int below = a->show[PANEL_CONTROLS] || a->show[PANEL_HISTOGRAM];
    if (a->show[PANEL_INFO]) {
        int h = below ? INFO_H : rows;
        a->r_info = (Rect){side_x, 0, side_w, h};
        y = h;
    }
    if (a->show[PANEL_CONTROLS]) {
        int h = rows - y;
        if (a->show[PANEL_HISTOGRAM] && h - CONTROLS_H >= MIN_HISTOGRAM_H) {
            h = CONTROLS_H;
        }
        a->r_controls = (Rect){side_x, y, side_w, h};
        y += h;
    }
    if (a->show[PANEL_HISTOGRAM] && rows - y >= MIN_HISTOGRAM_H) {
        a->r_histogram = (Rect){side_x, y, side_w, rows - y};
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

    // A frame limit (for slow terminals like tmux): hold new frames back
    if (a->playing && a->max_fps > 0 && !a->frame_drawn &&
        now - a->last_frame_ms < 1000.0 / a->max_fps) {
        a->limited = 1;
        return;
    }

    // Video noise flickers flat areas by a few steps each frame; sending
    // those cells is costly and invisible. Paused frames are exact again.
    a->screen->tolerance = a->playing ? PLAY_TOLERANCE : 0;
    viewer_update(a);
    screen_clear(a->screen, t->main_fg, t->main_bg);
    if (a->overlay == OVERLAY_HELP) {
        view_help(a);
    } else if (a->overlay == OVERLAY_OPTIONS) {
        view_options(a);
    } else if (a->overlay == OVERLAY_MENU) {
        view_menu(a);
    } else if (a->overlay == OVERLAY_PICKER) {
        view_picker(a);
    } else {
        if (a->show[PANEL_VIEWER] || a->compact) {
            view_viewer(a);
        }
        if (!a->compact) {
            view_info(a);
            view_controls(a);
            view_histogram(a);
            view_timeline(a);
        }
        view_status(a);
        if (a->prompting) {
            view_export(a);
        }
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
        a->last_frame_ms = now;
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
    if (a->frame && !a->frame_drawn && !a->limited) {
        a->dropped++;   // replaced before it ever reached the terminal
    }
    a->frame_drawn = 0;
    a->limited = 0;
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
    if (a->prompting) {
        prompt_key(a, key);
        return;
    }
    a->message[0] = '\0';
    if (a->overlay == OVERLAY_MENU) {
        int pick = key == KEY_CTRL_C ? MENU_QUIT : menu_key(a, key);
        if (pick == MENU_CLOSE) {
            a->overlay = OVERLAY_NONE;
        } else if (pick == MENU_OPTIONS) {
            a->overlay = OVERLAY_OPTIONS;
        } else if (pick == MENU_HELP) {
            a->overlay = OVERLAY_HELP;
            a->help_page = 0;
        } else if (pick == MENU_QUIT) {
            a->running = 0;
        }
        return;
    }
    if (a->overlay == OVERLAY_PICKER) {
        if (key == KEY_CTRL_C) {
            a->running = 0;
        } else if (picker_key(a, key)) {
            a->overlay = OVERLAY_NONE;
        }
        return;
    }
    if (a->overlay == OVERLAY_OPTIONS) {
        if (key == KEY_CTRL_C) {
            a->running = 0;
        } else if (options_key(a, key)) {
            a->overlay = OVERLAY_NONE;
        }
        return;
    }
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
    case KEY_ESC:
        // Esc closes whatever is open first; with nothing open, the menu
        a->overlay = OVERLAY_MENU;
        a->menu_sel = 0;
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
        reload_config(a);
        break;
    case '?':
    case KEY_F1:
        a->overlay = OVERLAY_HELP;
        a->help_page = 0;
        break;
    case 'o':
    case KEY_F2:
        a->overlay = OVERLAY_OPTIONS;
        break;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
        a->show[key - '1'] = !a->show[key - '1'];
        layout(a);
        break;
    case 'e':
        start_export(a);
        break;
    case 'n':
        open_neighbor(a, 1);
        break;
    case 'N':
        open_neighbor(a, -1);
        break;
    case 'O': {
        char dir[4096];
        path_dir(a->path, dir, sizeof(dir));
        picker_open(a, dir);
        break;
    }
    case 'b':
    case 'B':
        select_control(a, CTL_BRIGHTNESS, key == 'b' ? 1 : -1);
        break;
    case 't':
    case 'T':
        select_control(a, CTL_CONTRAST, key == 't' ? 1 : -1);
        break;
    case 'g':
    case 'G':
        select_control(a, CTL_GAMMA, key == 'g' ? 1 : -1);
        break;
    case 's':
    case 'S':
        select_control(a, CTL_SATURATION, key == 's' ? 1 : -1);
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

// Ask for a file name, suggesting the image's name as .html
static void start_export(App *a) {
    // Long names are cut so ".html" still fits in the prompt
    char stem[240];
    snprintf(stem, sizeof(stem), "%.*s", (int)sizeof(stem) - 1, a->name);
    char *dot = strrchr(stem, '.');
    if (dot && dot != stem) {
        *dot = '\0';
    }
    snprintf(a->prompt, sizeof(a->prompt), "%s.html", stem);

    // Keep the view as it is now, and hold playback while typing
    grid_free(a->export_art);
    a->export_art = NULL;
    if (a->art) {
        a->export_art = grid_new(a->art->cols, a->art->rows);
        if (a->export_art) {
            memcpy(a->export_art->cells, a->art->cells,
                   sizeof(Cell) * (size_t)a->art->cols * (size_t)a->art->rows);
        }
    }
    a->export_resume = a->playing;
    if (a->playing) {
        clock_set(a, clock_now(a));
        a->playing = 0;
    }
    a->prompting = 1;
}

// Close the dialog, playing on if it was playing before
static void end_export(App *a) {
    a->prompting = 0;
    grid_free(a->export_art);
    a->export_art = NULL;
    if (a->export_resume) {
        clock_set(a, clock_now(a));
        a->playing = 1;
        a->export_resume = 0;
    }
}

// Write the art on screen in the format the file name asks for
static void finish_export(App *a) {
    const char *name = a->prompt;
    size_t n = strlen(name);
    const Grid *art = a->export_art;
    Buf out;
    buf_init(&out);
    if (!art) {
        snprintf(a->message, sizeof(a->message), "nothing to export yet");
        return;
    }
    if (n > 5 && strcmp(name + n - 5, ".html") == 0) {
        export_html(art, &out, a->theme->main_fg, a->theme->main_bg);
    } else if (n > 4 && strcmp(name + n - 4, ".ans") == 0) {
        export_ansi(art, &out);
    } else if (n > 4 && strcmp(name + n - 4, ".txt") == 0) {
        export_txt(art, &out);
    } else {
        snprintf(a->message, sizeof(a->message), "use .html, .ans or .txt");
        buf_free(&out);
        return;
    }
    FILE *f = fopen(name, "w");
    int ok = f && fwrite(out.data, 1, out.len, f) == out.len;
    if (f && fclose(f) != 0) {
        ok = 0;
    }
    buf_free(&out);
    char cwd[200];
    if (name[0] != '/' && getcwd(cwd, sizeof(cwd))) {
        snprintf(a->message, sizeof(a->message),
                 ok ? "saved %s/%s" : "cannot write %s/%s", cwd, name);
    } else {
        snprintf(a->message, sizeof(a->message),
                 ok ? "saved %s" : "cannot write %s", name);
    }
}

// Typing into the export prompt, returns 1 when the key was used
static int prompt_key(App *a, int key) {
    size_t len = strlen(a->prompt);
    if (key == KEY_ENTER) {
        if (len) {
            finish_export(a);
        }
        end_export(a);
    } else if (key == KEY_ESC || key == KEY_CTRL_C) {
        end_export(a);
    } else if (key == KEY_BACKSPACE) {
        // Remove the last whole UTF-8 character
        while (len > 0 && ((unsigned char)a->prompt[len - 1] & 0xC0) == 0x80) {
            len--;
        }
        if (len > 0) {
            len--;
        }
        a->prompt[len] = '\0';
    } else if (key >= 0x20 && key < KEY_UP && key != 0x7F) {
        Buf b;
        buf_init(&b);
        buf_utf8(&b, (uint32_t)key);
        if (len + b.len < sizeof(a->prompt)) {
            memcpy(a->prompt + len, b.data, b.len);
            a->prompt[len + b.len] = '\0';
        }
        buf_free(&b);
    }
    return 1;
}

// Ctrl+R: read the config and theme files again
static void reload_config(App *a) {
    Config c;
    char warn[200];
    int bad = config_load(&c, warn, sizeof(warn)) != 0;
    snprintf(a->config.theme, sizeof(a->config.theme), "%s", c.theme);
    a->config.theme_background = c.theme_background;
    a->config.rounded = c.rounded;
    a->config.vim_keys = a->vim_keys = c.vim_keys;
    a->config.max_fps = a->max_fps = c.max_fps;
    app_apply_theme(a);
    if (bad) {
        snprintf(a->message, sizeof(a->message), "%s", warn);
    } else if (!a->message[0]) {
        snprintf(a->message, sizeof(a->message), "config reloaded");
    }
}

void app_apply_theme(App *a) {
    char warn[160];
    a->theme_count = theme_list(a->theme_names, MAX_THEMES);
    if (theme_load(a->config.theme, &a->theme_data, warn, sizeof(warn)) != 0) {
        snprintf(a->message, sizeof(a->message), "%s", warn);
    }
    if (!a->config.theme_background) {
        a->theme_data.main_bg = CELL_COLOR_DEFAULT;
    }
    a->theme = &a->theme_data;
    draw_set_rounded(a->config.rounded);
    if (a->screen) {
        a->screen->full = 1;
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

int app_run(const char *path, const RenderOpts *opts, FitMode fit, int loop,
            const Config *config, ColorDepth ui_depth, const char *warn,
            int picker) {
    App a;
    memset(&a, 0, sizeof(a));
    memset(a.pixels_key, -1, sizeof(a.pixels_key));
    a.config = *config;
    a.opts = *opts;
    a.start_opts = *opts;
    if (opts->palette != PALETTE_STANDARD && opts->palette != PALETTE_DETAILED &&
        opts->palette != PALETTE_SIMPLE && opts->palette != PALETTE_BLOCKS) {
        a.custom_palette = opts->palette;
    }
    a.fit = fit;
    a.vim_keys = config->vim_keys;
    a.max_fps = config->max_fps;
    a.loop = loop;
    a.speed = 1.0;
    app_apply_theme(&a);
    if (warn && *warn) {
        snprintf(a.message, sizeof(a.message), "%s", warn);
    }
    for (int i = 0; i < PANEL_COUNT; i++) {
        a.show[i] = 1;
    }

    char err[480];
    if (app_load(&a, path, err, sizeof(err)) != 0) {
        fprintf(stderr, "charcoal: %s\n", err);
        app_free(&a);
        return 1;
    }
    if (picker) {
        char dir[4096];
        path_dir(path, dir, sizeof(dir));
        picker_open(&a, dir);
    }

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
        if (a.playing && a.max_fps > 0 && !a.frame_drawn) {
            // A frame held back by the limit goes out when its time comes
            int left = (int)(1000.0 / a.max_fps - (now_ms() - a.last_frame_ms)) + 1;
            left = left < 0 ? 0 : left;
            timeout = timeout < 0 || left < timeout ? left : timeout;
        }
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
