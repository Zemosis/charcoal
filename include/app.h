#ifndef CHARCOAL_APP_H
#define CHARCOAL_APP_H

#include "config.h"
#include "media.h"
#include "render.h"
#include "tui.h"

#include <stddef.h>

#define CHARCOAL_VERSION "0.1.0-dev"

// Panels, numbered by their hotkeys 1-5
typedef enum Panel {
    PANEL_VIEWER,
    PANEL_INFO,
    PANEL_CONTROLS,
    PANEL_HISTOGRAM,
    PANEL_TIMELINE,
    PANEL_COUNT
} Panel;

typedef enum Overlay {
    OVERLAY_NONE,
    OVERLAY_HELP,
    OVERLAY_OPTIONS
} Overlay;

#define MAX_THEMES 64

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
    MediaKind kind;
    int src_w;                  // full resolution of the media
    int src_h;
    int64_t duration_us;        // 0 when unknown
    double fps;
    Frame *frame;               // on screen: full size for stills, scaled
                                // to the view for animations and video
    int frame_serial;           // bumps whenever frame changes

    // Playback (animations and video only)
    Player *player;
    int playing;
    int loop;
    double speed;
    int64_t clock_play_us;      // playback time at clock_wall_ms
    double clock_wall_ms;
    int need_first;             // show the next frame that arrives, now
    int dropped;                // frames skipped to keep up
    int frame_drawn;            // the frame on screen reached the terminal
    int limited;                // the max fps limit held the frame back
    int awaiting_ack;           // a frame went out, the terminal has not
    double ack_sent_ms;         // answered yet
    double term_lag_ms;         // how long the terminal takes per frame
    int frames_out;             // frames sent in the current second
    double fps_window_ms;
    double out_fps;             // frames per second the terminal shows
    int want_w;                 // frame size asked of the player
    int want_h;
    int target_changed;

    // Settings
    Config config;              // what the options overlay saves
    Theme theme_data;           // the loaded theme a->theme points at
    char theme_names[MAX_THEMES][64];
    int theme_count;
    int max_fps;                // 0 = every frame
    double last_frame_ms;       // when the last video frame went out
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
    Rect r_timeline;
    Rect r_histogram;
    double histogram[256];      // share of pixels per luminance, of the view
    int selected;               // highlighted control row
    int scroll;                 // first control row shown
    Overlay overlay;
    int help_page;
    int opt_tab;                // options overlay: tab and selected row
    int opt_sel;
    int prompting;              // typing a file name to export to
    char prompt[256];
    char message[256];          // one-off note shown in the viewer border
    int running;

    // Render cache
    int dirty;                  // art must be rendered again
    Grid *art;
    int art_x;
    int art_y;
    uint8_t *pixels;            // the view resized, before adjustments
    uint8_t *work;              // a copy the adjustments run on
    size_t pixels_cap;
    int pixels_key[7];          // crop, size and frame the pixels came from
    double render_ms;
} App;

// Run the viewer on a file until the user quits, returns the exit code
// opts, fit and loop are this run's settings (config plus command line);
// config is what the options overlay edits and saves; warn is shown first
int app_run(const char *path, const RenderOpts *opts, FitMode fit, int loop,
            const Config *config, ColorDepth ui_depth, const char *warn);

// Resize and render the viewer's art if settings changed
void viewer_update(App *a);

// Panels and overlays, drawn into a->screen
void view_viewer(App *a);
void view_info(App *a);
void view_controls(App *a);
void view_status(App *a);
void view_help(App *a);
void view_timeline(App *a);
void view_histogram(App *a);
void view_options(App *a);

// The charcoal logo and version, centered, 4 rows from y
void view_logo(App *a, int y);

// Options overlay: handle a key, returns 1 when the overlay should close
int options_key(App *a, int key);

// Load the theme named in the config and apply the general settings
void app_apply_theme(App *a);

// Time as m:ss (or h:mm:ss), with tenths when short is set
void format_time(int64_t us, int tenths, char *out, size_t n);

// Step a control's value by dir (-1 or +1)
void controls_change(App *a, int control, int dir);

// Help overlay: handle a key, returns 1 when the overlay should close
int help_key(App *a, int key);

#endif // CHARCOAL_APP_H
