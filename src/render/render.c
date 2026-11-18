#include "render.h"

#include <stdlib.h>
#include <string.h>

// Palette ramps, darkest (densest) first, as in ascii_art
const char PALETTE_STANDARD[] = "@%#*+=-:. ";
const char PALETTE_DETAILED[] =
    "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. ";
const char PALETTE_SIMPLE[] = "#*+:. ";
const char PALETTE_BLOCKS[] = "█▓▒░ ";

static const char *MODE_NAMES[MODE_COUNT] = {
    "ascii", "half", "quad", "braille", "edges",
};

static const char *COLOR_NAMES[COLOR_COUNT] = {
    "true", "256", "16", "mono",
};

static const char *DITHER_NAMES[DITHER_COUNT] = {
    "none", "floyd", "bayer",
};

void render_opts_default(RenderOpts *o) {
    o->mode = MODE_HALF;
    o->color = COLOR_TRUE;
    o->palette = PALETTE_STANDARD;
    o->invert = 0;
    o->dither = DITHER_NONE;
    o->brightness = 0.0;
    o->contrast = 1.0;
    o->gamma = 1.0;
    o->saturation = 1.0;
    o->cell_aspect = 2.0;
}

int glyph_mode_from_name(const char *name) {
    for (int i = 0; i < MODE_COUNT; i++) {
        if (strcmp(name, MODE_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

int color_depth_from_name(const char *name) {
    for (int i = 0; i < COLOR_COUNT; i++) {
        if (strcmp(name, COLOR_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

int dither_mode_from_name(const char *name) {
    for (int i = 0; i < DITHER_COUNT; i++) {
        if (strcmp(name, DITHER_NAMES[i]) == 0) {
            return i;
        }
    }
    return -1;
}

const char *glyph_mode_name(GlyphMode mode) {
    return (mode >= 0 && mode < MODE_COUNT) ? MODE_NAMES[mode] : "?";
}

const char *color_depth_name(ColorDepth depth) {
    return (depth >= 0 && depth < COLOR_COUNT) ? COLOR_NAMES[depth] : "?";
}

const char *dither_mode_name(DitherMode d) {
    return (d >= 0 && d < DITHER_COUNT) ? DITHER_NAMES[d] : "?";
}

const char *palette_from_name(const char *name) {
    if (strcmp(name, "standard") == 0) {
        return PALETTE_STANDARD;
    }
    if (strcmp(name, "detailed") == 0) {
        return PALETTE_DETAILED;
    }
    if (strcmp(name, "simple") == 0) {
        return PALETTE_SIMPLE;
    }
    if (strcmp(name, "blocks") == 0) {
        return PALETTE_BLOCKS;
    }
    return NULL;
}

Grid *grid_new(int cols, int rows) {
    if (cols < 1 || rows < 1) {
        return NULL;
    }
    Grid *g = malloc(sizeof(*g));
    if (!g) {
        return NULL;
    }
    g->cols = cols;
    g->rows = rows;
    g->cells = calloc((size_t)cols * (size_t)rows, sizeof(Cell));
    if (!g->cells) {
        free(g);
        return NULL;
    }
    for (int i = 0; i < cols * rows; i++) {
        g->cells[i].ch = ' ';
        g->cells[i].fg = CELL_COLOR_DEFAULT;
        g->cells[i].bg = CELL_COLOR_DEFAULT;
    }
    return g;
}

void grid_free(Grid *g) {
    if (g) {
        free(g->cells);
        free(g);
    }
}

void mode_subpixels(GlyphMode mode, int *sub_w, int *sub_h) {
    switch (mode) {
    case MODE_HALF:
        *sub_w = 1;
        *sub_h = 2;
        break;
    case MODE_QUAD:
        *sub_w = 2;
        *sub_h = 2;
        break;
    case MODE_BRAILLE:
        *sub_w = 2;
        *sub_h = 4;
        break;
    default:
        *sub_w = 1;
        *sub_h = 1;
        break;
    }
}

uint8_t rgb_luma(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((299 * r + 587 * g + 114 * b) / 1000);
}

int render_frame(const Frame *f, const RenderOpts *o, Grid *g) {
    int sub_w, sub_h;
    mode_subpixels(o->mode, &sub_w, &sub_h);
    int w = g->cols * sub_w;
    int h = g->rows * sub_h;

    uint8_t *px = malloc((size_t)w * (size_t)h * 3);
    if (!px) {
        return -1;
    }
    if (resize_rgb(f->rgb, f->width, f->height, px, w, h) != 0) {
        free(px);
        return -1;
    }
    adjust_rgb(px, w * h, o);
    int rc = glyph_render(px, o, g);
    free(px);
    return rc;
}
