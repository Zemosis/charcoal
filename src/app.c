#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SIDE_W 30
#define INFO_H 11
#define CONTROLS_H (CTL_COUNT + 2)
#define MIN_HISTOGRAM_H 5
#define TIMELINE_H 4
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

// The config's panel list follows the Panel enum
_Static_assert(PANEL_COUNT == CONFIG_PANELS, "panels out of step");

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
static int finish_export(App *a);
static void edit_field(char *text, size_t cap, int key);
static void end_export(App *a);
static int prompt_key(App *a, int key);
static void reload_config(App *a);
static void zoom_at(App *a, double factor, int cx, int cy);
static void seek_to_x(App *a, int x);
static void handle_mouse(App *a);

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
    frame_free(a->proxy);
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
    Rect none = {0, 0, 0, 0};

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

    if (a->palette_changed) {
        a->palette_changed = 0;
        a->dirty = 1;
        a->screen->full = 1;
    }

    // Video noise flickers flat areas by a few steps each frame; sending
    // those cells is costly and invisible. Paused frames are exact again.
    a->screen->tolerance = a->playing ? PLAY_TOLERANCE : 0;
    viewer_update(a);
    screen_clear(a->screen, t->main_fg, t->main_bg);

    // Views record where their clickable parts land; hidden ones have none
    a->hit_play = a->hit_bar = a->hit_slower = a->hit_faster = none;
    a->hit_repeat = a->hit_opt_tabs = a->hit_opt_list = none;
    a->hit_menu = a->hit_picker = none;
    a->hit_export_dir = a->hit_export_file = a->hit_export_browse = none;
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
        if (a->playing) {
            // Render and send time of this frame, for the timeline graph
            a->frame_ms[a->frame_ms_head] = now_ms() - now;
            a->frame_ms_head = (a->frame_ms_head + 1) % FRAME_HISTORY;
            if (a->frame_ms_count < FRAME_HISTORY) {
                a->frame_ms_count++;
            }
        }
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

// Zoom by factor, keeping the image point under cell (cx, cy) in place
static void zoom_at(App *a, double factor, int cx, int cy) {
    double old = a->zoom;
    double nz = old * factor;
    nz = nz < 1.0 ? 1.0 : nz > ZOOM_MAX ? ZOOM_MAX : nz;
    if (nz == old) {
        return;
    }
    if (a->art && cx >= a->art_x && cy >= a->art_y &&
        cx < a->art_x + a->art->cols && cy < a->art_y + a->art->rows) {
        double fx = (cx - a->art_x + 0.5) / a->art->cols;
        double fy = (cy - a->art_y + 0.5) / a->art->rows;
        double ix = a->vis_x + fx * a->vis_w, iy = a->vis_y + fy * a->vis_h;
        double nw = a->vis_w * old / nz, nh = a->vis_h * old / nz;
        a->pan_x = ix - (fx - 0.5) * nw;
        a->pan_y = iy - (fy - 0.5) * nh;
    }
    a->zoom = nz;
    a->dirty = 1;
}

// Seek to the point of the timeline bar under column x
static void seek_to_x(App *a, int x) {
    Rect b = a->hit_bar;
    if (b.w < 2 || a->duration_us <= 0) {
        return;
    }
    double f = (double)(x - b.x) / (b.w - 1);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    seek_to(a, (int64_t)(f * a->duration_us));
}

static void handle_mouse(App *a) {
    MouseEvent ev;
    term_mouse(&ev);
    int wheel = ev.button == MOUSE_WHEEL_UP     ? 1
              : ev.button == MOUSE_WHEEL_DOWN ? -1
                                              : 0;
    int click = ev.button == MOUSE_LEFT && ev.press && !ev.motion;
    if (!ev.press) {
        a->drag = DRAG_NONE;
        return;
    }
    if (a->prompting && a->overlay != OVERLAY_PICKER) {
        // Clicks pick a field or browse for the folder
        if (click && rect_contains(a->hit_export_browse, ev.x, ev.y)) {
            export_browse(a);
        } else if (click && rect_contains(a->hit_export_dir, ev.x, ev.y)) {
            a->export_field = 1;
        } else if (click && rect_contains(a->hit_export_file, ev.x, ev.y)) {
            a->export_field = 0;
        }
        return;
    }

    if (a->overlay == OVERLAY_MENU) {
        Rect m = a->hit_menu;
        if (click && rect_contains(rect_inner(m), ev.x, ev.y)) {
            a->menu_sel = ev.y - m.y - 1;
            handle_key(a, KEY_ENTER);
        } else if (click && !rect_contains(m, ev.x, ev.y)) {
            a->overlay = OVERLAY_NONE;
        } else if (wheel) {
            handle_key(a, wheel > 0 ? KEY_UP : KEY_DOWN);
        }
        return;
    }
    if (a->overlay == OVERLAY_HELP) {
        if (wheel) {
            help_key(a, wheel > 0 ? KEY_UP : KEY_DOWN);
        }
        return;
    }
    if (a->overlay == OVERLAY_OPTIONS) {
        Rect tabs = a->hit_opt_tabs, list = a->hit_opt_list;
        if (wheel) {
            options_key(a, wheel > 0 ? KEY_UP : KEY_DOWN);
        } else if (click && rect_contains(tabs, ev.x, ev.y)) {
            options_key(a, '1' + (ev.x - tabs.x) * 4 / tabs.w);
        } else if (click && rect_contains(list, ev.x, ev.y)) {
            // Two lines per option: name, then "← value →"
            int k = (ev.y - list.y) / 2 + a->opt_scroll;
            int on_value = (ev.y - list.y) % 2 == 1;
            int before = a->opt_sel;
            while (a->opt_sel != k) {
                options_key(a, a->opt_sel < k ? KEY_DOWN : KEY_UP);
                if (a->opt_sel == before) {
                    break;      // past the last option
                }
                before = a->opt_sel;
            }
            if (on_value && ev.x <= list.x + 2) {
                options_key(a, KEY_LEFT);
            } else if (on_value && ev.x >= list.x + list.w - 3) {
                options_key(a, KEY_RIGHT);
            }
        }
        return;
    }
    if (a->overlay == OVERLAY_PICKER) {
        Rect in = a->hit_picker;
        if (wheel) {
            for (int i = 0; i < 3; i++) {
                picker_key(a, wheel > 0 ? KEY_UP : KEY_DOWN);
            }
        } else if (click && rect_contains(in, ev.x, ev.y)) {
            int row = ev.y - in.y + a->picker_scroll;
            if (row >= a->picker_count) {
                return;
            }
            double now = now_ms();
            int twice = row == a->last_click_row && now - a->last_click_ms < 400;
            a->picker_sel = row;
            a->last_click_row = row;
            a->last_click_ms = now;
            if (twice && picker_key(a, KEY_ENTER)) {
                a->overlay = OVERLAY_NONE;
                a->picker_folders = 0;
            }
        }
        return;
    }

    // Main screen: timeline buttons and bar
    if (click && rect_contains(a->hit_play, ev.x, ev.y)) {
        handle_key(a, ' ');
        return;
    }
    if (click && rect_contains(a->hit_slower, ev.x, ev.y)) {
        handle_key(a, '<');
        return;
    }
    if (click && rect_contains(a->hit_faster, ev.x, ev.y)) {
        handle_key(a, '>');
        return;
    }
    if (click && rect_contains(a->hit_repeat, ev.x, ev.y)) {
        handle_key(a, 'R');
        return;
    }
    if ((click && rect_contains(a->hit_bar, ev.x, ev.y)) ||
        (ev.motion && a->drag == DRAG_SEEK)) {
        a->drag = DRAG_SEEK;
        seek_to_x(a, ev.x);
        return;
    }

    // Controls: click a row to select it, its arrows to change it, or roll
    // the wheel over it
    Rect in = rect_inner(a->r_controls);
    if (!a->compact && rect_contains(in, ev.x, ev.y)) {
        int c = ev.y - in.y + a->scroll;
        if (c >= CTL_COUNT) {
            return;
        }
        int vx = in.x + 1 + CONTROLS_LABEL_W;
        if (wheel) {
            a->selected = c;
            controls_change(a, c, wheel);
        } else if (click) {
            a->selected = c;
            if (ev.x >= vx && ev.x <= vx + 1) {
                controls_change(a, c, -1);
            } else if (ev.x >= in.x + in.w - 3) {
                controls_change(a, c, 1);
            }
        }
        return;
    }

    // Viewer: the wheel zooms toward the cursor, dragging pans
    Rect view = a->compact ? a->r_viewer : rect_inner(a->r_viewer);
    if (wheel && rect_contains(view, ev.x, ev.y)) {
        zoom_at(a, wheel > 0 ? ZOOM_STEP : 1 / ZOOM_STEP, ev.x, ev.y);
    } else if (click && rect_contains(view, ev.x, ev.y)) {
        a->drag = DRAG_PAN;
        a->drag_x = ev.x;
        a->drag_y = ev.y;
        a->drag_pan_x = a->pan_x;
        a->drag_pan_y = a->pan_y;
    } else if (ev.motion && a->drag == DRAG_PAN && a->art) {
        // The image follows the pointer
        a->pan_x = a->drag_pan_x - (ev.x - a->drag_x) * a->vis_w / a->art->cols;
        a->pan_y = a->drag_pan_y - (ev.y - a->drag_y) * a->vis_h / a->art->rows;
        a->dirty = 1;
    }
}

static void handle_key(App *a, int key) {
    if (key == KEY_MOUSE) {
        handle_mouse(a);
        return;
    }
    if (key == KEY_PALETTE) {
        // The terminal's real colors: 16-color output matches them from now
        int index;
        uint32_t rgb;
        term_palette(&index, &rgb);
        if (index == TERM_COLOR_FG) {
            a->term_fg = rgb;
        } else if (index == TERM_COLOR_BG) {
            a->term_bg = rgb;
        } else {
            color_set_ansi16(index, rgb);
            a->palette_changed = 1;
        }
        return;
    }
    if (key == KEY_TERM_OK) {
        // Smoothed so the info panel does not jitter
        double lag = now_ms() - a->ack_sent_ms;
        a->term_lag_ms = a->term_lag_ms > 0 ? 0.8 * a->term_lag_ms + 0.2 * lag
                                            : lag;
        a->awaiting_ack = 0;
        return;
    }
    if (a->prompting && a->overlay != OVERLAY_PICKER) {
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
            a->picker_folders = 0;
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
    case '5': {
        // Panels shown or hidden are remembered for next time
        int p = key - '1';
        char err[160];
        a->show[p] = !a->show[p];
        a->config.panels[p] = a->show[p];
        if (config_save(&a->config, err, sizeof(err)) != 0) {
            snprintf(a->message, sizeof(a->message), "%s", err);
        }
        layout(a);
        break;
    }
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

    // The folder saved to last time, else where charcoal was started
    char dir[1024];
    path_expand(a->config.export_dir, dir, sizeof(dir));
    if (a->config.export_dir[0] && path_is_dir(dir)) {
        snprintf(a->export_dir, sizeof(a->export_dir), "%s",
                 a->config.export_dir);
    } else {
        char cwd[1024];
        path_abbrev(getcwd(cwd, sizeof(cwd)) ? cwd : ".", a->export_dir,
                    sizeof(a->export_dir));
    }
    a->export_field = 0;

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

// Write the art on screen to folder/name in the format the name asks for;
// returns -1 to keep the dialog open so the name or folder can be fixed
static int finish_export(App *a) {
    const Grid *art = a->export_art;
    if (!art) {
        snprintf(a->message, sizeof(a->message), "nothing to export yet");
        return 0;
    }

    // A name with a path of its own (/x, ~/x) ignores the folder field
    char dir[1024], name[1024], path[2100];
    path_expand(a->export_dir[0] ? a->export_dir : ".", dir, sizeof(dir));
    path_expand(a->prompt, name, sizeof(name));
    if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name);
    } else {
        snprintf(path, sizeof(path), "%s/%s", dir, name);
    }
    char parent[2100], shown[2100];
    path_dir(path, parent, sizeof(parent));
    if (!path_is_dir(parent)) {
        path_abbrev(parent, shown, sizeof(shown));
        snprintf(a->message, sizeof(a->message), "no folder %.200s", shown);
        return -1;
    }

    Buf out;
    buf_init(&out);
    size_t n = strlen(path);
    if (n > 5 && strcmp(path + n - 5, ".html") == 0) {
        // The page looks like the screen: the theme's colors, or the
        // terminal's own where the theme leaves them to the terminal
        uint32_t fg = a->theme->main_fg, bg = a->theme->main_bg;
        if (fg == CELL_COLOR_DEFAULT) {
            fg = a->term_fg;
        }
        if (bg == CELL_COLOR_DEFAULT) {
            bg = a->term_bg;
        }
        export_html(art, &out, fg, bg, a->cell_aspect);
    } else if (n > 4 && strcmp(path + n - 4, ".ans") == 0) {
        export_ansi(art, &out);
    } else if (n > 4 && strcmp(path + n - 4, ".txt") == 0) {
        export_txt(art, &out);
    } else {
        snprintf(a->message, sizeof(a->message), "end the name with .html, .ans "
                 "or .txt");
        buf_free(&out);
        return -1;
    }
    FILE *f = fopen(path, "w");
    int ok = f && fwrite(out.data, 1, out.len, f) == out.len;
    if (f && fclose(f) != 0) {
        ok = 0;
    }
    buf_free(&out);

    // Messages show the full path, ~ for home
    char full[3200];
    if (path[0] != '/') {
        char cwd[1024];
        snprintf(full, sizeof(full), "%s/%s",
                 getcwd(cwd, sizeof(cwd)) ? cwd : ".", path);
    } else {
        snprintf(full, sizeof(full), "%s", path);
    }
    path_abbrev(full, shown, sizeof(shown));
    snprintf(a->message, sizeof(a->message),
             ok ? "saved %.200s" : "cannot write %.200s", shown);
    if (!ok) {
        return -1;
    }

    // Next time starts in this folder
    char saved_dir[1024];
    path_dir(full, parent, sizeof(parent));
    path_abbrev(parent, saved_dir, sizeof(saved_dir));
    if (strcmp(saved_dir, a->config.export_dir) != 0) {
        char err[160];
        snprintf(a->config.export_dir, sizeof(a->config.export_dir), "%s",
                 saved_dir);
        if (config_save(&a->config, err, sizeof(err)) != 0) {
            snprintf(a->message, sizeof(a->message), "%s", err);
        }
    }
    return 0;
}

// Typing, Backspace, Ctrl+U (clear) and Ctrl+W (back one word or folder)
// for a text field
static void edit_field(char *text, size_t cap, int key) {
    size_t len = strlen(text);
    if (key == KEY_CTRL_U) {
        text[0] = '\0';
    } else if (key == KEY_CTRL_W) {
        // Back to the previous '/', as in a shell
        while (len > 0 && text[len - 1] == '/') {
            len--;
        }
        while (len > 0 && text[len - 1] != '/') {
            len--;
        }
        text[len] = '\0';
    } else if (key == KEY_BACKSPACE) {
        // Remove the last whole UTF-8 character
        while (len > 0 && ((unsigned char)text[len - 1] & 0xC0) == 0x80) {
            len--;
        }
        if (len > 0) {
            len--;
        }
        text[len] = '\0';
    } else if (key >= 0x20 && key < KEY_UP && key != 0x7F) {
        Buf b;
        buf_init(&b);
        buf_utf8(&b, (uint32_t)key);
        if (len + b.len < cap) {
            memcpy(text + len, b.data, b.len);
            text[len + b.len] = '\0';
        }
        buf_free(&b);
    }
}

// Keys in the export dialog: Tab switches field, Ctrl+O browses for the
// folder; returns 1 when the key was used
static int prompt_key(App *a, int key) {
    if (key == KEY_ENTER) {
        if (a->prompt[0] && finish_export(a) == 0) {
            end_export(a);
        }
    } else if (key == KEY_ESC || key == KEY_CTRL_C) {
        end_export(a);
    } else if (key == KEY_TAB || key == KEY_BACKTAB || key == KEY_UP ||
               key == KEY_DOWN) {
        a->export_field = !a->export_field;
    } else if (key == KEY_CTRL_O) {
        export_browse(a);
    } else if (a->export_field == 1) {
        edit_field(a->export_dir, sizeof(a->export_dir), key);
    } else {
        edit_field(a->prompt, sizeof(a->prompt), key);
    }
    return 1;
}

void export_browse(App *a) {
    char dir[1024];
    path_expand(a->export_dir, dir, sizeof(dir));
    a->picker_folders = 1;
    picker_open(a, path_is_dir(dir) ? dir : ".");
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
    a->config.mouse = c.mouse;
    term_set_mouse(c.mouse);
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
        a.show[i] = config->panels[i];
    }

    // No path, or a folder without media: start empty on the file picker
    char err[480];
    struct stat st;
    int is_dir = path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    if (path && !is_dir && app_load(&a, path, err, sizeof(err)) != 0) {
        fprintf(stderr, "charcoal: %s\n", err);
        app_free(&a);
        return 1;
    }
    if (picker || !path || is_dir) {
        char dir[4096] = ".";
        if (is_dir) {
            snprintf(dir, sizeof(dir), "%s", path);
        } else if (path) {
            path_dir(path, dir, sizeof(dir));
        }
        picker_open(&a, dir);
    }

    if (term_init() != 0) {
        fprintf(stderr, "charcoal: the viewer needs a terminal\n");
        app_free(&a);
        return 1;
    }
    term_set_mouse(a.config.mouse);
    a.term_fg = a.term_bg = CELL_COLOR_DEFAULT;
    term_query_palette();
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
