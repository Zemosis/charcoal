#ifndef CHARCOAL_RENDER_H
#define CHARCOAL_RENDER_H

#include "buf.h"
#include "media.h"

#include <stdint.h>

// Marks a cell color as "use the terminal's own color"
#define CELL_COLOR_DEFAULT 0xFF000000u

// Marks a cell color as an xterm palette index (low byte) instead of RGB
#define CELL_COLOR_INDEX 0x01000000u

// Cell attribute bits
#define ATTR_BOLD 0x01
#define ATTR_DIM  0x02

typedef enum GlyphMode {
    MODE_ASCII,
    MODE_HALF,
    MODE_QUAD,
    MODE_BRAILLE,
    MODE_EDGES,
    MODE_COUNT
} GlyphMode;

typedef enum ColorDepth {
    COLOR_TRUE,
    COLOR_256,
    COLOR_16,
    COLOR_MONO,
    COLOR_COUNT
} ColorDepth;

typedef enum DitherMode {
    DITHER_NONE,
    DITHER_FLOYD,
    DITHER_BAYER,
    DITHER_COUNT
} DitherMode;

// One terminal cell
typedef struct Cell {
    uint32_t ch;        // Unicode code point
    uint32_t fg;        // 0xRRGGBB, CELL_COLOR_INDEX | n, or CELL_COLOR_DEFAULT
    uint32_t bg;        // same encoding as fg
    uint8_t attr;       // ATTR_* bits
} Cell;

// A rectangle of cells, row-major
typedef struct Grid {
    int cols;
    int rows;
    Cell *cells;
} Grid;

// Settings that control how a frame becomes cells
typedef struct RenderOpts {
    GlyphMode mode;
    ColorDepth color;
    const char *palette;    // UTF-8 ramp, darkest first
    int invert;             // flip brightness, for light terminal backgrounds
    DitherMode dither;
    double brightness;      // -1 to 1, 0 leaves it unchanged
    double contrast;        // 0 to 4, 1 leaves it unchanged
    double gamma;           // 0.1 to 5, above 1 brightens midtones
    double saturation;      // 0 to 4, 0 is grayscale
    double cell_aspect;     // cell height / width, for edge angles
} RenderOpts;

// Built-in palette ramps
extern const char PALETTE_STANDARD[];
extern const char PALETTE_DETAILED[];
extern const char PALETTE_SIMPLE[];
extern const char PALETTE_BLOCKS[];

// Fill opts with the defaults from the spec
void render_opts_default(RenderOpts *o);

// Name lookups for command-line parsing, -1 if unknown
int glyph_mode_from_name(const char *name);
int color_depth_from_name(const char *name);
int dither_mode_from_name(const char *name);
const char *glyph_mode_name(GlyphMode mode);
const char *color_depth_name(ColorDepth depth);
const char *dither_mode_name(DitherMode d);

// Look up a built-in palette by name, NULL if unknown
const char *palette_from_name(const char *name);

// Allocate a grid of blank cells, NULL on failure
Grid *grid_new(int cols, int rows);

// Free a grid
void grid_free(Grid *g);

// How many image sub-pixels one cell holds in this mode
void mode_subpixels(GlyphMode mode, int *sub_w, int *sub_h);

// Luminance 0-255 of an RGB color (Rec. 601 weights, as in ascii_art)
uint8_t rgb_luma(uint8_t r, uint8_t g, uint8_t b);

// Pick a cell count that fits an image inside max_cols x max_rows
// cell_aspect is cell height / cell width (2.0 on most terminals)
// Never picks more sub-pixels than the image has, so it does not upscale
void render_fit(int img_w, int img_h, int max_cols, int max_rows,
                double cell_aspect, GlyphMode mode, int *cols, int *rows);

// Cells for a fixed width or height, keeping the image's aspect ratio
int render_rows_for_cols(int img_w, int img_h, int cols, double cell_aspect);
int render_cols_for_rows(int img_w, int img_h, int rows, double cell_aspect);

// Resize RGB24 src to dst: area average when shrinking, nearest when growing
// Returns 0 on success, -1 if out of memory
int resize_rgb(const uint8_t *src, int src_w, int src_h, uint8_t *dst,
               int dst_w, int dst_h);

// Same, for the src_w x src_h region at x0, y0 of an image stride pixels wide
int resize_rgb_rect(const uint8_t *src, int stride, int x0, int y0, int src_w,
                    int src_h, uint8_t *dst, int dst_w, int dst_h);

// Apply saturation, then brightness, contrast and gamma to n RGB pixels
void adjust_rgb(uint8_t *rgb, int n, const RenderOpts *o);

// 4x4 ordered dither thresholds, 0-15
extern const int DITHER_BAYER4[4][4];

// Quantize a w x h luminance plane to levels steps (0 to levels - 1)
// DITHER_NONE uses equal-width bins, the others round to the nearest level
void dither_levels(const uint8_t *luma, int w, int h, int levels,
                   DitherMode d, uint8_t *out);

// RGB of an xterm palette index (0-255)
uint32_t color_index_rgb(int index);

// Set what one of the 16 terminal colors really is (from the terminal)
void color_set_ansi16(int index, uint32_t rgb);

// Nearest palette color as CELL_COLOR_INDEX | index (256 skips 0-15)
uint32_t color_nearest(uint32_t rgb, ColorDepth depth);

// Replace a w x h plane of RGB colors with palette indices, optionally dithered
// CELL_COLOR_DEFAULT entries are left alone
void color_quantize(uint32_t *colors, int w, int h, ColorDepth depth,
                    DitherMode d);

// Map pixels already resized to grid size x sub-pixels into cells
// Returns 0 on success, -1 if out of memory
int glyph_render(const uint8_t *rgb, const RenderOpts *o, Grid *g);

// Resize and map a frame into g (whose size picks the output size)
// Returns 0 on success, -1 if out of memory
int render_frame(const Frame *f, const RenderOpts *o, Grid *g);

// Write a grid as text with ANSI color codes, one line per row
void export_ansi(const Grid *g, Buf *out);

// Write a grid as plain text with no escape codes
void export_txt(const Grid *g, Buf *out);

// Write a grid as an HTML page: a <pre> of colored spans; fg and bg are the
// page colors used for cells with the terminal's default colors
void export_html(const Grid *g, Buf *out, uint32_t fg, uint32_t bg);

#endif // CHARCOAL_RENDER_H
