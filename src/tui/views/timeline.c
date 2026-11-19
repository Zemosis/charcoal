#include "app.h"

#include <stdio.h>

#define CH_PLAY  0x25B6     // ▶
#define CH_PAUSE 0x2016     // ‖
#define CH_DONE  0x2501     // ━
#define CH_LEFT  0x2500     // ─
#define CH_KNOB  0x25CF     // ●
#define MAX_CELLS 128

// Braille dots from the bottom up, left and right column
static const uint8_t DOTS_LEFT[4] = {0x40, 0x04, 0x02, 0x01};
static const uint8_t DOTS_RIGHT[4] = {0x80, 0x20, 0x10, 0x08};

// Prototype
static void frame_graph(App *a, int x, int y, int w);

// One row of braille bars: how long the latest frames took against the
// frame budget, green under half, yellow under 80%, red above, like btop
static void frame_graph(App *a, int x, int y, int w) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    double fps = a->fps > 0 ? a->fps : 30;
    double budget = 1000.0 / (fps * a->speed);
    char text[48];
    double last = a->frame_ms_count
                      ? a->frame_ms[(a->frame_ms_head + FRAME_HISTORY - 1) %
                                    FRAME_HISTORY]
                      : 0;
    snprintf(text, sizeof(text), "%.1f of %.0f ms", last, budget);
    int tw = text_width(text);
    x += draw_text(s, x, y, 6, "frame ", t->inactive_fg, t->main_bg, 0);
    int cells = w - 6 - tw - 1;
    if (cells > MAX_CELLS) {
        cells = MAX_CELLS;
    }
    draw_text(s, x + cells + 1, y, tw, text, t->main_fg, t->main_bg, 0);
    if (cells < 1) {
        return;
    }

    // The newest frame is at the right end
    for (int c = 0; c < cells; c++) {
        uint8_t bits = 0;
        double worst = 0;
        for (int half = 0; half < 2; half++) {
            int back = (cells - 1 - c) * 2 + (1 - half);   // frames ago
            if (back >= a->frame_ms_count) {
                continue;
            }
            double v = a->frame_ms[(a->frame_ms_head + FRAME_HISTORY - 1 - back) %
                                   FRAME_HISTORY] / budget;
            worst = v > worst ? v : worst;
            int dots = v <= 0 ? 0 : v >= 1 ? 4 : 1 + (int)(v * 3.999);
            for (int k = 0; k < dots; k++) {
                bits |= half ? DOTS_RIGHT[k] : DOTS_LEFT[k];
            }
        }
        if (!bits) {
            continue;   // no frames that far back yet
        }
        uint32_t fg = worst < 0.5 ? t->graph_low
                    : worst < 0.8 ? t->graph_mid
                                  : t->graph_high;
        screen_put(s, x + c, y, 0x2800 + bits, fg, t->main_bg, 0);
    }
}

void format_time(int64_t us, int tenths, char *out, size_t n) {
    if (us < 0) {
        us = 0;
    }
    int64_t s = us / 1000000;
    int h = (int)(s / 3600), m = (int)(s / 60 % 60), sec = (int)(s % 60);
    if (h > 0) {
        snprintf(out, n, "%d:%02d:%02d", h, m, sec);
    } else if (tenths) {
        snprintf(out, n, "%d:%02d.%d", m, sec, (int)(us / 100000 % 10));
    } else {
        snprintf(out, n, "%d:%02d", m, sec);
    }
}

void view_timeline(App *a) {
    Rect r = a->r_timeline;
    if (r.w < 20 || r.h < 3) {
        return;
    }
    const Theme *t = a->theme;
    Screen *s = a->screen;
    draw_box(s, r, t, 5, "timeline");

    // Speed and loop as buttons in the top border, btop style
    char label[32];
    snprintf(label, sizeof(label), "<speed %.2f×>", a->speed);
    int lw = text_width(label) + 2;
    const char *loop = a->loop ? "Repeat on" : "Repeat off";
    int rw = text_width(loop) + 2;
    int rx = r.x + r.w - 2 - rw, lx = r.x + r.w - 3 - rw - lw;
    draw_border_label(s, rx, r.y, loop, "R", t);
    draw_border_label(s, lx, r.y, label, "<>", t);
    a->hit_repeat = (Rect){rx, r.y, rw, 1};
    a->hit_slower = (Rect){lx + 1, r.y, 1, 1};
    a->hit_faster = (Rect){lx + lw - 2, r.y, 1, 1};

    // ▶ 0:12.3 ━━━━━━━━━●─────────── 1:47.0
    Rect in = rect_inner(r);
    int64_t pos = a->frame ? a->frame->pts_us : 0;
    int tenths = a->duration_us > 0 && a->duration_us < 60000000;
    char now[24], total[24];
    format_time(pos, tenths, now, sizeof(now));
    if (a->duration_us > 0) {
        format_time(a->duration_us, tenths, total, sizeof(total));
    } else {
        snprintf(total, sizeof(total), "--:--");
    }

    int x = in.x + 1, y = in.y;
    screen_put(s, x, y, a->playing ? CH_PLAY : CH_PAUSE, t->hi_fg, t->main_bg,
               ATTR_BOLD);
    a->hit_play = (Rect){x, y, 1, 1};
    x += 2;
    x += draw_text(s, x, y, in.w, now, t->main_fg, t->main_bg, 0) + 1;
    int right = in.x + in.w - 1 - text_width(total);
    draw_text(s, right, y, text_width(total), total, t->main_fg, t->main_bg, 0);

    int bar_w = right - 1 - x;
    if (bar_w < 2) {
        return;
    }
    if (in.h >= 2) {
        frame_graph(a, in.x + 1, in.y + 1, in.w - 2);
    }
    a->hit_bar = (Rect){x, y, bar_w, 1};
    int done = 0;
    if (a->duration_us > 0) {
        done = (int)((double)pos / a->duration_us * (bar_w - 1) + 0.5);
        done = done < 0 ? 0 : done > bar_w - 1 ? bar_w - 1 : done;
    }
    for (int i = 0; i < bar_w; i++) {
        uint32_t ch = i < done ? CH_DONE : i == done ? CH_KNOB : CH_LEFT;
        uint32_t fg = i < done ? t->main_fg : i == done ? t->hi_fg
                                                        : t->inactive_fg;
        screen_put(s, x + i, y, ch, fg, t->main_bg, 0);
    }
}
