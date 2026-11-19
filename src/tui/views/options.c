#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define LIST_W 32
#define TAB_COUNT 4

typedef enum Option {
    OPT_THEME,
    OPT_THEME_BG,
    OPT_ROUNDED,
    OPT_VIM,
    OPT_MODE,
    OPT_PALETTE,
    OPT_FIT,
    OPT_INVERT,
    OPT_COLOR,
    OPT_DITHER,
    OPT_BRIGHTNESS,
    OPT_CONTRAST,
    OPT_GAMMA,
    OPT_SATURATION,
    OPT_LOOP,
    OPT_MAX_FPS,
    OPT_COUNT
} Option;

typedef struct OptionInfo {
    int tab;
    const char *name;
    const char *help;
} OptionInfo;

static const char *const TABS[TAB_COUNT] = {"general", "render", "color",
                                            "playback"};

static const OptionInfo OPTIONS[OPT_COUNT] = {
    {0, "Color theme",
     "Set color theme.\n\nBuilt-in: gruvbox_dark, nord, tokyo_night, mono, "
     "and tty (the terminal's own 16 colors).\n\nYour own themes go in "
     "~/.config/charcoal/themes/<name>.theme as lines like\nmain_bg = "
     "\"#282828\"."},
    {0, "Theme background",
     "Paint the theme's background color.\n\nFalse keeps the terminal's own "
     "background, transparency included."},
    {0, "Rounded corners",
     "Rounded box corners.\n\nFalse uses square ones, for fonts without "
     "╭╮╰╯."},
    {0, "Vim keys",
     "Enable vim keys.\n\nh j k l move through the controls and change them, "
     "H J K L pan when zoomed in.\n\nWith vim keys off, h opens the help."},
    {1, "Glyph mode",
     "Glyph mode used when a file opens.\n\nhalf: two pixels per cell, the "
     "most detail.\nquad: four pixels per cell.\nbraille: eight dots per "
     "cell.\nascii: characters from the palette.\nedges: ascii with lines "
     "along edges."},
    {1, "Palette",
     "Characters for ascii and edges, densest first.\n\nstandard, detailed "
     "(70 levels), simple, blocks (█▓▒░).\n\nAny other "
     "ramp can be given with --palette or in the config file."},
    {1, "Fit",
     "How the image fills the viewer.\n\ncontain: the whole image.\ncover: "
     "fills the viewer, cropping.\nstretch: fills it, ignoring the shape."},
    {1, "Invert",
     "Flip brightness.\n\ncharcoal assumes a dark terminal; turn this on for "
     "a light one."},
    {2, "Color depth",
     "Colors sent to the terminal.\n\nauto reads COLORTERM and TERM.\ntrue: "
     "24-bit.\n256, 16: the terminal's palette.\nmono: no color."},
    {2, "Dither",
     "Dithering for glyph choice and reduced colors.\n\nfloyd: error "
     "diffusion, the finest.\nbayer: a fixed pattern that stays still in "
     "video."},
    {2, "Brightness", "Added to every pixel, -1 to 1.\n\nb and B change it "
                      "while viewing."},
    {2, "Contrast", "Spread around the middle gray, 0 to 4.\n\nt and T change "
                    "it while viewing."},
    {2, "Gamma", "Above 1 brightens midtones, below 1 darkens them.\n\ng and "
                 "G change it while viewing."},
    {2, "Saturation", "0 is grayscale, above 1 is more vivid.\n\ns and S "
                      "change it while viewing."},
    {3, "Loop", "Repeat animations and video.\n\nR toggles it while "
                "playing."},
    {3, "Max fps",
     "Most frames per second sent to the terminal.\n\nInside tmux, video can "
     "stutter: tmux redraws everything it is sent. 15 or 20 is smoother "
     "there.\n\noff sends every frame."},
};

// Choices for options with a fixed list
static const char *const PALETTE_NAMES[] = {"standard", "detailed", "simple",
                                            "blocks"};
#define PALETTE_COUNT 4
static const int FPS_STEPS[] = {0, 10, 15, 20, 24, 30, 60};
#define FPS_COUNT ((int)(sizeof(FPS_STEPS) / sizeof(FPS_STEPS[0])))

// Prototype
static int tab_first(int tab);
static int tab_count(int tab);
static int cycle(int v, int dir, int n);
static double step(double v, int dir, double by, double lo, double hi);
static void option_value(const App *a, int opt, char *out, size_t n);
static void option_change(App *a, int opt, int dir);
static int wrap_line(const char *text, int width, char *out, size_t n,
                     const char **rest);

static int tab_first(int tab) {
    for (int i = 0; i < OPT_COUNT; i++) {
        if (OPTIONS[i].tab == tab) {
            return i;
        }
    }
    return 0;
}

static int tab_count(int tab) {
    int n = 0;
    for (int i = 0; i < OPT_COUNT; i++) {
        n += OPTIONS[i].tab == tab;
    }
    return n;
}

static int cycle(int v, int dir, int n) {
    return ((v + dir) % n + n) % n;
}

static double step(double v, int dir, double by, double lo, double hi) {
    v = round((v + dir * by) / by) * by;
    return v < lo ? lo : v > hi ? hi : v;
}

static void option_value(const App *a, int opt, char *out, size_t n) {
    const Config *c = &a->config;
    switch (opt) {
    case OPT_THEME:
        snprintf(out, n, "%s", c->theme);
        break;
    case OPT_THEME_BG:
        snprintf(out, n, "%s", c->theme_background ? "True" : "False");
        break;
    case OPT_ROUNDED:
        snprintf(out, n, "%s", c->rounded ? "True" : "False");
        break;
    case OPT_VIM:
        snprintf(out, n, "%s", c->vim_keys ? "True" : "False");
        break;
    case OPT_MODE:
        snprintf(out, n, "%s", glyph_mode_name(c->mode));
        break;
    case OPT_PALETTE:
        snprintf(out, n, "%.60s", palette_from_name(c->palette) ? c->palette
                                                                : "custom");
        break;
    case OPT_FIT:
        snprintf(out, n, "%s", fit_name(c->fit));
        break;
    case OPT_INVERT:
        snprintf(out, n, "%s", c->invert ? "True" : "False");
        break;
    case OPT_COLOR:
        snprintf(out, n, "%s", c->color_auto ? "auto" : color_depth_name(c->color));
        break;
    case OPT_DITHER:
        snprintf(out, n, "%s", dither_mode_name(c->dither));
        break;
    case OPT_BRIGHTNESS:
        snprintf(out, n, "%+.2f", c->brightness);
        break;
    case OPT_CONTRAST:
        snprintf(out, n, "%.2f", c->contrast);
        break;
    case OPT_GAMMA:
        snprintf(out, n, "%.2f", c->gamma);
        break;
    case OPT_SATURATION:
        snprintf(out, n, "%.2f", c->saturation);
        break;
    case OPT_LOOP:
        snprintf(out, n, "%s", c->loop ? "True" : "False");
        break;
    case OPT_MAX_FPS:
        if (c->max_fps > 0) {
            snprintf(out, n, "%d", c->max_fps);
        } else {
            snprintf(out, n, "off");
        }
        break;
    default:
        out[0] = '\0';
    }
}

// Change an option, apply it to the running viewer, and save the config
static void option_change(App *a, int opt, int dir) {
    Config *c = &a->config;
    RenderOpts *o = &a->opts;
    switch (opt) {
    case OPT_THEME: {
        int i = 0;
        while (i < a->theme_count && strcmp(a->theme_names[i], c->theme) != 0) {
            i++;
        }
        i = i == a->theme_count ? 0 : cycle(i, dir, a->theme_count);
        snprintf(c->theme, sizeof(c->theme), "%s", a->theme_names[i]);
        app_apply_theme(a);
        break;
    }
    case OPT_THEME_BG:
        c->theme_background = !c->theme_background;
        app_apply_theme(a);
        break;
    case OPT_ROUNDED:
        c->rounded = !c->rounded;
        app_apply_theme(a);
        break;
    case OPT_VIM:
        c->vim_keys = !c->vim_keys;
        a->vim_keys = c->vim_keys;
        break;
    case OPT_MODE:
        c->mode = (GlyphMode)cycle(c->mode, dir, MODE_COUNT);
        o->mode = c->mode;
        break;
    case OPT_PALETTE: {
        int i = 0;
        while (i < PALETTE_COUNT && strcmp(PALETTE_NAMES[i], c->palette) != 0) {
            i++;
        }
        i = i == PALETTE_COUNT ? 0 : cycle(i, dir, PALETTE_COUNT);
        snprintf(c->palette, sizeof(c->palette), "%s", PALETTE_NAMES[i]);
        o->palette = config_palette(c);
        break;
    }
    case OPT_FIT:
        c->fit = (FitMode)cycle(c->fit, dir, FIT_COUNT);
        a->fit = c->fit;
        break;
    case OPT_INVERT:
        c->invert = !c->invert;
        o->invert = c->invert;
        break;
    case OPT_COLOR: {
        // auto, then each depth
        int v = c->color_auto ? 0 : c->color + 1;
        v = cycle(v, dir, COLOR_COUNT + 1);
        c->color_auto = v == 0;
        if (v > 0) {
            c->color = (ColorDepth)(v - 1);
        }
        o->color = c->color_auto ? a->screen->depth : c->color;
        break;
    }
    case OPT_DITHER:
        c->dither = (DitherMode)cycle(c->dither, dir, DITHER_COUNT);
        o->dither = c->dither;
        break;
    case OPT_BRIGHTNESS:
        c->brightness = step(c->brightness, dir, 0.05, -1, 1);
        o->brightness = a->start_opts.brightness = c->brightness;
        break;
    case OPT_CONTRAST:
        c->contrast = step(c->contrast, dir, 0.1, 0, 4);
        o->contrast = a->start_opts.contrast = c->contrast;
        break;
    case OPT_GAMMA:
        c->gamma = step(c->gamma, dir, 0.1, 0.1, 5);
        o->gamma = a->start_opts.gamma = c->gamma;
        break;
    case OPT_SATURATION:
        c->saturation = step(c->saturation, dir, 0.1, 0, 4);
        o->saturation = a->start_opts.saturation = c->saturation;
        break;
    case OPT_LOOP:
        c->loop = !c->loop;
        a->loop = c->loop;
        if (a->player) {
            player_set_loop(a->player, a->loop);
        }
        break;
    case OPT_MAX_FPS: {
        int i = 0;
        while (i < FPS_COUNT && FPS_STEPS[i] != c->max_fps) {
            i++;
        }
        i = i == FPS_COUNT ? 0 : cycle(i, dir, FPS_COUNT);
        c->max_fps = FPS_STEPS[i];
        a->max_fps = c->max_fps;
        break;
    }
    }
    a->dirty = 1;

    char err[160];
    if (config_save(c, err, sizeof(err)) != 0) {
        snprintf(a->message, sizeof(a->message), "%s", err);
    }
}

// Take as many words of text as fit in width, up to a newline
// Returns the cells used; rest points at what is left (NULL at the end)
static int wrap_line(const char *text, int width, char *out, size_t n,
                     const char **rest) {
    int cells = 0;
    size_t len = 0, cut = 0;
    const char *p = text;
    const char *cut_rest = NULL;
    uint32_t cp;
    int used;
    while ((used = utf8_decode(p, &cp)) > 0 && cp != '\n') {
        if (cells == width) {
            break;
        }
        if (cp == ' ') {
            cut = len;
            cut_rest = p + 1;
        }
        if (len + (size_t)used < n) {
            memcpy(out + len, p, (size_t)used);
            len += (size_t)used;
        }
        p += used;
        cells++;
    }
    if (used > 0 && cp != '\n' && cut_rest) {
        // Too long: break at the last space
        len = cut;
        *rest = cut_rest;
    } else {
        *rest = used > 0 ? p + (cp == '\n' ? 1 : 0) : NULL;
    }
    out[len] = '\0';
    return cells;
}

void view_options(App *a) {
    const Theme *t = a->theme;
    Screen *s = a->screen;
    int cols = s->cols, rows = s->rows;
    int with_logo = rows >= 22 && cols >= 44;
    int top = with_logo ? 6 : 0;
    int w = cols - 4 < 84 ? cols - 4 : 84;
    Rect box = {(cols - w) / 2, top, w, rows - top - 1};
    if (box.w < 40 || box.h < 8) {
        draw_text(s, 0, 0, cols, "Options need at least 44x9 cells.", t->hi_fg,
                  t->main_bg, 0);
        return;
    }
    if (with_logo) {
        view_logo(a, 1);
    }

    draw_fill(s, box, t->main_fg, t->main_bg);
    draw_box(s, box, t, -1, NULL);
    draw_border_label(s, box.x + 2, box.y, "tab→", NULL, t);
    Rect in = rect_inner(box);

    // Tab row: the current one in [brackets], the others with their number
    int tab_w = in.w / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        char label[32];
        int cur = i == a->opt_tab;
        if (cur) {
            snprintf(label, sizeof(label), "[%s]", TABS[i]);
        } else {
            snprintf(label, sizeof(label), "%d%s", i + 1, TABS[i]);
        }
        int lw = text_width(label);
        int x = in.x + i * tab_w + (tab_w - lw) / 2;
        draw_text(s, x, in.y, lw, label, cur ? t->title : t->main_fg, t->main_bg,
                  cur ? ATTR_BOLD : 0);
        if (!cur) {
            draw_text(s, x, in.y, 1, label, t->hi_fg, t->main_bg, ATTR_BOLD);
        }
    }

    // ├───┬───┤ under the tabs, │ between the list and the description
    int div_y = in.y + 1, split_x = in.x + LIST_W;
    for (int x = in.x; x < in.x + in.w; x++) {
        screen_put(s, x, div_y, 0x2500, t->box_border, t->main_bg, 0);
    }
    screen_put(s, box.x, div_y, 0x251C, t->box_border, t->main_bg, 0);
    screen_put(s, box.x + box.w - 1, div_y, 0x2524, t->box_border, t->main_bg, 0);
    screen_put(s, split_x, div_y, 0x252C, t->box_border, t->main_bg, 0);
    for (int y = div_y + 1; y < in.y + in.h; y++) {
        screen_put(s, split_x, y, 0x2502, t->box_border, t->main_bg, 0);
    }
    screen_put(s, split_x, box.y + box.h - 1, 0x2534, t->box_border, t->main_bg,
               0);

    // Option list: name, then value; the selected one is a solid bar
    int first = tab_first(a->opt_tab), count = tab_count(a->opt_tab);
    int list_y = div_y + 1, fit_n = (in.y + in.h - list_y) / 2;
    int scroll = a->opt_sel >= fit_n ? a->opt_sel - fit_n + 1 : 0;
    for (int k = 0; k < fit_n && scroll + k < count; k++) {
        int opt = first + scroll + k, sel = scroll + k == a->opt_sel;
        int y = list_y + k * 2;
        uint32_t fg = sel ? t->selected_fg : t->main_fg;
        uint32_t bg = sel ? t->selected_bg : t->main_bg;
        uint8_t attr = sel && s->depth == COLOR_MONO ? ATTR_REVERSE : 0;
        if (sel) {
            draw_fill(s, (Rect){in.x, y, LIST_W, 2}, fg, bg);
        }
        const char *name = OPTIONS[opt].name;
        int nw = text_width(name);
        draw_text(s, in.x + (LIST_W - nw) / 2, y, nw, name, fg, bg,
                  attr | ATTR_BOLD);
        char value[64];
        option_value(a, opt, value, sizeof(value));
        int vw = text_width(value) > LIST_W - 4 ? LIST_W - 4 : text_width(value);
        draw_text(s, in.x + (LIST_W - vw) / 2, y + 1, vw, value,
                  sel ? fg : t->inactive_fg, bg, attr);
        if (sel) {
            screen_put(s, in.x + 1, y + 1, 0x2190, fg, bg, attr);
            screen_put(s, in.x + LIST_W - 2, y + 1, 0x2192, fg, bg, attr);
        }
    }
    if (count > fit_n) {
        char label[32];
        snprintf(label, sizeof(label), "↑ %d/%d ↓", a->opt_sel + 1,
                 count);
        draw_border_label(s, box.x + 2, box.y + box.h - 1, label, NULL, t);
    }

    // Description of the selected option, wrapped
    const char *text = OPTIONS[first + a->opt_sel].help;
    int dx = split_x + 2, dw = in.x + in.w - dx - 1;
    for (int y = list_y; text && y < in.y + in.h; y++) {
        char line[256];
        wrap_line(text, dw, line, sizeof(line), &text);
        draw_text(s, dx, y, dw, line, t->main_fg, t->main_bg, 0);
    }
}

int options_key(App *a, int key) {
    int count = tab_count(a->opt_tab);
    int vim = a->vim_keys;
    switch (key) {
    case KEY_ESC:
    case KEY_F2:
    case 'o':
    case 'q':
        return 1;
    case KEY_TAB:
        a->opt_tab = cycle(a->opt_tab, 1, TAB_COUNT);
        a->opt_sel = 0;
        break;
    case KEY_BACKTAB:
        a->opt_tab = cycle(a->opt_tab, -1, TAB_COUNT);
        a->opt_sel = 0;
        break;
    case '1':
    case '2':
    case '3':
    case '4':
        a->opt_tab = key - '1';
        a->opt_sel = 0;
        break;
    case KEY_UP:
        a->opt_sel = cycle(a->opt_sel, -1, count);
        break;
    case KEY_DOWN:
        a->opt_sel = cycle(a->opt_sel, 1, count);
        break;
    case KEY_LEFT:
        option_change(a, tab_first(a->opt_tab) + a->opt_sel, -1);
        break;
    case KEY_RIGHT:
    case KEY_ENTER:
        option_change(a, tab_first(a->opt_tab) + a->opt_sel, 1);
        break;
    default:
        if (vim && key == 'k') {
            return options_key(a, KEY_UP);
        }
        if (vim && key == 'j') {
            return options_key(a, KEY_DOWN);
        }
        if (vim && key == 'h') {
            return options_key(a, KEY_LEFT);
        }
        if (vim && key == 'l') {
            return options_key(a, KEY_RIGHT);
        }
        break;
    }
    return 0;
}
