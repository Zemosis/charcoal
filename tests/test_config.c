#include "config.h"
#include "test.h"
#include "tui.h"

static void test_parse(void) {
    Config c;
    char warn[256];
    config_defaults(&c);
    CHECK_STR(c.theme, "gruvbox_dark");
    CHECK_INT(c.mode, MODE_HALF);
    CHECK_INT(c.color_auto, 1);

    config_parse("# comment\n"
                 "theme = \"nord\"   # trailing comment\n"
                 "mode = braille\n"
                 "palette = \" .:#\"\n"
                 "color = 256\n"
                 "dither=floyd\n"
                 "contrast = 1.5\n"
                 "vim_keys = false\n"
                 "max_fps = 20\n"
                 "fit = cover\n",
                 &c, warn, sizeof(warn));
    CHECK_STR(warn, "");
    CHECK_STR(c.theme, "nord");
    CHECK_INT(c.mode, MODE_BRAILLE);
    CHECK_STR(c.palette, " .:#");
    CHECK_STR(config_palette(&c), " .:#");
    CHECK_INT(c.color_auto, 0);
    CHECK_INT(c.color, COLOR_256);
    CHECK_INT(c.dither, DITHER_FLOYD);
    CHECK(c.contrast == 1.5);
    CHECK_INT(c.vim_keys, 0);
    CHECK_INT(c.max_fps, 20);
    CHECK_INT(c.fit, FIT_COVER);

    // Bad lines are reported and leave the value alone
    config_parse("mode = sideways\nwhat = 1\ngamma = 9\nnonsense\n", &c, warn,
                 sizeof(warn));
    CHECK_INT(c.mode, MODE_BRAILLE);
    CHECK(c.gamma == 1.0);
    CHECK(strstr(warn, "line 1: bad value for mode") != NULL);
    CHECK(strstr(warn, "line 2: unknown key what") != NULL);
    CHECK(strstr(warn, "line 3: bad value for gamma") != NULL);
    CHECK(strstr(warn, "line 4") != NULL);

    // A built-in palette name gives the built-in ramp
    config_parse("palette = detailed\n", &c, warn, sizeof(warn));
    CHECK(config_palette(&c) == PALETTE_DETAILED);
}

static void test_round_trip(void) {
    Config a, b;
    char warn[256];
    config_defaults(&a);
    snprintf(a.theme, sizeof(a.theme), "tokyo_night");
    a.mode = MODE_QUAD;
    a.color_auto = 0;
    a.color = COLOR_16;
    a.brightness = -0.25;
    a.saturation = 1.7;
    a.loop = 0;
    a.rounded = 0;

    Buf out;
    buf_init(&out);
    config_write(&a, &out);
    buf_putc(&out, '\0');
    config_defaults(&b);
    config_parse(out.data, &b, warn, sizeof(warn));
    CHECK_STR(warn, "");
    CHECK_STR(b.theme, "tokyo_night");
    CHECK_INT(b.mode, MODE_QUAD);
    CHECK_INT(b.color_auto, 0);
    CHECK_INT(b.color, COLOR_16);
    CHECK(b.brightness == -0.25);
    CHECK(b.saturation == 1.7);
    CHECK_INT(b.loop, 0);
    CHECK_INT(b.rounded, 0);
    buf_free(&out);
}

static void test_theme(void) {
    CHECK_INT(theme_builtin_count(), 5);
    CHECK_STR(theme_builtin(1)->name, "nord");
    CHECK_STR(theme_builtin(4)->name, "tty");
    CHECK(theme_builtin(4)->main_bg == CELL_COLOR_DEFAULT);

    Theme t = *theme_default();
    char warn[96];
    CHECK_INT(theme_parse("main_bg = \"#102030\"\n"
                          "hi_fg = 9   # palette red\n"
                          "selected_bg = default\n",
                          &t, warn, sizeof(warn)), 0);
    CHECK_INT(t.main_bg, 0x102030);
    CHECK_INT(t.hi_fg, CELL_COLOR_INDEX | 9);
    CHECK(t.selected_bg == CELL_COLOR_DEFAULT);
    CHECK_INT(t.main_fg, theme_default()->main_fg);

    CHECK_INT(theme_parse("main_fg = \"#12\"\n", &t, warn, sizeof(warn)), -1);
    CHECK_STR(warn, "bad color for main_fg");
    CHECK_INT(theme_parse("glow = \"#123456\"\n", &t, warn, sizeof(warn)), -1);
    CHECK_STR(warn, "unknown key glow");
}

static void test_html(void) {
    Grid *g = grid_new(3, 1);
    g->cells[0] = (Cell){'<', 0xFF0000, CELL_COLOR_DEFAULT, 0};
    g->cells[1] = (Cell){'a', 0xFF0000, CELL_COLOR_DEFAULT, 0};
    g->cells[2] = (Cell){0x2580, CELL_COLOR_INDEX | 21, 0x000000, 0};
    Buf b;
    buf_init(&b);
    export_html(g, &b, 0xd4be98, 0x282828);
    buf_putc(&b, '\0');
    CHECK(strstr(b.data, "background:#282828") != NULL);
    // One span for the two red cells, the < escaped; palette colors as RGB
    CHECK(strstr(b.data, "<pre><span style=\"color:#ff0000\">&lt;a</span>"
                         "<span style=\"color:#0000ff;background:#000000\">"
                         "\xe2\x96\x80</span>\n</pre>") != NULL);
    buf_free(&b);
    grid_free(g);
}

static void test_graph(void) {
    const Theme *t = theme_default();
    Screen *s = screen_new(2, 2, COLOR_TRUE);
    screen_clear(s, t->main_fg, t->main_bg);

    // Bars of 0, 3/8, 1 and 1/8 of 8 dots high
    double v[4] = {0.0, 0.375, 1.0, 0.125};
    draw_braille_graph(s, (Rect){0, 0, 2, 2}, v, t);
    // Bottom row: left bar empty, right bar 3 dots; then full + 1 dot
    CHECK_INT(screen_cell(s, 0, 1)->ch, 0x2800 + 0x80 + 0x20 + 0x10);
    CHECK_INT(screen_cell(s, 0, 0)->ch, ' ');
    CHECK_INT(screen_cell(s, 1, 1)->ch, 0x2800 + 0x40 + 0x04 + 0x02 + 0x01 + 0x80);
    CHECK_INT(screen_cell(s, 1, 0)->ch, 0x2800 + 0x40 + 0x04 + 0x02 + 0x01);
    CHECK_INT(screen_cell(s, 1, 1)->fg, t->graph_mid);
    CHECK_INT(screen_cell(s, 1, 0)->fg, t->graph_high);
    screen_free(s);
}

void test_config(void) {
    test_parse();
    test_round_trip();
    test_theme();
    test_html();
    test_graph();
}
