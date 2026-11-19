// realpath is an X/Open function
#define _XOPEN_SOURCE 700

#include "app.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PICKER_W 72

// Prototype
static int matches(const char *name, const char *filter);
static void refilter(App *a);
static void select_name(App *a, const char *name);
static void go_to(App *a, const char *dir, const char *select);
static void go_up(App *a);
static void open_selected(App *a, int *close);

// Case-insensitive "name contains filter"
static int matches(const char *name, const char *filter) {
    size_t fl = strlen(filter);
    if (fl == 0) {
        return 1;
    }
    for (const char *p = name; *p; p++) {
        size_t i = 0;
        while (i < fl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)filter[i])) {
            i++;
        }
        if (i == fl) {
            return 1;
        }
    }
    return 0;
}

// Rebuild the visible list after the folder or the filter changed
static void refilter(App *a) {
    free(a->picker_view);
    a->picker_view = malloc(sizeof(int) * (size_t)(a->picker_list.count + 1));
    a->picker_count = 0;
    if (!a->picker_view) {
        return;
    }
    for (int i = 0; i < a->picker_list.count; i++) {
        if (matches(a->picker_list.items[i].name, a->picker_filter)) {
            a->picker_view[a->picker_count++] = i;
        }
    }
    if (a->picker_sel >= a->picker_count) {
        a->picker_sel = a->picker_count ? a->picker_count - 1 : 0;
    }
}

static void select_name(App *a, const char *name) {
    for (int i = 0; i < a->picker_count; i++) {
        if (strcmp(a->picker_list.items[a->picker_view[i]].name, name) == 0) {
            a->picker_sel = i;
            return;
        }
    }
}

// Show dir, with select highlighted if it is there
static void go_to(App *a, const char *dir, const char *select) {
    char real[PATH_MAX];
    if (!realpath(dir, real)) {
        snprintf(a->message, sizeof(a->message), "cannot open %.200s", dir);
        return;
    }
    FileList list;
    if (files_list(real, 1, &list) != 0) {
        snprintf(a->message, sizeof(a->message), "cannot read %.200s", real);
        return;
    }
    files_free(&a->picker_list);
    a->picker_list = list;
    snprintf(a->picker_dir, sizeof(a->picker_dir), "%s", real);
    a->picker_filter[0] = '\0';
    a->picker_sel = 0;
    a->picker_scroll = 0;
    refilter(a);
    // Skip ".." so the first real entry is selected by default
    if (a->picker_count > 1 && strcmp(a->picker_list.items[0].name, "..") == 0) {
        a->picker_sel = 1;
    }
    if (select) {
        select_name(a, select);
    }
}

static void go_up(App *a) {
    if (strcmp(a->picker_dir, "/") == 0) {
        return;
    }
    char parent[4096], came_from[256];
    snprintf(came_from, sizeof(came_from), "%s", path_name(a->picker_dir));
    path_dir(a->picker_dir, parent, sizeof(parent));
    go_to(a, parent, came_from);
}

// Enter a folder, or open a file and close the picker
static void open_selected(App *a, int *close) {
    if (a->picker_count == 0) {
        return;
    }
    const FileEntry *e = &a->picker_list.items[a->picker_view[a->picker_sel]];
    if (strcmp(e->name, "..") == 0) {
        go_up(a);
        return;
    }
    char path[4400];
    snprintf(path, sizeof(path), "%s%s%s", a->picker_dir,
             strcmp(a->picker_dir, "/") == 0 ? "" : "/", e->name);
    if (e->is_dir) {
        go_to(a, path, NULL);
    } else if (app_open_file(a, path) == 0) {
        *close = 1;
    }
}

void picker_open(App *a, const char *dir) {
    char real[PATH_MAX];
    const char *current = NULL;
    if (a->path[0] && realpath(a->path, real)) {
        current = path_name(real);
    }
    go_to(a, dir, NULL);
    if (current) {
        // Start on the file being viewed when it is in this folder
        char here[PATH_MAX];
        path_dir(real, here, sizeof(here));
        if (strcmp(here, a->picker_dir) == 0) {
            select_name(a, current);
        }
    }
    a->overlay = OVERLAY_PICKER;
}

void picker_close(App *a) {
    files_free(&a->picker_list);
    free(a->picker_view);
    a->picker_view = NULL;
    a->picker_count = 0;
}

void view_picker(App *a) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    int w = s->cols - 4 < PICKER_W ? s->cols - 4 : PICKER_W;
    int h = s->rows - 4;
    if (w < 24 || h < 6) {
        draw_text(s, 0, 0, s->cols, "The file picker needs at least 28x10.",
                  t->hi_fg, t->main_bg, 0);
        return;
    }
    Rect box = {(s->cols - w) / 2, 2, w, h};
    draw_fill(s, box, t->main_fg, t->main_bg);
    draw_box(s, box, t, -1, "open");

    // The folder at the right of the top border, keeping its end in view
    const char *dir = a->picker_dir;
    int max = w - 14;
    while (text_width(dir) > max && strchr(dir + 1, '/')) {
        dir = strchr(dir + 1, '/');
    }
    draw_border_label(s, box.x + box.w - 3 - text_width(dir), box.y, dir, NULL,
                      t);

    Rect in = rect_inner(box);
    a->hit_picker = in;
    if (a->picker_sel < a->picker_scroll) {
        a->picker_scroll = a->picker_sel;
    }
    if (a->picker_sel >= a->picker_scroll + in.h) {
        a->picker_scroll = a->picker_sel - in.h + 1;
    }

    char cur_real[PATH_MAX];
    int have_cur = a->path[0] && realpath(a->path, cur_real);
    for (int row = 0; row < in.h; row++) {
        int i = a->picker_scroll + row;
        if (i >= a->picker_count) {
            break;
        }
        const FileEntry *e = &a->picker_list.items[a->picker_view[i]];
        int sel = i == a->picker_sel;
        int y = in.y + row;
        uint32_t bg = sel ? t->selected_bg : t->main_bg;
        uint32_t fg = sel ? t->selected_fg : e->is_dir ? t->title : t->main_fg;
        uint8_t attr = sel && s->depth == COLOR_MONO ? ATTR_REVERSE : 0;
        draw_fill(s, (Rect){in.x, y, in.w, 1}, fg, bg);

        // ● marks the file on screen now
        char full[4400];
        snprintf(full, sizeof(full), "%s/%s", a->picker_dir, e->name);
        if (have_cur && !e->is_dir && strcmp(full, cur_real) == 0) {
            screen_put(s, in.x + 1, y, 0x25CF, sel ? fg : t->hi_fg, bg, attr);
        }
        char label[300];
        snprintf(label, sizeof(label), "%s%s", e->name, e->is_dir ? "/" : "");
        draw_text(s, in.x + 3, y, in.w - 12, label, fg, bg,
                  attr | (e->is_dir ? ATTR_BOLD : 0));
        if (!e->is_dir) {
            char size[16];
            format_size(e->size, size, sizeof(size));
            int sw = text_width(size);
            draw_text(s, in.x + in.w - 1 - sw, y, sw, size,
                      sel ? fg : t->inactive_fg, bg, attr);
        }
    }
    if (a->picker_count == 0) {
        draw_text(s, in.x + 3, in.y, in.w - 4,
                  a->picker_filter[0] ? "nothing matches" : "no images or videos",
                  t->inactive_fg, t->main_bg, 0);
    }

    // Typing filters; the bottom border shows what was typed and the keys
    char filter[100];
    if (a->picker_filter[0]) {
        snprintf(filter, sizeof(filter), "filter: %s█", a->picker_filter);
    } else {
        snprintf(filter, sizeof(filter), "type to filter");
    }
    int y1 = box.y + box.h - 1;
    draw_border_label(s, box.x + 2, y1, filter, NULL, t);
    const char *keys = "Enter open · ← up · Esc";
    if (box.w > text_width(filter) + text_width(keys) + 10) {
        draw_border_label(s, box.x + box.w - 4 - text_width(keys), y1, keys, NULL,
                          t);
    }
}

int picker_key(App *a, int key) {
    int close = 0;
    size_t len = strlen(a->picker_filter);
    switch (key) {
    case KEY_ESC:
        // A filter clears first, then the picker closes
        if (len) {
            a->picker_filter[0] = '\0';
            refilter(a);
        } else {
            close = 1;
        }
        break;
    case KEY_UP:
        if (a->picker_sel > 0) {
            a->picker_sel--;
        }
        break;
    case KEY_DOWN:
        if (a->picker_sel + 1 < a->picker_count) {
            a->picker_sel++;
        }
        break;
    case KEY_PAGE_UP:
        a->picker_sel = a->picker_sel > 10 ? a->picker_sel - 10 : 0;
        break;
    case KEY_PAGE_DOWN:
        a->picker_sel += 10;
        if (a->picker_sel >= a->picker_count) {
            a->picker_sel = a->picker_count ? a->picker_count - 1 : 0;
        }
        break;
    case KEY_HOME:
        a->picker_sel = 0;
        break;
    case KEY_END:
        a->picker_sel = a->picker_count ? a->picker_count - 1 : 0;
        break;
    case KEY_LEFT:
        go_up(a);
        break;
    case KEY_RIGHT:
    case KEY_ENTER:
        open_selected(a, &close);
        break;
    case KEY_BACKSPACE:
        if (len) {
            a->picker_filter[len - 1] = '\0';
            refilter(a);
        } else {
            go_up(a);
        }
        break;
    default:
        // Printable ASCII narrows the list
        if (key >= 0x20 && key < 0x7F && len + 1 < sizeof(a->picker_filter)) {
            a->picker_filter[len] = (char)key;
            a->picker_filter[len + 1] = '\0';
            a->picker_sel = 0;
            refilter(a);
        }
        break;
    }
    return close;
}
