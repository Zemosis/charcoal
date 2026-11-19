#include "app.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MENU_W 24
#define DIALOG_H 7

static const char *const MENU_ITEMS[MENU_COUNT] = {"Options", "Help", "Quit"};

// Prototype
static void draw_hint(Screen *s, int x, int y, const Theme *t);
static void draw_field(Screen *s, int x, int y, int w, const char *text,
                       int active, const Theme *t);

void view_menu(App *a) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    int with_logo = s->rows >= 16 && s->cols >= 44;
    int h = MENU_COUNT + 2;
    int y0 = with_logo ? (s->rows - h - 5) / 2 + 5 : (s->rows - h) / 2;
    if (with_logo) {
        view_logo(a, y0 - 5);
    }

    Rect box = {(s->cols - MENU_W) / 2, y0, MENU_W, h};
    draw_fill(s, box, t->main_fg, t->main_bg);
    draw_box(s, box, t, -1, "menu");
    a->hit_menu = box;
    for (int i = 0; i < MENU_COUNT; i++) {
        int sel = i == a->menu_sel;
        uint32_t fg = sel ? t->selected_fg : t->main_fg;
        uint32_t bg = sel ? t->selected_bg : t->main_bg;
        uint8_t attr = sel && s->depth == COLOR_MONO ? ATTR_REVERSE : 0;
        int y = box.y + 1 + i;
        draw_fill(s, (Rect){box.x + 1, y, box.w - 2, 1}, fg, bg);
        int w = text_width(MENU_ITEMS[i]);
        int x = box.x + (box.w - w) / 2;
        draw_text(s, x, y, w, MENU_ITEMS[i], fg, bg, attr | ATTR_BOLD);
        // The first letter is the key
        draw_text(s, x, y, 1, MENU_ITEMS[i], sel ? fg : t->hi_fg, bg,
                  attr | ATTR_BOLD);
    }
    draw_border_label(s, box.x + 2, box.y + box.h - 1, "Esc", NULL, t);
}

int menu_key(App *a, int key) {
    switch (key) {
    case KEY_ESC:
        return MENU_CLOSE;
    case KEY_UP:
    case 'k':
        a->menu_sel = (a->menu_sel + MENU_COUNT - 1) % MENU_COUNT;
        return -1;
    case KEY_DOWN:
    case 'j':
    case KEY_TAB:
        a->menu_sel = (a->menu_sel + 1) % MENU_COUNT;
        return -1;
    case KEY_ENTER:
    case ' ':
        return a->menu_sel;
    case 'o':
    case 'O':
        return MENU_OPTIONS;
    case 'h':
    case 'H':
    case '?':
        return MENU_HELP;
    case 'q':
    case 'Q':
        return MENU_QUIT;
    }
    return -1;
}

// "Enter save · Esc cancel" with the keys lit
static void draw_hint(Screen *s, int x, int y, const Theme *t) {
    x += draw_text(s, x, y, 5, "Enter", t->hi_fg, t->main_bg, ATTR_BOLD);
    x += draw_text(s, x, y, 10, " save · ", t->inactive_fg, t->main_bg, 0);
    x += draw_text(s, x, y, 3, "Esc", t->hi_fg, t->main_bg, ATTR_BOLD);
    draw_text(s, x, y, 7, " cancel", t->inactive_fg, t->main_bg, 0);
}

// A text field; the active one is a solid bar with a cursor, keeping the
// end of long text in view
static void draw_field(Screen *s, int x, int y, int w, const char *text,
                       int active, const Theme *t) {
    uint32_t fg = active ? t->selected_fg : t->main_fg;
    uint32_t bg = active ? t->selected_bg : t->main_bg;
    draw_fill(s, (Rect){x, y, w, 1}, fg, bg);
    const char *shown = text;
    int over = text_width(shown) + (active ? 1 : 0) - w;
    uint32_t cp;
    while (over-- > 0 && *shown) {
        shown += utf8_decode(shown, &cp);
    }
    int n = draw_text(s, x, y, w, shown, fg, bg, 0);
    if (active && n < w) {
        screen_put(s, x + n, y, 0x2588, fg, bg, 0);
    }
}

void view_export(App *a) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    int w = s->cols - 4 < 72 ? s->cols - 4 : 72;
    if (w < 36 || s->rows < DIALOG_H) {
        // Too small for a box: the status line is all there is
        char line[1400];
        snprintf(line, sizeof(line), "export to: %s/%s█", a->export_dir,
                 a->prompt);
        draw_fill(s, (Rect){0, s->rows - 1, s->cols, 1}, t->main_fg,
                  t->selected_bg);
        draw_text(s, 0, s->rows - 1, s->cols, line, t->selected_fg,
                  t->selected_bg, 0);
        return;
    }
    Rect box = {(s->cols - w) / 2, (s->rows - DIALOG_H) / 2, w, DIALOG_H};
    draw_fill(s, box, t->main_fg, t->main_bg);
    draw_box(s, box, t, -1, "export");
    Rect in = rect_inner(box);
    int lx = in.x + 2, vx = in.x + 10, vw = in.w - 12;
    int on_dir = a->export_field == 1;

    // Folder, with a browse button; then the file name
    const char *browse = "[browse]";
    int bw = text_width(browse), fw = vw - bw - 1;
    draw_text(s, lx, in.y + 1, 7, "Folder", on_dir ? t->hi_fg : t->inactive_fg,
              t->main_bg, on_dir ? ATTR_BOLD : 0);
    draw_field(s, vx, in.y + 1, fw, a->export_dir, on_dir, t);
    draw_text(s, vx + fw + 1, in.y + 1, bw, browse, t->hi_fg, t->main_bg,
              ATTR_BOLD);
    draw_text(s, lx, in.y + 2, 7, "File", !on_dir ? t->hi_fg : t->inactive_fg,
              t->main_bg, !on_dir ? ATTR_BOLD : 0);
    draw_field(s, vx, in.y + 2, vw, a->prompt, !on_dir, t);
    a->hit_export_dir = (Rect){lx, in.y + 1, vx + fw - lx, 1};
    a->hit_export_browse = (Rect){vx + fw + 1, in.y + 1, bw, 1};
    a->hit_export_file = (Rect){lx, in.y + 2, vx + vw - lx, 1};

    // Keys, and the formats the name can end with
    int x = lx;
    x += draw_text(s, x, in.y + 4, 3, "Tab", t->hi_fg, t->main_bg, ATTR_BOLD);
    x += draw_text(s, x, in.y + 4, 9, " field · ", t->inactive_fg, t->main_bg,
                   0);
    x += draw_text(s, x, in.y + 4, 6, "Ctrl+O", t->hi_fg, t->main_bg,
                   ATTR_BOLD);
    x += draw_text(s, x, in.y + 4, 10, " browse · ", t->inactive_fg,
                   t->main_bg, 0);
    x += draw_text(s, x, in.y + 4, 6, "Ctrl+U", t->hi_fg, t->main_bg,
                   ATTR_BOLD);
    draw_text(s, x, in.y + 4, 6, " clear", t->inactive_fg, t->main_bg, 0);
    draw_hint(s, in.x + in.w - 24, in.y + 4, t);
    draw_border_label(s, box.x + 2, box.y + box.h - 1, ".html .ans .txt", NULL,
                      t);
}
