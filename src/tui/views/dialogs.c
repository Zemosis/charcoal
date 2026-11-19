#include "app.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MENU_W 24
#define DIALOG_H 7

static const char *const MENU_ITEMS[MENU_COUNT] = {"Options", "Help", "Quit"};

// Prototype
static void draw_hint(Screen *s, int x, int y, const Theme *t);

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

void view_export(App *a) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    int w = s->cols - 4 < 72 ? s->cols - 4 : 72;
    if (w < 30 || s->rows < DIALOG_H) {
        // Too small for a box: the status line is all there is
        char line[300];
        snprintf(line, sizeof(line), "export as: %s█", a->prompt);
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
    int lx = in.x + 2, vx = in.x + 8, vw = in.w - 10;

    // The file name field: a solid bar so it is clearly where typing goes
    draw_text(s, lx, in.y + 1, 5, "File", t->inactive_fg, t->main_bg, 0);
    draw_fill(s, (Rect){vx, in.y + 1, vw, 1}, t->selected_fg, t->selected_bg);
    const char *shown = a->prompt;
    int over = text_width(shown) + 1 - vw;
    uint32_t cp;
    while (over-- > 0) {
        shown += utf8_decode(shown, &cp);   // keep the end in view
    }
    int n = draw_text(s, vx, in.y + 1, vw, shown, t->selected_fg,
                      t->selected_bg, 0);
    screen_put(s, vx + n, in.y + 1, 0x2588, t->selected_fg, t->selected_bg, 0);

    // Where it lands: the folder of the path, relative to where charcoal runs
    char cwd[512], where[600];
    if (!getcwd(cwd, sizeof(cwd))) {
        snprintf(cwd, sizeof(cwd), ".");
    }
    const char *slash = strrchr(a->prompt, '/');
    if (a->prompt[0] == '/') {
        snprintf(where, sizeof(where), "%.*s",
                 slash == a->prompt ? 1 : (int)(slash - a->prompt), a->prompt);
    } else if (slash) {
        snprintf(where, sizeof(where), "%s/%.*s", cwd, (int)(slash - a->prompt),
                 a->prompt);
    } else {
        snprintf(where, sizeof(where), "%s", cwd);
    }
    draw_text(s, lx, in.y + 2, 5, "in", t->inactive_fg, t->main_bg, 0);
    draw_text(s, vx, in.y + 2, vw, where, t->main_fg, t->main_bg, 0);

    draw_text(s, lx, in.y + 4, 20, ".html .ans .txt", t->inactive_fg,
              t->main_bg, 0);
    draw_hint(s, in.x + in.w - 24, in.y + 4, t);
}
