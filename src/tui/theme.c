#include "tui.h"

// Close to btop's gruvbox_material_dark
static const Theme GRUVBOX_DARK = {
    .name = "gruvbox_dark",
    .main_bg = 0x282828,
    .main_fg = 0xd4be98,
    .title = 0xd4be98,
    .hi_fg = 0xea6962,
    .selected_bg = 0xd8a657,
    .selected_fg = 0x282828,
    .inactive_fg = 0x7c6f64,
    .box_border = 0x5a524c,
    .graph_low = 0xa9b665,
    .graph_mid = 0xd8a657,
    .graph_high = 0xea6962,
};

const Theme *theme_default(void) {
    return &GRUVBOX_DARK;
}
