#include "config.h"
#include "tui.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDX(n) (CELL_COLOR_INDEX | (n))

static const Theme BUILTIN[] = {
    // Close to btop's gruvbox_material_dark
    {"gruvbox_dark", 0x282828, 0xd4be98, 0xd4be98, 0xea6962, 0xd8a657,
     0x282828, 0x7c6f64, 0x5a524c, 0xa9b665, 0xd8a657, 0xea6962},
    {"nord", 0x2e3440, 0xd8dee9, 0xeceff4, 0x88c0d0, 0x5e81ac, 0xeceff4,
     0x616e88, 0x4c566a, 0xa3be8c, 0xebcb8b, 0xbf616a},
    {"tokyo_night", 0x1a1b26, 0xc0caf5, 0xc0caf5, 0x7aa2f7, 0x7aa2f7,
     0x1a1b26, 0x565f89, 0x3b4261, 0x9ece6a, 0xe0af68, 0xf7768e},
    {"mono", 0x1c1c1c, 0xd0d0d0, 0xffffff, 0xffffff, 0xd0d0d0, 0x1c1c1c,
     0x767676, 0x4e4e4e, 0x808080, 0xb2b2b2, 0xeeeeee},
    // The terminal's own 16 colors, for consoles without 256 colors
    {"tty", CELL_COLOR_DEFAULT, CELL_COLOR_DEFAULT, IDX(15), IDX(1), IDX(3),
     IDX(0), IDX(8), IDX(8), IDX(2), IDX(3), IDX(1)},
};
#define BUILTIN_COUNT ((int)(sizeof(BUILTIN) / sizeof(BUILTIN[0])))

// Theme file keys, in the order of the Theme fields after the name
static const char *const KEYS[] = {
    "main_bg", "main_fg", "title", "hi_fg", "selected_bg", "selected_fg",
    "inactive_fg", "box_border", "graph_low", "graph_mid", "graph_high",
};
#define KEY_COUNT ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

// Prototype
static uint32_t *theme_field(Theme *t, int i);
static int parse_color(const char *v, uint32_t *out);
static int load_file(const char *path, Theme *t, char *warn, int warn_len);
static int name_cmp(const void *a, const void *b);

const Theme *theme_default(void) {
    return &BUILTIN[0];
}

int theme_builtin_count(void) {
    return BUILTIN_COUNT;
}

const Theme *theme_builtin(int i) {
    return i >= 0 && i < BUILTIN_COUNT ? &BUILTIN[i] : NULL;
}

static uint32_t *theme_field(Theme *t, int i) {
    uint32_t *fields[] = {
        &t->main_bg, &t->main_fg, &t->title, &t->hi_fg,
        &t->selected_bg, &t->selected_fg, &t->inactive_fg, &t->box_border,
        &t->graph_low, &t->graph_mid, &t->graph_high,
    };
    return fields[i];
}

// "#rrggbb", "default" (the terminal's color) or a palette index 0-255
static int parse_color(const char *v, uint32_t *out) {
    if (v[0] == '#' && strlen(v) == 7) {
        char *end;
        unsigned long x = strtoul(v + 1, &end, 16);
        if (*end) {
            return -1;
        }
        *out = (uint32_t)x;
        return 0;
    }
    if (strcmp(v, "default") == 0) {
        *out = CELL_COLOR_DEFAULT;
        return 0;
    }
    char *end;
    long n = strtol(v, &end, 10);
    if (end != v && !*end && n >= 0 && n <= 255) {
        *out = IDX((uint32_t)n);
        return 0;
    }
    return -1;
}

int theme_parse(const char *text, Theme *t, char *warn, int warn_len) {
    int bad = 0;
    if (warn_len > 0) {
        warn[0] = '\0';
    }
    while (*text) {
        const char *nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : strlen(text);
        char line[256];
        if (n >= sizeof(line)) {
            n = sizeof(line) - 1;
        }
        memcpy(line, text, n);
        line[n] = '\0';
        text = nl ? nl + 1 : text + strlen(text);

        // key = "#rrggbb"   # comment
        char key[64], value[64];
        char *hash = strchr(line, '#');
        char *quote = strchr(line, '"');
        if (hash && (!quote || hash < quote)) {
            *hash = '\0';
        }
        if (sscanf(line, " %63[a-z_] = \"%63[^\"]\"", key, value) != 2 &&
            sscanf(line, " %63[a-z_] = %63s", key, value) != 2) {
            continue;
        }
        int found = 0;
        for (int i = 0; i < KEY_COUNT; i++) {
            if (strcmp(key, KEYS[i]) == 0) {
                found = 1;
                if (parse_color(value, theme_field(t, i)) != 0) {
                    bad++;
                    if (warn_len > 0 && !warn[0]) {
                        snprintf(warn, (size_t)warn_len, "bad color for %s", key);
                    }
                }
            }
        }
        if (!found) {
            bad++;
            if (warn_len > 0 && !warn[0]) {
                snprintf(warn, (size_t)warn_len, "unknown key %s", key);
            }
        }
    }
    return bad ? -1 : 0;
}

static int load_file(const char *path, Theme *t, char *warn, int warn_len) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    Buf text;
    buf_init(&text);
    char chunk[2048];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        buf_push(&text, chunk, n);
    }
    fclose(f);
    buf_putc(&text, '\0');
    theme_parse(text.data, t, warn, warn_len);
    buf_free(&text);
    return 0;
}

int theme_load(const char *name, Theme *out, char *warn, int warn_len) {
    if (warn_len > 0) {
        warn[0] = '\0';
    }

    // A user theme file wins; it starts from the default so it may be partial
    char path[1024], rel[128];
    snprintf(rel, sizeof(rel), "themes/%s.theme", name);
    if (config_path(rel, path, sizeof(path)) == 0) {
        Theme t = BUILTIN[0];
        char inner[96];
        if (load_file(path, &t, inner, sizeof(inner)) == 0) {
            snprintf(t.name, sizeof(t.name), "%s", name);
            *out = t;
            if (inner[0]) {
                snprintf(warn, (size_t)warn_len, "theme %s: %s", name, inner);
                return -1;
            }
            return 0;
        }
    }
    for (int i = 0; i < BUILTIN_COUNT; i++) {
        if (strcmp(BUILTIN[i].name, name) == 0) {
            *out = BUILTIN[i];
            return 0;
        }
    }
    *out = BUILTIN[0];
    snprintf(warn, (size_t)warn_len, "no theme named %s", name);
    return -1;
}

static int name_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int theme_list(char names[][64], int max) {
    int n = 0;
    for (int i = 0; i < BUILTIN_COUNT && n < max; i++) {
        snprintf(names[n++], 64, "%.63s", BUILTIN[i].name);
    }

    // User themes after the built-ins, sorted, skipping names already listed
    char dir[1024];
    if (config_path("themes", dir, sizeof(dir)) != 0) {
        return n;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return n;
    }
    int first_user = n;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        size_t len = strlen(e->d_name);
        if (len <= 6 || len > 63 || strcmp(e->d_name + len - 6, ".theme") != 0) {
            continue;
        }
        char name[64];
        snprintf(name, sizeof(name), "%.*s", (int)(len - 6), e->d_name);
        int dup = 0;
        for (int i = 0; i < n; i++) {
            dup |= strcmp(names[i], name) == 0;
        }
        if (!dup) {
            snprintf(names[n++], 64, "%s", name);
        }
    }
    closedir(d);
    qsort(names[first_user], (size_t)(n - first_user), 64, name_cmp);
    return n;
}
