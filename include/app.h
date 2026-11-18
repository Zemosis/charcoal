#ifndef CHARCOAL_APP_H
#define CHARCOAL_APP_H

#include "media.h"
#include "render.h"
#include "tui.h"

#include <stddef.h>

#define CHARCOAL_VERSION "0.1.0-dev"

typedef enum FitMode {
    FIT_CONTAIN,
    FIT_COVER,
    FIT_STRETCH,
    FIT_COUNT
} FitMode;

// Panels, numbered by their hotkeys 1-3
typedef enum Panel {
    PANEL_VIEWER,
    PANEL_INFO,
    PANEL_CONTROLS,
    PANEL_COUNT
} Panel;

typedef enum Overlay {
    OVERLAY_NONE,
    OVERLAY_HELP
} Overlay;

// Rows of the controls panel, top to bottom
typedef enum Control {
    CTL_MODE,
    CTL_PALETTE,
    CTL_COLOR,
    CTL_DITHER,
    CTL_INVERT,
    CTL_FIT,
    CTL_BRIGHTNESS,
    CTL_CONTRAST,
    CTL_GAMMA,
    CTL_SATURATION,
    CTL_COUNT
} Control;

// The interactive viewer's whole state
typedef struct App {
    // Input
    char name[256];             // file name shown in the UI
    char codec[32];
    Frame *frame;               // full resolution, decoded once

    // Settings
    RenderOpts opts;
    RenderOpts start_opts;      // what r resets the adjustments to
    const char *custom_palette; // from --palette, if not a built-in
    FitMode fit;
    double zoom;                // 1 = the whole image fits
    double pan_x;               // center of the view, in image pixels
    double pan_y;
    int vim_keys;

    // Screen
    const Theme *theme;
    Screen *screen;
    double cell_aspect;
    int show[PANEL_COUNT];
    int compact;                // too small for side panels: status line
    Rect r_viewer;              // zero width when hidden
    Rect r_info;
    Rect r_controls;
    Rect r_status;
    int selected;               // highlighted control row
    int scroll;                 // first control row shown
    Overlay overlay;
    int help_page;
    char message[96];           // one-off note shown in the viewer border
    int running;

    // Render cache
    int dirty;                  // art must be rendered again
    Grid *art;
    int art_x;
    int art_y;
    uint8_t *pixels;            // the view resized, before adjustments
    uint8_t *work;              // a copy the adjustments run on
    size_t pixels_cap;
    int pixels_key[6];          // crop and size the pixels were made for
    double render_ms;
} App;

// Run the viewer on a file until the user quits, returns the exit code
int app_run(const char *path, const RenderOpts *opts, ColorDepth ui_depth);

// Resize and render the viewer's art if settings changed
void viewer_update(App *a);

// Panels and overlays, drawn into a->screen
void view_viewer(App *a);
void view_info(App *a);
void view_controls(App *a);
void view_status(App *a);
void view_help(App *a);

// Step a control's value by dir (-1 or +1)
void controls_change(App *a, int control, int dir);

// Name of a fit mode
const char *fit_name(FitMode f);

// Help overlay: handle a key, returns 1 when the overlay should close
int help_key(App *a, int key);

#endif // CHARCOAL_APP_H
