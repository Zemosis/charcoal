#include "app.h"
#include "buf.h"
#include "config.h"
#include "media.h"
#include "render.h"
#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

// Columns used when there is no terminal to fit
#define DEFAULT_COLS 100

// Cell height / width when the terminal does not report pixel sizes
#define DEFAULT_CELL_ASPECT 2.0

typedef enum Command {
    CMD_VIEW,
    CMD_RENDER,
    CMD_EXPORT
} Command;

// Everything the command line asked for
typedef struct Args {
    Command cmd;
    const char *input;
    const char *output;
    RenderOpts opts;
    int width;
    int height;
    double time;        // --time in seconds, -1 when not given
    long frame;         // --frame index, -1 when not given
    int loop;
    FitMode fit;
    Config config;      // the config file, which the options start from
    char warn[256];     // problems found in the config file
} Args;

// Prototype
static void usage(FILE *out);
static const char *option_value(int argc, char **argv, int *i,
                                const char *arg, const char *s, const char *l);
static int parse_int(const char *s, const char *name, int *out);
static int parse_double(const char *s, const char *name, double lo, double hi,
                        double *out);
static ColorDepth detect_color(void);
static int parse_args(int argc, char **argv, Args *a);
static int terminal_size(int *cols, int *rows, double *cell_aspect);
static int write_all(FILE *f, const Buf *b);
static int has_suffix(const char *s, const char *suffix);

static void usage(FILE *out) {
    fprintf(out,
        "charcoal %s - terminal ASCII art viewer\n"
        "\n"
        "Usage:\n"
        "  charcoal <file> [options]                 open the viewer\n"
        "  charcoal render <file> [options]          print once and exit\n"
        "  charcoal export <file> -o <out> [options] write .txt, .ans or .html\n"
        "\n"
        "Options:\n"
        "  -m, --mode MODE        half (default), ascii, quad, braille, edges\n"
        "  -p, --palette NAME     standard, detailed, simple, blocks,\n"
        "                         or your own ramp, darkest first\n"
        "  -c, --color DEPTH      auto (default), true, 256, 16, mono\n"
        "      --fit KIND         contain (default), cover, stretch (viewer)\n"
        "      --theme NAME       gruvbox_dark, nord, tokyo_night, mono, tty,\n"
        "                         or a file in ~/.config/charcoal/themes\n"
        "  -d, --dither KIND      none (default), floyd, bayer\n"
        "  -w, --width N          output width in characters\n"
        "  -H, --height N         output height in characters\n"
        "  -i, --invert           flip brightness, for light terminal backgrounds\n"
        "      --brightness N     -1 to 1 (default 0)\n"
        "      --contrast N       0 to 4 (default 1)\n"
        "      --gamma N          0.1 to 5 (default 1, higher is brighter)\n"
        "      --saturation N     0 to 4 (default 1, 0 is grayscale)\n"
        "  -t, --time SECONDS     render the video frame shown at this time\n"
        "      --frame N          render the N-th frame (0 is the first)\n"
        "      --loop, --no-loop  loop animations and video (default: loop)\n"
        "  -o, --output FILE      export target (export only)\n"
        "  -h, --help             show this help\n"
        "  -V, --version          show the version\n"
        "\n"
        "Color auto-detection reads COLORTERM and TERM; NO_COLOR forces mono.\n"
        "Defaults come from ~/.config/charcoal/charcoal.conf (press o in the\n"
        "viewer to change them); options here override it.\n",
        CHARCOAL_VERSION);
}

// Match -s VALUE, --long VALUE or --long=VALUE, NULL if arg is not this option
static const char *option_value(int argc, char **argv, int *i,
                                const char *arg, const char *s, const char *l) {
    size_t n = strlen(l);
    if (strncmp(arg, l, n) == 0 && arg[n] == '=') {
        return arg + n + 1;
    }
    if (strcmp(arg, s) != 0 && strcmp(arg, l) != 0) {
        return NULL;
    }
    if (*i + 1 >= argc) {
        fprintf(stderr, "charcoal: %s needs a value\n", arg);
        exit(1);
    }
    return argv[++*i];
}

// Parse a positive integer option, returns 0 on success
static int parse_int(const char *s, const char *name, int *out) {
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 10000) {
        fprintf(stderr, "charcoal: %s must be a number from 1 to 10000\n", name);
        return -1;
    }
    *out = (int)v;
    return 0;
}

// Parse a decimal option within [lo, hi], returns 0 on success
static int parse_double(const char *s, const char *name, double lo, double hi,
                        double *out) {
    char *end;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || !(v >= lo && v <= hi)) {
        fprintf(stderr, "charcoal: %s must be a number from %g to %g\n", name,
                lo, hi);
        return -1;
    }
    *out = v;
    return 0;
}

// Pick the richest color depth the terminal says it supports
static ColorDepth detect_color(void) {
    const char *no_color = getenv("NO_COLOR");
    if (no_color && *no_color) {
        return COLOR_MONO;
    }
    const char *ct = getenv("COLORTERM");
    if (ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0)) {
        return COLOR_TRUE;
    }
    const char *term = getenv("TERM");
    if (term && strstr(term, "256color")) {
        return COLOR_256;
    }
    return COLOR_16;
}

// Fill a from argv, returns 0 on success, 1 to exit cleanly, -1 on error
static int parse_args(int argc, char **argv, Args *a) {
    memset(a, 0, sizeof(*a));
    a->time = -1;
    a->frame = -1;

    // Defaults, then the config file, then the command line
    config_load(&a->config, a->warn, sizeof(a->warn));
    const Config *c = &a->config;
    render_opts_default(&a->opts);
    a->opts.mode = c->mode;
    a->opts.palette = config_palette(c);
    a->opts.invert = c->invert;
    a->opts.color = c->color_auto ? detect_color() : c->color;
    a->opts.dither = c->dither;
    a->opts.brightness = c->brightness;
    a->opts.contrast = c->contrast;
    a->opts.gamma = c->gamma;
    a->opts.saturation = c->saturation;
    a->loop = c->loop;
    a->fit = c->fit;

    int i = 1;
    if (i < argc && strcmp(argv[i], "render") == 0) {
        a->cmd = CMD_RENDER;
        i++;
    } else if (i < argc && strcmp(argv[i], "export") == 0) {
        a->cmd = CMD_EXPORT;
        i++;
    }

    for (; i < argc; i++) {
        const char *arg = argv[i];
        const char *v;
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 1;
        } else if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            char libs[128];
            media_versions(libs, sizeof(libs));
            printf("charcoal %s (%s)\n", CHARCOAL_VERSION, libs);
            return 1;
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--invert") == 0) {
            a->opts.invert = 1;
        } else if ((v = option_value(argc, argv, &i, arg, "-m", "--mode"))) {
            int m = glyph_mode_from_name(v);
            if (m < 0 && strcmp(v, "blocks") == 0) {
                fprintf(stderr, "charcoal: blocks is a palette now: "
                        "-m ascii -p blocks\n");
                return -1;
            }
            if (m < 0) {
                fprintf(stderr, "charcoal: unknown mode '%s' (ascii, half, quad, "
                        "braille, edges)\n", v);
                return -1;
            }
            a->opts.mode = m;
        } else if ((v = option_value(argc, argv, &i, arg, "-p", "--palette"))) {
            const char *p = palette_from_name(v);
            if (!p && *v == '\0') {
                fprintf(stderr, "charcoal: palette cannot be empty\n");
                return -1;
            }
            a->opts.palette = p ? p : v;
        } else if ((v = option_value(argc, argv, &i, arg, "-c", "--color"))) {
            int c = strcmp(v, "auto") == 0 ? (int)detect_color()
                                           : color_depth_from_name(v);
            if (c < 0) {
                fprintf(stderr, "charcoal: unknown color '%s' (auto, true, 256, 16, mono)\n",
                        v);
                return -1;
            }
            a->opts.color = c;
        } else if ((v = option_value(argc, argv, &i, arg, "-d", "--dither"))) {
            int d = dither_mode_from_name(v);
            if (d < 0) {
                fprintf(stderr, "charcoal: unknown dither '%s' (none, floyd, bayer)\n", v);
                return -1;
            }
            a->opts.dither = d;
        } else if ((v = option_value(argc, argv, &i, arg, "", "--brightness"))) {
            if (parse_double(v, "--brightness", -1, 1, &a->opts.brightness)) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "", "--contrast"))) {
            if (parse_double(v, "--contrast", 0, 4, &a->opts.contrast)) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "", "--gamma"))) {
            if (parse_double(v, "--gamma", 0.1, 5, &a->opts.gamma)) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "", "--saturation"))) {
            if (parse_double(v, "--saturation", 0, 4, &a->opts.saturation)) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "-w", "--width"))) {
            if (parse_int(v, "--width", &a->width) != 0) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "-H", "--height"))) {
            if (parse_int(v, "--height", &a->height) != 0) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "-o", "--output"))) {
            a->output = v;
        } else if ((v = option_value(argc, argv, &i, arg, "", "--fit"))) {
            int f = fit_from_name(v);
            if (f < 0) {
                fprintf(stderr, "charcoal: unknown fit '%s' (contain, cover, "
                        "stretch)\n", v);
                return -1;
            }
            a->fit = (FitMode)f;
        } else if ((v = option_value(argc, argv, &i, arg, "", "--theme"))) {
            snprintf(a->config.theme, sizeof(a->config.theme), "%s", v);
        } else if ((v = option_value(argc, argv, &i, arg, "-t", "--time"))) {
            if (parse_double(v, "--time", 0, 1e7, &a->time)) {
                return -1;
            }
        } else if ((v = option_value(argc, argv, &i, arg, "", "--frame"))) {
            char *end;
            long fv = strtol(v, &end, 10);
            if (end == v || *end != '\0' || fv < 0) {
                fprintf(stderr, "charcoal: --frame must be 0 or more\n");
                return -1;
            }
            a->frame = fv;
        } else if (strcmp(arg, "--loop") == 0) {
            a->loop = 1;
        } else if (strcmp(arg, "--no-loop") == 0) {
            a->loop = 0;
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "charcoal: unknown option '%s'\n", arg);
            return -1;
        } else if (!a->input) {
            a->input = arg;
        } else {
            fprintf(stderr, "charcoal: unexpected argument '%s'\n", arg);
            return -1;
        }
    }

    if (!a->input) {
        usage(stderr);
        return -1;
    }
    if (a->cmd == CMD_EXPORT && !a->output) {
        fprintf(stderr, "charcoal: export needs -o <file>\n");
        return -1;
    }
    if (a->cmd != CMD_EXPORT && a->output) {
        fprintf(stderr, "charcoal: -o is only for export\n");
        return -1;
    }
    return 0;
}

// Get the terminal size and the cell aspect ratio, returns 0 on success
static int terminal_size(int *cols, int *rows, double *cell_aspect) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
        return -1;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;

    // Real cell shape when the terminal reports its size in pixels
    *cell_aspect = DEFAULT_CELL_ASPECT;
    if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        double cw = (double)ws.ws_xpixel / ws.ws_col;
        double ch = (double)ws.ws_ypixel / ws.ws_row;
        if (cw > 0 && ch / cw > 1.0 && ch / cw < 4.0) {
            *cell_aspect = ch / cw;
        }
    }
    return 0;
}

static int write_all(FILE *f, const Buf *b) {
    return fwrite(b->data, 1, b->len, f) == b->len ? 0 : -1;
}

static int has_suffix(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

int main(int argc, char **argv) {
    Args a;
    int rc = parse_args(argc, argv, &a);
    if (rc != 0) {
        return rc > 0 ? 0 : 1;
    }

    struct stat st;
    int viewer = a.cmd == CMD_VIEW && isatty(STDIN_FILENO) &&
                 isatty(STDOUT_FILENO);
    if (a.warn[0] && !viewer) {
        fprintf(stderr, "charcoal: warning: %s\n", a.warn);
    }
    if (stat(a.input, &st) == 0 && S_ISDIR(st.st_mode)) {
        fprintf(stderr, "charcoal: %s is a directory\n", a.input);
        return 1;
    }

    // The viewer needs a terminal on both ends; otherwise print once
    if (viewer) {
        if (a.width || a.height) {
            fprintf(stderr, "charcoal: -w and -H are for render and export\n");
            return 1;
        }
        return app_run(a.input, &a.opts, a.fit, a.loop, &a.config,
                       detect_color(), a.warn);
    }

    char err[512];
    MediaFile *m = media_open(a.input, err, sizeof(err));
    if (!m) {
        fprintf(stderr, "charcoal: %s\n", err);
        return 1;
    }
    int img_w, img_h;
    media_size(m, &img_w, &img_h);

    // Output size: explicit, else fit the terminal, else a fixed width
    int cols = 0, rows = 0, term_cols = 0, term_rows = 0;
    double aspect = DEFAULT_CELL_ASPECT;
    int have_term = a.cmd != CMD_EXPORT &&
                    terminal_size(&term_cols, &term_rows, &aspect) == 0;
    if (a.width && a.height) {
        cols = a.width;
        rows = a.height;
    } else if (a.width) {
        cols = a.width;
        rows = render_rows_for_cols(img_w, img_h, cols, aspect);
    } else if (a.height) {
        rows = a.height;
        cols = render_cols_for_rows(img_w, img_h, rows, aspect);
    } else if (have_term) {
        // One line is left for the shell prompt
        render_fit(img_w, img_h, term_cols, term_rows - 1, aspect,
                   a.opts.mode, &cols, &rows);
    } else {
        render_fit(img_w, img_h, DEFAULT_COLS, 10000, aspect, a.opts.mode,
                   &cols, &rows);
    }

    a.opts.cell_aspect = aspect;

    // Decode only as many pixels as the cells can show
    int sub_w, sub_h;
    mode_subpixels(a.opts.mode, &sub_w, &sub_h);
    // --frame wins over --time; neither means the first frame
    int64_t t_us = a.time > 0 ? (int64_t)(a.time * 1e6) : 0;
    int64_t index = a.frame >= 0 ? a.frame : a.time > 0 ? -1 : 0;
    Frame *f = media_decode_at(m, t_us, index, cols * sub_w, rows * sub_h, err,
                               sizeof(err));
    media_close(m);
    if (!f) {
        fprintf(stderr, "charcoal: %s: %s\n", a.input, err);
        return 1;
    }

    Grid *g = grid_new(cols, rows);
    if (!g || render_frame(f, &a.opts, g) != 0) {
        fprintf(stderr, "charcoal: out of memory\n");
        grid_free(g);
        frame_free(f);
        return 1;
    }
    frame_free(f);

    Buf out;
    buf_init(&out);
    rc = 0;
    if (a.cmd == CMD_EXPORT) {
        if (has_suffix(a.output, ".html")) {
            // The page takes the theme's colors for the terminal defaults
            Theme t;
            char twarn[160];
            theme_load(a.config.theme, &t, twarn, sizeof(twarn));
            export_html(g, &out, t.main_fg,
                        a.config.theme_background ? t.main_bg
                                                  : CELL_COLOR_DEFAULT);
        } else if (has_suffix(a.output, ".ans")) {
            export_ansi(g, &out);
        } else if (has_suffix(a.output, ".txt")) {
            export_txt(g, &out);
        } else {
            fprintf(stderr, "charcoal: export writes .txt, .ans or .html\n");
            rc = 1;
        }
        FILE *file = rc ? NULL : fopen(a.output, "w");
        if (!rc && (!file || write_all(file, &out) != 0)) {
            fprintf(stderr, "charcoal: cannot write %s\n", a.output);
            rc = 1;
        }
        if (file) {
            fclose(file);
        }
    } else {
        export_ansi(g, &out);
        if (write_all(stdout, &out) != 0) {
            rc = 1;
        }
    }

    buf_free(&out);
    grid_free(g);
    return rc;
}
