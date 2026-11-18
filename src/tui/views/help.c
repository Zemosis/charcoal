#include "app.h"

#include <stdio.h>

// The "charcoal smudge" logo: each row a lighter shade, like a fading stroke
static const char *const LOGO[3] = {
    "▓▓▓▓ ▓  ▓ ▓▓▓▓ ▓▓▓  "
    "▓▓▓▓ ▓▓▓▓ ▓▓▓▓ ▓   ",
    "▒    ▒▒▒▒ ▒▒▒▒ ▒▒▒  "
    "▒    ▒  ▒ ▒▒▒▒ ▒   ",
    "░░░░ ░  ░ ░  ░ ░  ░ "
    "░░░░ ░░░░ ░  ░ ░░░░",
};
#define LOGO_W 39

typedef struct HelpEntry {
    const char *keys;
    const char *vim_keys;   // shown instead when vim keys are on, if set
    const char *text;
} HelpEntry;

static const HelpEntry ENTRIES[] = {
    {"q, Ctrl+C", NULL, "Quit."},
    {"?, F1, h", "?, F1", "Show this window."},
    {"Esc", NULL, "Close this window."},
    {"1, 2, 3, 5", NULL, "Toggle the viewer, info, controls and timeline."},
    {"Up, Down", "Up, Down, j, k", "Select a control."},
    {"Left, Right", "Left, Right, h, l", "Change the selected control."},
    {"m, M", NULL, "Next / previous glyph mode."},
    {"p, P", NULL, "Next / previous palette."},
    {"c, C", NULL, "Next / previous color depth."},
    {"d", NULL, "Cycle dithering."},
    {"i", NULL, "Toggle invert (for light terminals)."},
    {"f", NULL, "Cycle fit: contain, cover, stretch."},
    {"+, -", NULL, "Zoom in / out."},
    {"Shift+arrows", "Shift+arrows, H J K L", "Pan when zoomed in."},
    {"r", NULL, "Reset zoom, pan and adjustments."},
    {"Space", NULL, "Play / pause."},
    {", .", NULL, "Step one frame back / forward."},
    {"[ ]", NULL, "Seek 5 seconds back / forward."},
    {"{ }", NULL, "Seek 30 seconds back / forward."},
    {"Home", NULL, "Back to the start."},
    {"< >", NULL, "Slower / faster playback."},
    {"R", NULL, "Toggle repeat (loop)."},
    {"Ctrl+Z", NULL, "Suspend to the shell (fg comes back)."},
    {"Ctrl+R", NULL, "Redraw the screen."},
};
#define ENTRY_COUNT ((int)(sizeof(ENTRIES) / sizeof(ENTRIES[0])))
#define KEY_COL_W 22

// Prototype
static uint32_t blend(uint32_t a, uint32_t b);
static int rows_per_page(const App *a, Rect *box, int *logo_y);

// Halfway between two colors
static uint32_t blend(uint32_t a, uint32_t b) {
    uint32_t r = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF)) / 2;
    uint32_t g = (((a >> 8) & 0xFF) + ((b >> 8) & 0xFF)) / 2;
    uint32_t bl = ((a & 0xFF) + (b & 0xFF)) / 2;
    return r << 16 | g << 8 | bl;
}

// Where the box goes and how many entries fit in it
static int rows_per_page(const App *a, Rect *box, int *logo_y) {
    int cols = a->screen->cols, rows = a->screen->rows;
    int w = cols - 4 < 72 ? cols - 4 : 72;
    int with_logo = rows >= 16 && cols >= LOGO_W + 4;
    int top = with_logo ? 6 : 1;
    int h = rows - top - 1;
    if (h > ENTRY_COUNT + 3) {
        h = ENTRY_COUNT + 3;
    }
    *logo_y = with_logo ? 1 : -1;
    *box = (Rect){(cols - w) / 2, top, w, h};
    int n = h - 3;
    return n < 1 ? 1 : n;
}

void view_help(App *a) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    Rect box;
    int logo_y;
    int per_page = rows_per_page(a, &box, &logo_y);
    if (box.w < 20 || box.h < 4) {
        return;
    }
    int pages = (ENTRY_COUNT + per_page - 1) / per_page;
    if (a->help_page >= pages) {
        a->help_page = pages - 1;
    }

    if (logo_y >= 0) {
        uint32_t shades[3] = {t->main_fg, blend(t->main_fg, t->inactive_fg),
                              t->inactive_fg};
        int lx = (s->cols - LOGO_W) / 2;
        for (int i = 0; i < 3; i++) {
            draw_text(s, lx, logo_y + i, LOGO_W, LOGO[i], shades[i], t->main_bg,
                      0);
        }
        const char *ver = "v" CHARCOAL_VERSION;
        draw_text(s, lx + LOGO_W - text_width(ver), logo_y + 3, 20, ver,
                  t->inactive_fg, t->main_bg, 0);
    }

    draw_fill(s, box, t->main_fg, t->main_bg);
    draw_box(s, box, t, -1, "help");
    Rect in = rect_inner(box);
    int kx = in.x + 2, dx = in.x + 2 + KEY_COL_W;
    draw_text(s, kx, in.y, KEY_COL_W, "Key:", t->title, t->main_bg, ATTR_BOLD);
    draw_text(s, dx, in.y, in.w - KEY_COL_W - 3, "Description:", t->title,
              t->main_bg, ATTR_BOLD);

    int first = a->help_page * per_page;
    for (int i = 0; i < per_page && first + i < ENTRY_COUNT; i++) {
        const HelpEntry *e = &ENTRIES[first + i];
        const char *keys = a->vim_keys && e->vim_keys ? e->vim_keys : e->keys;
        int y = in.y + 1 + i;
        draw_text(s, kx, y, KEY_COL_W - 1, keys, t->hi_fg, t->main_bg, 0);
        draw_text(s, dx, y, in.w - KEY_COL_W - 3, e->text, t->main_fg,
                  t->main_bg, 0);
    }

    if (pages > 1) {
        char label[48];
        snprintf(label, sizeof(label), "↑ page %d/%d ↓", a->help_page + 1,
                 pages);
        draw_border_label(s, box.x + 2, box.y + box.h - 1, label, 0, t);
    }
}

int help_key(App *a, int key) {
    switch (key) {
    case KEY_ESC:
    case KEY_F1:
    case '?':
    case 'q':
        return 1;
    case 'h':
        return !a->vim_keys;
    case KEY_PAGE_DOWN:
    case KEY_DOWN:
    case KEY_RIGHT:
    case 'j':
        a->help_page++;
        break;
    case KEY_PAGE_UP:
    case KEY_UP:
    case KEY_LEFT:
    case 'k':
        if (a->help_page > 0) {
            a->help_page--;
        }
        break;
    }
    return 0;
}
