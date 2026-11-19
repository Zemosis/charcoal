#include "test.h"
#include "tui.h"

// Flush s and return the output as a C string (valid until the next call)
static const char *flush_str(Screen *s, Buf *b) {
    buf_clear(b);
    screen_flush(s, b);
    buf_putc(b, '\0');
    return b->data;
}

static void test_flush(void) {
    Buf b;
    buf_init(&b);
    Screen *s = screen_new(4, 2, COLOR_TRUE);
    screen_clear(s, 0xFFFFFF, 0x000000);

    // The first flush clears and draws everything
    const char *out = flush_str(s, &b);
    CHECK(strstr(out, "\033[2J") != NULL);
    CHECK(strncmp(out, "\033[?2026h", 8) == 0);

    // Nothing changed: only the sync markers
    CHECK_STR(flush_str(s, &b), "\033[?2026h\033[?2026l");

    // One changed cell: move there, style it, write it
    screen_put(s, 2, 1, 'x', 0xFF0000, 0x000000, 0);
    CHECK_STR(flush_str(s, &b),
              "\033[?2026h\033[2;3H\033[0;38;2;255;0;0;48;2;0;0;0mx"
              "\033[?2026l");

    // Two neighbors with the same look share one move and one style
    screen_put(s, 0, 0, 'a', 0x00FF00, 0x000000, ATTR_BOLD);
    screen_put(s, 1, 0, 'b', 0x00FF00, 0x000000, ATTR_BOLD);
    CHECK_STR(flush_str(s, &b),
              "\033[?2026h\033[1;1H\033[0;1;38;2;0;255;0;48;2;0;0;0mab"
              "\033[?2026l");

    // 256-color terminals get palette codes; mono gets no colors at all
    s->depth = COLOR_256;
    screen_put(s, 3, 0, 'c', 0xFF0000, 0x000000, 0);
    CHECK_STR(flush_str(s, &b),
              "\033[?2026h\033[1;4H\033[0;38;5;196;48;5;16mc\033[?2026l");
    s->depth = COLOR_MONO;
    screen_put(s, 3, 0, 'd', 0xFF0000, 0x000000, ATTR_REVERSE);
    CHECK_STR(flush_str(s, &b), "\033[?2026h\033[1;4H\033[0;7md\033[?2026l");

    // Short hops on a row move forward, the next row starts with \r\n,
    // and only the color that changed is sent
    s->depth = COLOR_TRUE;
    screen_put(s, 0, 1, 'e', 0x00FF00, 0x000000, 0);
    screen_put(s, 3, 1, 'f', 0x00FF00, 0x0000FF, 0);
    screen_put(s, 0, 0, 'g', 0x00FF00, 0x000000, 0);
    CHECK_STR(flush_str(s, &b),
              "\033[?2026h\033[1;1H\033[0;38;2;0;255;0;48;2;0;0;0mg"
              "\r\ne\033[2C\033[48;2;0;0;255mf\033[?2026l");

    // Small color changes are skipped only with a tolerance
    screen_put(s, 3, 1, 'f', 0x04FB00, 0x0000FF, 0);
    s->tolerance = 8;
    CHECK_STR(flush_str(s, &b), "\033[?2026h\033[?2026l");
    s->tolerance = 0;
    CHECK(strstr(flush_str(s, &b), "38;2;4;251;0") != NULL);

    // Out-of-range puts are ignored, resize forces a full redraw
    screen_put(s, -1, 0, 'z', 0, 0, 0);
    screen_put(s, 4, 0, 'z', 0, 0, 0);
    CHECK(screen_cell(s, 4, 0) == NULL);
    CHECK_INT(screen_resize(s, 6, 3), 0);
    CHECK(strstr(flush_str(s, &b), "\033[2J") != NULL);

    screen_free(s);
    buf_free(&b);
}

static void test_draw(void) {
    const Theme *t = theme_default();
    Screen *s = screen_new(12, 4, COLOR_TRUE);
    screen_clear(s, t->main_fg, t->main_bg);

    // ╭─¹ab─────╮ with rounded corners and the hotkey in the accent color
    draw_box(s, (Rect){0, 0, 10, 3}, t, 1, "ab");
    CHECK_INT(screen_cell(s, 0, 0)->ch, 0x256D);
    CHECK_INT(screen_cell(s, 9, 0)->ch, 0x256E);
    CHECK_INT(screen_cell(s, 0, 2)->ch, 0x2570);
    CHECK_INT(screen_cell(s, 9, 2)->ch, 0x256F);
    CHECK_INT(screen_cell(s, 1, 0)->ch, 0x2500);
    CHECK_INT(screen_cell(s, 2, 0)->ch, 0x00B9);
    CHECK_INT(screen_cell(s, 2, 0)->fg, t->hi_fg);
    CHECK_INT(screen_cell(s, 3, 0)->ch, 'a');
    CHECK_INT(screen_cell(s, 0, 1)->ch, 0x2502);

    Rect in = rect_inner((Rect){0, 0, 10, 3});
    CHECK_INT(in.x, 1);
    CHECK_INT(in.w, 8);
    CHECK_INT(in.h, 1);

    // Text is clipped, control characters are made visible
    CHECK_INT(draw_text(s, 0, 3, 3, "hello", t->main_fg, t->main_bg, 0), 3);
    CHECK_INT(screen_cell(s, 2, 3)->ch, 'l');
    CHECK_INT(screen_cell(s, 3, 3)->ch, ' ');
    draw_text(s, 0, 3, 2, "\x1b" "x", t->main_fg, t->main_bg, 0);
    CHECK_INT(screen_cell(s, 0, 3)->ch, '?');
    CHECK_INT(text_width("▓a×"), 3);

    // ┐label┌ with the hotkey letter lit
    CHECK_INT(draw_border_label(s, 1, 0, "fit", "f", t), 5);
    CHECK_INT(screen_cell(s, 1, 0)->ch, 0x2510);
    CHECK_INT(screen_cell(s, 2, 0)->fg, t->hi_fg);
    CHECK_INT(screen_cell(s, 3, 0)->fg, t->main_fg);
    CHECK_INT(screen_cell(s, 5, 0)->ch, 0x250C);

    // Art with default colors takes the theme's
    Grid *g = grid_new(1, 1);
    draw_grid(s, g, 11, 3, t);
    CHECK_INT(screen_cell(s, 11, 3)->bg, t->main_bg);
    grid_free(g);
    screen_free(s);
}

static void test_wide(void) {
    CHECK_INT(cp_width('a'), 1);
    CHECK_INT(cp_width(0x65E5), 2);     // 日
    CHECK_INT(cp_width(0x1F600), 2);    // an emoji
    CHECK_INT(cp_width(0x0301), 0);     // a combining accent
    CHECK_INT(text_width("\u65e5\u672c.png"), 8);
    CHECK_INT(text_width("e\u0301"), 1);

    Buf b;
    buf_init(&b);
    Screen *s = screen_new(6, 1, COLOR_MONO);
    screen_clear(s, 0, 0);
    flush_str(s, &b);

    // 日 takes two cells: written once, the cursor moves past both
    CHECK_INT(draw_text(s, 0, 0, 6, "\u65e5a", 0, 0, 0), 3);
    CHECK_INT(screen_cell(s, 1, 0)->ch, CELL_WIDE_TAIL);
    CHECK_STR(flush_str(s, &b),
              "\033[?2026h\033[1;1H\033[0m\xe6\x97\xa5" "a\033[?2026l");

    // A wide character that does not fit becomes a space
    screen_clear(s, 0, 0);
    CHECK_INT(draw_text(s, 0, 0, 1, "\u65e5", 0, 0, 0), 1);
    CHECK_INT(screen_cell(s, 0, 0)->ch, ' ');

    // Drawing over its right half redraws the left half as well
    screen_clear(s, 0, 0);
    draw_text(s, 0, 0, 6, "\u65e5a", 0, 0, 0);
    flush_str(s, &b);
    screen_clear(s, 0, 0);
    draw_text(s, 0, 0, 6, " xa", 0, 0, 0);
    CHECK_STR(flush_str(s, &b), "\033[?2026h\033[1;1H\033[0m x\033[?2026l");

    screen_free(s);
    buf_free(&b);
}

void test_screen(void) {
    test_wide();
    test_flush();
    test_draw();
}
