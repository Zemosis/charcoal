#ifndef CHARCOAL_APP_H
#define CHARCOAL_APP_H

#include "config.h"
#include "files.h"
#include "media.h"
#include "render.h"
#include "tui.h"

#include <stddef.h>

#define CHARCOAL_VERSION "0.1.1"

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
    OVERLAY_OPTIONS,
    OVERLAY_MENU,
    OVERLAY_PICKER
} Overlay;

// Esc menu entries; menu_key also returns MENU_CLOSE or -1 (stay open)
enum {
    MENU_OPTIONS,
    MENU_HELP,
    MENU_QUIT,
    MENU_COUNT,
    MENU_CLOSE = 100
};

#define MAX_THEMES 64
#define FRAME_HISTORY 256

// Width of the label column in the controls panel
#define CONTROLS_LABEL_W 12

// What a held mouse button is doing
enum {
    DRAG_NONE,
    DRAG_PAN,       // moving a zoomed image
    DRAG_SEEK       // scrubbing the timeline
};

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
    char path[4096];            // the file as it was opened
    char name[256];             // file name shown in the UI
    int file_index;             // position in its folder, 1-based
    int file_count;             // media files in that folder
    char codec[32];
    MediaKind kind;
    int src_w;                  // full resolution of the media
    int src_h;
    int64_t duration_us;        // 0 when unknown
    double fps;
    Frame *proxy;               // a still shrunk to about twice what the
    int proxy_serial;           // screen can show, which most views resize
    int proxy_cols;             // from; made for this frame and screen
    int proxy_rows;
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
    int palette_changed;        // the terminal reported its real colors
    uint32_t term_fg;           // the terminal's own text and background
    uint32_t term_bg;           // colors, CELL_COLOR_DEFAULT until it says
    int awaiting_ack;           // a frame went out, the terminal has not
    double ack_sent_ms;         // answered yet
    double term_lag_ms;         // how long the terminal takes per frame
    int frames_out;             // frames sent in the current second
    double fps_window_ms;
    double out_fps;             // frames per second the terminal shows
    double frame_ms[FRAME_HISTORY];  // render + send time of recent frames
    int frame_ms_count;
    int frame_ms_head;          // where the next one goes
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
    int menu_sel;

    // File picker (O): a folder listing filtered by what was typed
    char picker_dir[4096];
    FileList picker_list;
    int *picker_view;           // indices into picker_list that match
    int picker_count;
    int picker_sel;
    int picker_scroll;
    char picker_filter[64];
    int prompting;              // the export dialog is open
    char prompt[256];           // its file name
    char export_dir[1024];      // its folder, ~ allowed
    int export_field;           // 0 = typing the file name, 1 = the folder
    int picker_folders;         // the picker is choosing the export folder
    Grid *export_art;           // the view when e was pressed
    int export_resume;          // playback was on before the dialog
    char message[256];          // one-off note shown in the viewer border
    int running;

    // Render cache
    int dirty;                  // art must be rendered again
    Grid *art;
    int art_x;
    int art_y;
    double vis_x;               // the part of the image on screen, in its
    double vis_y;               // own pixels
    double vis_w;
    double vis_h;

    // Mouse: where clickable things were last drawn, and what a drag does
    Rect hit_play;
    Rect hit_bar;
    Rect hit_slower;
    Rect hit_faster;
    Rect hit_repeat;
    Rect hit_opt_tabs;
    Rect hit_opt_list;
    int opt_scroll;
    Rect hit_menu;
    Rect hit_picker;
    Rect hit_export_dir;        // the export dialog's fields and button
    Rect hit_export_file;
    Rect hit_export_browse;
    int drag;                   // DRAG_*
    int drag_x;
    int drag_y;
    double drag_pan_x;
    double drag_pan_y;
    double last_click_ms;       // for double clicks in the picker
    int last_click_row;
    uint8_t *pixels;            // the view resized, before adjustments
    uint8_t *work;              // a copy the adjustments run on
    size_t pixels_cap;
    int pixels_key[8];          // crop, size and source of the pixels
    double render_ms;
} App;

// Run the viewer on a file until the user quits, returns the exit code
// opts, fit and loop are this run's settings (config plus command line);
// config is what the options overlay edits and saves; warn is shown first;
// picker opens the file picker on top, for charcoal <folder>; a NULL path
// starts with no file and the picker on the current folder
int app_run(const char *path, const RenderOpts *opts, FitMode fit, int loop,
            const Config *config, ColorDepth ui_depth, const char *warn,
            int picker);

// Open another file in place of the current one; on failure the current
// one stays and the reason is put in the message. Returns 0 or -1
int app_open_file(App *a, const char *path);

// File picker: open it on a folder, draw it, handle a key (1 = close)
void picker_open(App *a, const char *dir);
void picker_close(App *a);
void view_picker(App *a);
int picker_key(App *a, int key);

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
void view_menu(App *a);
void view_export(App *a);

// Ctrl+O in the export dialog: pick its folder in the file picker
void export_browse(App *a);

// Esc menu: handle a key, returns a MENU_* entry, MENU_CLOSE, or -1
int menu_key(App *a, int key);

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
