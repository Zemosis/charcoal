#ifndef CHARCOAL_TUI_H
#define CHARCOAL_TUI_H

#include "buf.h"
#include "render.h"

#include <stddef.h>
#include <stdint.h>

// Extra cell attribute, used by the screen on top of the render ones
#define ATTR_REVERSE 0x04

// Keys that are not plain characters; above the Unicode range
enum {
    KEY_NONE = 0,
    KEY_CTRL_C = 3,
    KEY_TAB = 9,
    KEY_ENTER = 13,
    KEY_CTRL_R = 18,
    KEY_CTRL_Z = 26,
    KEY_ESC = 27,
    KEY_BACKSPACE = 127,
    KEY_UP = 0x110000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_SHIFT_UP,
    KEY_SHIFT_DOWN,
    KEY_SHIFT_LEFT,
    KEY_SHIFT_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGE_UP,
    KEY_PAGE_DOWN,
    KEY_DELETE,
    KEY_F1,
    KEY_F2
};

// What term_wait woke up for, as bits
#define TERM_EV_INPUT  0x01
#define TERM_EV_RESIZE 0x02
#define TERM_EV_RESUME 0x04
#define TERM_EV_QUIT   0x08
#define TERM_EV_EXTRA  0x10

typedef struct Rect {
    int x;
    int y;
    int w;
    int h;
} Rect;

// UI colors, 0xRRGGBB; the art keeps the image's own colors
typedef struct Theme {
    const char *name;
    uint32_t main_bg;
    uint32_t main_fg;
    uint32_t title;
    uint32_t hi_fg;         // hotkeys and accents
    uint32_t selected_bg;
    uint32_t selected_fg;
    uint32_t inactive_fg;   // labels and dim text
    uint32_t box_border;
    uint32_t graph_low;
    uint32_t graph_mid;
    uint32_t graph_high;
} Theme;

// Double-buffered cells: draw into back, flush writes only what changed
typedef struct Screen {
    int cols;
    int rows;
    Cell *front;            // what the terminal shows
    Cell *back;             // the next frame
    ColorDepth depth;       // what the terminal can show
    int full;               // redraw everything on the next flush
    int tolerance;          // skip cells whose colors moved this little
} Screen;

// The built-in default theme, gruvbox dark
const Theme *theme_default(void);

// Enter raw mode and the alternate screen, -1 if stdin/stdout is no terminal
int term_init(void);

// Leave raw mode and the alternate screen; safe to call more than once
void term_restore(void);

// Stop the process for Ctrl+Z, restoring the terminal while stopped
void term_suspend(void);

// Terminal size in cells and the cell height / width ratio
void term_size(int *cols, int *rows, double *cell_aspect);

// Block until input, a signal, extra_fd turning readable (-1 for none), or
// timeout_ms (-1 waits forever); returns TERM_EV_* bits
int term_wait(int timeout_ms, int extra_fd);

// Next decoded key from input already read, KEY_NONE when there is none
int term_read_key(void);

// Write all of b to the terminal
void term_write(const Buf *b);

// Allocate a screen of blank cells, NULL on failure
Screen *screen_new(int cols, int rows, ColorDepth depth);

// Change the size; everything is redrawn on the next flush
int screen_resize(Screen *s, int cols, int rows);

void screen_free(Screen *s);

// Fill the back buffer with spaces in the given colors
void screen_clear(Screen *s, uint32_t fg, uint32_t bg);

// Cell at x, y in the back buffer, NULL when outside the screen
Cell *screen_cell(Screen *s, int x, int y);

// Set one cell, ignored when outside the screen
void screen_put(Screen *s, int x, int y, uint32_t ch, uint32_t fg,
                uint32_t bg, uint8_t attr);

// Append the escape codes that bring the terminal up to date to out
void screen_flush(Screen *s, Buf *out);

// Fill a rectangle with spaces
void draw_fill(Screen *s, Rect r, uint32_t fg, uint32_t bg);

// Draw UTF-8 text clipped to max_w cells, returns the cells used
int draw_text(Screen *s, int x, int y, int max_w, const char *text,
              uint32_t fg, uint32_t bg, uint8_t attr);

// Text width in cells (one per code point; no wide characters yet)
int text_width(const char *text);

// Rounded box with "¹title" in the top border, btop style
void draw_box(Screen *s, Rect r, const Theme *t, int hotkey,
              const char *title);

// A "┐label┌" button in a box's top or bottom border, returns its width
// The first occurrence of hotkey in label is drawn in the accent color
int draw_border_label(Screen *s, int x, int y, const char *label,
                      char hotkey, const Theme *t);

// Copy rendered art onto the screen; default colors become the theme's
void draw_grid(Screen *s, const Grid *g, int x, int y, const Theme *t);

// The inside of a box
Rect rect_inner(Rect r);

#endif // CHARCOAL_TUI_H
