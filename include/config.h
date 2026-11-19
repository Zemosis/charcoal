#ifndef CHARCOAL_CONFIG_H
#define CHARCOAL_CONFIG_H

#include "buf.h"
#include "render.h"

// Fit modes, shared by the config and the viewer
typedef enum FitMode {
    FIT_CONTAIN,
    FIT_COVER,
    FIT_STRETCH,
    FIT_COUNT
} FitMode;

// Settings kept in ~/.config/charcoal/charcoal.conf
typedef struct Config {
    // General
    char theme[64];
    int theme_background;   // paint the theme's background, or keep the
                            // terminal's own
    int rounded;            // rounded box corners
    int vim_keys;
    int mouse;              // clicks, wheel and drags in the viewer

    // Render defaults
    GlyphMode mode;
    char palette[256];      // a built-in name or a custom ramp
    FitMode fit;
    int invert;

    // Color defaults
    int color_auto;         // pick the color depth from the terminal
    ColorDepth color;
    DitherMode dither;
    double brightness;
    double contrast;
    double gamma;
    double saturation;

    // Playback
    int loop;
    int max_fps;            // 0 = as many as the video has
} Config;

// The defaults from the spec
void config_defaults(Config *c);

// Read "key = value" lines over c; unknown keys and bad values are skipped
// with a note in warn (empty when all was fine)
void config_parse(const char *text, Config *c, char *warn, int warn_len);

// Write every setting as "key = value" lines
void config_write(const Config *c, Buf *out);

// $XDG_CONFIG_HOME/charcoal (or ~/.config/charcoal) plus name; -1 if no home
int config_path(const char *name, char *out, int n);

// Defaults, then the config file if there is one; returns 0 or -1 with warn
int config_load(Config *c, char *warn, int warn_len);

// Write the config file (creating the folder), returns 0 or -1 with err
int config_save(const Config *c, char *err, int err_len);

// Palette ramp for a config value: a built-in by name, else the text itself
const char *config_palette(const Config *c);

const char *fit_name(FitMode f);
int fit_from_name(const char *name);

#endif // CHARCOAL_CONFIG_H
