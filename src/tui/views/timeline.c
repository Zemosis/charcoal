#include "app.h"

#include <stdio.h>

#define CH_PLAY  0x25B6     // ▶
#define CH_PAUSE 0x2016     // ‖
#define CH_DONE  0x2501     // ━
#define CH_LEFT  0x2500     // ─
#define CH_KNOB  0x25CF     // ●

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
    snprintf(label, sizeof(label), "<%.2f×>", a->speed);
    int lw = text_width(label) + 2;
    const char *loop = a->loop ? "Repeat on" : "Repeat off";
    int rw = text_width(loop) + 2;
    draw_border_label(s, r.x + r.w - 2 - rw, r.y, loop, 'R', t);
    draw_border_label(s, r.x + r.w - 3 - rw - lw, r.y, label, 0, t);

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
    x += 2;
    x += draw_text(s, x, y, in.w, now, t->main_fg, t->main_bg, 0) + 1;
    int right = in.x + in.w - 1 - text_width(total);
    draw_text(s, right, y, text_width(total), total, t->main_fg, t->main_bg, 0);

    int bar_w = right - 1 - x;
    if (bar_w < 2) {
        return;
    }
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
