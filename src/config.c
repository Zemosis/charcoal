#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_FILE "charcoal.conf"

static const char *const FIT_NAMES[FIT_COUNT] = {"contain", "cover", "stretch"};

// Prototype
static char *trim(char *s);
static void unquote(char *s);
static int parse_bool(const char *v, int *out);
static int parse_num(const char *v, double lo, double hi, double *out);
static int apply(Config *c, const char *key, const char *v);
static void add_warning(char *warn, int warn_len, const char *fmt, int line,
                        const char *what);
static void put_line(Buf *out, const char *key, const char *value, int quote);
static void put_num(Buf *out, const char *key, double v);
static int mkdir_p(const char *dir);

const char *fit_name(FitMode f) {
    return (f >= 0 && f < FIT_COUNT) ? FIT_NAMES[f] : "?";
}

int fit_from_name(const char *name) {
    for (int i = 0; i < FIT_COUNT; i++) {
        if (strcmp(name, FIT_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

void config_defaults(Config *c) {
    memset(c, 0, sizeof(*c));
    snprintf(c->theme, sizeof(c->theme), "gruvbox_dark");
    c->theme_background = 1;
    c->rounded = 1;
    c->vim_keys = 1;
    c->mode = MODE_HALF;
    snprintf(c->palette, sizeof(c->palette), "standard");
    c->fit = FIT_CONTAIN;
    c->color_auto = 1;
    c->color = COLOR_TRUE;
    c->dither = DITHER_NONE;
    c->contrast = 1.0;
    c->gamma = 1.0;
    c->saturation = 1.0;
    c->loop = 1;
    c->max_fps = 0;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return s;
}

// "value" or 'value' -> value
static void unquote(char *s) {
    size_t n = strlen(s);
    if (n >= 2 && (s[0] == '"' || s[0] == '\'') && s[n - 1] == s[0]) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = '\0';
    }
}

static int parse_bool(const char *v, int *out) {
    if (strcmp(v, "true") == 0 || strcmp(v, "True") == 0 || strcmp(v, "1") == 0) {
        *out = 1;
        return 0;
    }
    if (strcmp(v, "false") == 0 || strcmp(v, "False") == 0 ||
        strcmp(v, "0") == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static int parse_num(const char *v, double lo, double hi, double *out) {
    char *end;
    errno = 0;
    double d = strtod(v, &end);
    if (end == v || *end != '\0' || errno || !(d >= lo && d <= hi)) {
        return -1;
    }
    *out = d;
    return 0;
}

// Set one key, returns 0, -1 for a bad value, -2 for an unknown key
static int apply(Config *c, const char *key, const char *v) {
    int i;
    double d;
    if (strcmp(key, "theme") == 0) {
        if (!*v || strlen(v) >= sizeof(c->theme)) {
            return -1;
        }
        snprintf(c->theme, sizeof(c->theme), "%s", v);
    } else if (strcmp(key, "theme_background") == 0) {
        return parse_bool(v, &c->theme_background);
    } else if (strcmp(key, "rounded") == 0) {
        return parse_bool(v, &c->rounded);
    } else if (strcmp(key, "vim_keys") == 0) {
        return parse_bool(v, &c->vim_keys);
    } else if (strcmp(key, "mode") == 0) {
        if ((i = glyph_mode_from_name(v)) < 0) {
            return -1;
        }
        c->mode = (GlyphMode)i;
    } else if (strcmp(key, "palette") == 0) {
        if (!*v || strlen(v) >= sizeof(c->palette)) {
            return -1;
        }
        snprintf(c->palette, sizeof(c->palette), "%s", v);
    } else if (strcmp(key, "fit") == 0) {
        if ((i = fit_from_name(v)) < 0) {
            return -1;
        }
        c->fit = (FitMode)i;
    } else if (strcmp(key, "invert") == 0) {
        return parse_bool(v, &c->invert);
    } else if (strcmp(key, "color") == 0) {
        if (strcmp(v, "auto") == 0) {
            c->color_auto = 1;
        } else if ((i = color_depth_from_name(v)) >= 0) {
            c->color_auto = 0;
            c->color = (ColorDepth)i;
        } else {
            return -1;
        }
    } else if (strcmp(key, "dither") == 0) {
        if ((i = dither_mode_from_name(v)) < 0) {
            return -1;
        }
        c->dither = (DitherMode)i;
    } else if (strcmp(key, "brightness") == 0) {
        return parse_num(v, -1, 1, &c->brightness);
    } else if (strcmp(key, "contrast") == 0) {
        return parse_num(v, 0, 4, &c->contrast);
    } else if (strcmp(key, "gamma") == 0) {
        return parse_num(v, 0.1, 5, &c->gamma);
    } else if (strcmp(key, "saturation") == 0) {
        return parse_num(v, 0, 4, &c->saturation);
    } else if (strcmp(key, "loop") == 0) {
        return parse_bool(v, &c->loop);
    } else if (strcmp(key, "max_fps") == 0) {
        if (parse_num(v, 0, 240, &d) != 0) {
            return -1;
        }
        c->max_fps = (int)d;
    } else {
        return -2;
    }
    return 0;
}

static void add_warning(char *warn, int warn_len, const char *fmt, int line,
                        const char *what) {
    size_t used = strlen(warn);
    if (used + 1 >= (size_t)warn_len) {
        return;
    }
    if (used) {
        snprintf(warn + used, (size_t)warn_len - used, "; ");
        used = strlen(warn);
    }
    snprintf(warn + used, (size_t)warn_len - used, fmt, line, what);
}

void config_parse(const char *text, Config *c, char *warn, int warn_len) {
    if (warn_len > 0) {
        warn[0] = '\0';
    }
    int line_no = 0;
    while (*text) {
        // One line at a time, without the newline
        const char *nl = strchr(text, '\n');
        size_t n = nl ? (size_t)(nl - text) : strlen(text);
        char line[512];
        if (n >= sizeof(line)) {
            n = sizeof(line) - 1;
        }
        memcpy(line, text, n);
        line[n] = '\0';
        text = nl ? nl + 1 : text + strlen(text);
        line_no++;

        // Comments start with #, outside quotes
        int quoted = 0;
        for (char *p = line; *p; p++) {
            if (*p == '"') {
                quoted = !quoted;
            } else if (*p == '#' && !quoted) {
                *p = '\0';
                break;
            }
        }
        char *s = trim(line);
        if (!*s) {
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            add_warning(warn, warn_len, "line %d: expected key = value%s",
                        line_no, "");
            continue;
        }
        *eq = '\0';
        char *key = trim(s);
        char *value = trim(eq + 1);
        unquote(value);
        int r = apply(c, key, value);
        if (r == -1) {
            add_warning(warn, warn_len, "line %d: bad value for %s", line_no,
                        key);
        } else if (r == -2) {
            add_warning(warn, warn_len, "line %d: unknown key %s", line_no, key);
        }
    }
}

static void put_line(Buf *out, const char *key, const char *value, int quote) {
    char line[384];
    snprintf(line, sizeof(line), quote ? "%-17s= \"%s\"\n" : "%-17s= %s\n", key,
             value);
    buf_puts(out, line);
}

static void put_num(Buf *out, const char *key, double v) {
    char num[32];
    snprintf(num, sizeof(num), "%g", v);
    put_line(out, key, num, 0);
}

void config_write(const Config *c, Buf *out) {
    buf_puts(out, "# charcoal settings, written by the options overlay (o)\n"
                  "# Command-line options override these.\n\n");
    put_line(out, "theme", c->theme, 1);
    put_line(out, "theme_background", c->theme_background ? "true" : "false", 0);
    put_line(out, "rounded", c->rounded ? "true" : "false", 0);
    put_line(out, "vim_keys", c->vim_keys ? "true" : "false", 0);
    buf_putc(out, '\n');
    put_line(out, "mode", glyph_mode_name(c->mode), 1);
    put_line(out, "palette", c->palette, 1);
    put_line(out, "fit", fit_name(c->fit), 1);
    put_line(out, "invert", c->invert ? "true" : "false", 0);
    buf_putc(out, '\n');
    put_line(out, "color", c->color_auto ? "auto" : color_depth_name(c->color),
             1);
    put_line(out, "dither", dither_mode_name(c->dither), 1);
    put_num(out, "brightness", c->brightness);
    put_num(out, "contrast", c->contrast);
    put_num(out, "gamma", c->gamma);
    put_num(out, "saturation", c->saturation);
    buf_putc(out, '\n');
    put_line(out, "loop", c->loop ? "true" : "false", 0);
    put_num(out, "max_fps", c->max_fps);
}

int config_path(const char *name, char *out, int n) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int len;
    if (xdg && *xdg) {
        len = snprintf(out, (size_t)n, "%s/charcoal%s%s", xdg, *name ? "/" : "",
                       name);
    } else if (home && *home) {
        len = snprintf(out, (size_t)n, "%s/.config/charcoal%s%s", home,
                       *name ? "/" : "", name);
    } else {
        return -1;
    }
    return len > 0 && len < n ? 0 : -1;
}

int config_load(Config *c, char *warn, int warn_len) {
    config_defaults(c);
    if (warn_len > 0) {
        warn[0] = '\0';
    }
    char path[1024];
    if (config_path(CONFIG_FILE, path, sizeof(path)) != 0) {
        return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;   // no config yet: defaults
    }
    Buf text;
    buf_init(&text);
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        buf_push(&text, chunk, n);
    }
    fclose(f);
    buf_putc(&text, '\0');
    char inner[256];
    config_parse(text.data, c, inner, sizeof(inner));
    buf_free(&text);
    if (inner[0]) {
        snprintf(warn, (size_t)warn_len, "%s: %s", CONFIG_FILE, inner);
        return -1;
    }
    return 0;
}

// mkdir -p for the config folder
static int mkdir_p(const char *dir) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

int config_save(const Config *c, char *err, int err_len) {
    char dir[1024], path[1024], tmp[1100];
    if (config_path("", dir, sizeof(dir)) != 0 ||
        config_path(CONFIG_FILE, path, sizeof(path)) != 0) {
        snprintf(err, (size_t)err_len, "no home folder for the config");
        return -1;
    }
    if (mkdir_p(dir) != 0) {
        snprintf(err, (size_t)err_len, "cannot create %s", dir);
        return -1;
    }

    // Write a temporary file and rename it, so a crash never leaves half a file
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    Buf out;
    buf_init(&out);
    config_write(c, &out);
    FILE *f = fopen(tmp, "w");
    int ok = f && fwrite(out.data, 1, out.len, f) == out.len;
    if (f && fclose(f) != 0) {
        ok = 0;
    }
    buf_free(&out);
    if (!ok || rename(tmp, path) != 0) {
        unlink(tmp);
        snprintf(err, (size_t)err_len, "cannot write %s", path);
        return -1;
    }
    return 0;
}

const char *config_palette(const Config *c) {
    const char *p = palette_from_name(c->palette);
    return p ? p : c->palette;
}
