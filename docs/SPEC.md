# charcoal — Specification

Status: draft v0.1 (2026-11-18)

charcoal is a terminal ASCII art viewer for images, animations and video,
written in C. It is the successor to `projects/c/ascii_art`: same core idea
(image → characters), but with a full-screen TUI whose look is inspired by
btop — rounded boxes, titled panels, braille graphs, a dark gruvbox-style
theme — plus many more input formats and rendering modes.

---

## 1. Goals

1. Open almost any image, animated image or video file and show it as
   character art in the terminal.
2. Look great: a btop-style interface that feels like a polished tool, not a
   script printing text.
3. Give the user live control over how the art is made (glyph mode, palette,
   color depth, dithering, contrast, …) and see the result instantly.
4. Play animations and video smoothly at their native frame rate.
5. Keep a scriptable, non-interactive mode so it still works in pipes and
   scripts, like the old tool.

## 2. Non-goals (v1)

- Audio playback for video files.
- Windows support (Linux first, macOS best-effort).
- Image editing or saving back to image formats.
- Sixel / Kitty / iTerm image protocols. charcoal is about *character* art;
  those protocols show real pixels and defeat the point. Reconsidered and
  kept on 2026-11-18 (a `pixels` mode via Kitty graphics was offered and
  declined); `quad` or `half` at true color is the closest to the original.
- Networking (URLs, streams). Local files only.

---

## 3. Usage

```
charcoal <file>                 open the interactive viewer (TUI)
charcoal render <file> [opts]   print once to stdout and exit (no TUI)
charcoal export <file> -o out   write to a file (.txt, .ans, .html)
charcoal --help | --version
```

If stdout is not a terminal, `charcoal <file>` behaves like `render`.

### 3.1 Options (shared by all modes)

| Option | Values | Default |
|---|---|---|
| `-m, --mode` | `ascii`, `half`, `quad`, `braille`, `edges` | `half` |
| `-p, --palette` | `standard`, `detailed`, `simple`, `blocks`, or a custom string | `standard` |
| `-c, --color` | `true`, `256`, `16`, `mono` | auto-detect |
| `-d, --dither` | `none`, `floyd`, `bayer` | `none` |
| `-w, --width N` | output width in characters | fit terminal |
| `-H, --height N` | output height in characters | fit terminal |
| `--fit` | `contain`, `cover`, `stretch` | `contain` |
| `-i, --invert` | flip brightness, for light terminal backgrounds | off |
| `--brightness`, `--contrast`, `--gamma`, `--saturation` | numbers | neutral |
| `--theme NAME` | theme name | `gruvbox_dark` |
| `--frame N` / `--time T` | `render`/`export` of one video frame | first frame |
| `--loop` / `--no-loop` | animation looping | loop |

If neither `--width` nor `--height` is given, the output fits the terminal
(carrying over the fit logic from ascii_art), or 100 columns when there is
no terminal.

---

## 4. Input formats

All decoding goes through FFmpeg (`libavformat` + `libavcodec` +
`libswscale`). One code path handles everything, and new formats come free
with newer FFmpeg builds.

| Kind | Formats (depends on the FFmpeg build) |
|---|---|
| Still images | PNG, JPEG, WebP, AVIF, TIFF, BMP, TGA, PNM, QOI, JPEG XL, HEIC* |
| Animated | GIF, APNG, animated WebP |
| Video | MP4, MKV, WebM, MOV, AVI, … (H.264, H.265, VP9, AV1, …) |

\* HEIC support varies by FFmpeg version and distro packaging.

On startup, charcoal sorts the input into one of three kinds:

- **still**: one frame, rendered once and re-rendered only when settings or
  the terminal size change
- **animated**: a few frames with per-frame delays; all frames are decoded and
  cached up front if they fit in a memory budget (default 256 MiB)
- **video**: decoded as a stream on a background thread

Errors such as an unsupported codec, a corrupt file or a missing file show a
clear message (in the TUI as a red box, otherwise on stderr) with exit code 1.

---

## 5. Rendering pipeline

```
decode ─► RGB24 frame ─► resize ─► adjust ─► glyph map ─► quantize ─► cells ─► screen
          (swscale)      (area avg) (s, b/c/γ) (+ dither)  (256/16)
```

### 5.1 Cell aspect ratio

Terminal cells are taller than they are wide. charcoal reads the real cell size
from `TIOCGWINSZ` (`ws_xpixel` / `ws_ypixel`) when the terminal reports it,
otherwise it assumes 1:2. This replaces ascii_art's hard-coded `* 2`.

### 5.2 Resize

Area averaging (box filter) is used when shrinking, which is the ascii_art
approach, kept because it is fast and avoids aliasing. Partial edge blocks are
averaged over the pixels they cover. Growing uses nearest neighbor so pixel
art stays crisp. Decoders that can shrink while decoding (JPEG's `lowres`) are
asked for the smallest size that still covers the output, which halves the
time for large photos. Each glyph mode sets how many sub-pixels a
cell holds (see below), so the image is resized to
`cols × sub_w` by `rows × sub_h` pixels.

### 5.3 Adjustments

Saturation is applied first (each channel pushed away from the pixel's gray),
then brightness, contrast and gamma through one 256-entry lookup table. They
run on the resized pixels, so their cost does not depend on the image size.

Terminals are assumed to have a dark background (decided 2026-11-18): bright
pixels get dense glyphs and lit dots. Invert flips this for light terminals;
for the ramp modes `-i` reproduces ascii_art's output exactly.

### 5.4 Glyph modes

| Mode | Sub-pixels per cell | How it works |
|---|---|---|
| `ascii` | 1×1 | Luminance → character from the palette ramp. Color = fg. |
| `half` | 1×2 | `▀` with fg = top pixel, bg = bottom pixel. Highest fidelity; default. |
| `quad` | 2×2 | Quadrant blocks `▖▗▘▝▚▞▙▛▜▟…`. Of the 8 ways to split the 4 pixels into 2 groups, pick the one with the least squared color error; fg/bg are the group averages. Mono: lit quadrants are the light pixels. |
| `braille` | 2×4 | Braille `⠀`–`⣿`. Dots are lit where luminance is light, after dithering (use `floyd` for shading). Dot color = average of the lit pixels. btop-like look. |
| `edges` | 1×1 | Sobel filter (rows scaled by the cell aspect). Strong edges use `/ \ \| -` by gradient angle; the rest falls back to `ascii`. |

Palettes are listed densest first, as in ascii_art:

- `standard`: `@%#*+=-:. ` (10 levels)
- `detailed`: the 70-level Paul Bourke ramp from ascii_art
- `simple`: `#*+:. `
- `blocks`: `█▓▒░ `
- custom: any string passed to `--palette`

### 5.5 Color

- **true**: 24-bit `38;2;r;g;b` / `48;2;r;g;b`
- **256**: nearest xterm-256 color (6×6×6 cube + 24 grays; indices 0–15 are
  skipped because terminals theme them), written as `38;5;n`
- **16**: nearest of the 16 ANSI colors, written as `30–37` / `90–97`. Until
  themes (M5) or a terminal query (OSC 4) supply the real values, xterm's
  defaults are used.
- **mono**: no color; the glyph alone carries brightness

"Nearest" is measured in OKLab, where distance follows how different two
colors look; plain RGB distance picked visibly wrong hues for pale colors.

Auto-detect: `NO_COLOR` set → mono, `COLORTERM=truecolor|24bit` → true,
otherwise `TERM` containing `256color` → 256, otherwise 16.

### 5.6 Dithering

Dithering applies to glyph choice (the ramp in `ascii`/`edges`, and
which half/quadrant/dot is lit in mono `half`, `quad` and in `braille`) and to
256/16 color reduction. `quad` and `braille` colors are already per-cell
averages, so their colors are not dithered.

- `none`: equal-width bins, as ascii_art picked characters
- `floyd`: Floyd–Steinberg error diffusion (per channel for color)
- `bayer`: 4×4 ordered dithering; stable across video frames (no shimmer)

---

## 6. TUI

### 6.1 Look and feel (btop-inspired)

- Rounded box-drawing borders `╭─╮ │ ╰─╯`, with the title inside the top
  border: `╭─¹viewer──…─╮`. Superscript digits show the panel's hotkey, as
  in btop.
- Clickable/toggle "buttons" in the top border: `╭─²controls─┤mode├─┤palette├─╮`,
  with the hotkey letter highlighted in the accent color.
- Braille graphs for the histogram and frame-time panels.
- Labels in the dim foreground color, values in bright foreground, hotkeys in
  the accent color, as in the btop screenshots.
- Help overlay (`?`) with a **CHARCOAL** logo above a key/description table
  in a centered box. The logo is our own "charcoal smudge" design: letters
  shaded `▓` → `▒` → `░` from top to bottom so they fade out like a charcoal
  stroke, with no outline or drop shadow (unlike btop's logo):

  ```
  ▓▓▓▓ ▓  ▓ ▓▓▓▓ ▓▓▓  ▓▓▓▓ ▓▓▓▓ ▓▓▓▓ ▓
  ▒    ▒▒▒▒ ▒▒▒▒ ▒▒▒  ▒    ▒  ▒ ▒▒▒▒ ▒
  ░░░░ ░  ░ ░  ░ ░  ░ ░░░░ ░░░░ ░  ░ ░░░░
  ```
- Options overlay (`o`) with tabs (`general`, `render`, `color`, `playback`),
  the setting list on the left and a description on the right, the selected
  row highlighted with a solid accent bar, and `←`/`→` to change values.

### 6.2 Layout

```
╭─¹viewer──────────────────────────────────────────╮╭─²info──────────────╮
│                                                  ││ file   cat.webp    │
│                                                  ││ format WebP (anim) │
│               (rendered art here)                ││ size   1280×720    │
│                                                  ││ frame  12 / 48     │
│                                                  │╰────────────────────╯
│                                                  │╭─³controls──────────╮
│                                                  ││ mode    ← half →   │
│                                                  ││ palette standard   │
│                                                  ││ color   true       │
│                                                  ││ dither  none       │
│                                                  ││ invert  off        │
│                                                  ││ contrast ▮▮▮▮▯▯ 1.2│
│                                                  │╰────────────────────╯
│                                                  │╭─⁴histogram─────────╮
│                                                  ││ ⣀⣤⣶⣿⣿⣷⣦⣤⣀⡀ (luma)  │
╰──────────────────────────────────────────────────╯╰────────────────────╯
╭─⁵timeline──────────────────────────────────────────────────────────────╮
│ ▶ 00:12 ━━━━━━━━━━━━━━━━━●───────────────────────── 01:47  1.0×  loop  │
╰────────────────────────────────────────────────────────────────────────╯
```

- The side panels take a fixed 30 columns: info on top, controls below
  (filling the rest of the column until the histogram exists). `1`–`5`
  toggle each panel. With all side panels hidden, the viewer fills the
  screen; with the viewer hidden, the side panels take the full width.
- The file name sits in the viewer's top border as a `┐name┌` label (long
  names keep their end, where the extension is); one-off notes go in the
  bottom border.
- The viewer always fits the image to the panel, enlarging small images with
  nearest-neighbor pixels (decided 2026-11-18). `charcoal render` keeps never
  enlarging. `fit` picks contain (whole image), cover (fills, crops) or
  stretch (ignores aspect).
- The timeline shows only for animated and video input.
- Below 80×24, side panels and the viewer border auto-hide and a one-line
  status bar shows the mode, color, dither, zoom and the selected control.

The controls panel works like btop's option lists (decided 2026-11-18): one
row is highlighted with a solid accent bar showing `← value →`, `↑`/`↓`
select a row and `←`/`→` change it. Rows with a hotkey show its letter in
the accent color (`m`ode, `p`alette, `c`olor, `d`ither, `i`nvert, `f`it);
pressing a hotkey also moves the cursor to that row. Numbers step by 0.05
(brightness) or 0.1 and snap to that grid.

### 6.3 Key bindings

| Key | Action |
|---|---|
| `q`, `Ctrl+C` | Quit (`q` closes an open overlay first) |
| `?`, `F1` (and `h` with vim keys off) | Help overlay |
| `Esc` | Close the overlay |
| `o`, `F2` | Options overlay (M5) |
| `1`–`5` | Toggle panel |
| `↑` / `↓` (`k` / `j` with vim keys) | Select a control |
| `←` / `→` (`h` / `l` with vim keys) | Change the selected control |
| `m` / `M` | Next / previous glyph mode |
| `p` / `P` | Next / previous palette |
| `c` / `C` | Next / previous color depth |
| `d` | Cycle dithering |
| `i` | Toggle invert |
| `f` | Cycle fit (contain / cover / stretch) |
| `+` / `-` | Zoom in / out (past 1:1 with nearest-neighbor upscaling) |
| `Shift`+arrows (`H J K L` with vim keys) | Pan when zoomed |
| `r` | Reset zoom, pan and adjustments |
| `Ctrl+Z` | Suspend to the shell; `fg` brings the viewer back |
| `Space` | Play / pause |
| `,` / `.` | Step one frame back / forward (paused) |
| `[` / `]` | Seek −5 s / +5 s |
| `{` / `}` | Seek −30 s / +30 s |
| `<` / `>` | Playback speed down / up |
| loop key: TBD in M4 (`L` is vim pan right) | Toggle loop |
| `e` | Export the current frame (prompts for a filename) |
| `Ctrl+R` | Redraw; also reloads config and themes from M5 |

Mouse support (click panel buttons, scroll to zoom) is planned for M6.

### 6.4 Terminal handling

- Alternate screen, hidden cursor, raw mode via `termios`. The terminal is
  always restored on exit, on fatal signals (`SIGINT`, `SIGTERM`, `SIGSEGV`
  via `atexit` + signal handlers) and on `SIGTSTP`/`SIGCONT` (`Ctrl+Z`).
- `SIGWINCH` triggers a re-layout and re-render.
- Double-buffered cell grid: each frame is drawn into a back buffer, and only
  changed cells are written. Runs of the same color skip repeated escape codes.
- Synchronized output (DEC private mode 2026) wraps each flush when the
  terminal supports it, to avoid tearing during video.
- One `write()` per frame from a growable output buffer, never `printf` per
  character.

### 6.5 Themes

Themes are plain `key = "#rrggbb"` files in `~/.config/charcoal/themes/`.
Built-ins are compiled in.

```
main_bg      = "#282828"
main_fg      = "#d4be98"
title        = "#d4be98"
hi_fg        = "#ea6962"   # hotkeys / accent
selected_bg  = "#d8a657"
selected_fg  = "#282828"
inactive_fg  = "#7c6f64"
box_border   = "#504945"
graph_low    = "#a9b665"
graph_mid    = "#d8a657"
graph_high   = "#ea6962"
```

Built-in themes: `gruvbox_dark` (default, closest to the screenshots),
`nord`, `tokyo_night`, `mono`, `tty` (16-color only). A theme only colors the
UI chrome; the art keeps the image's own colors.

---

## 7. Playback

- A decoder thread fills a ring buffer (default 8 frames) of frames already
  resized to the current target size. A size change flushes the ring.
- The main loop shows each frame when the clock reaches its PTS. If it falls
  more than one frame behind, it drops frames instead of slowing down.
- Seeking uses `av_seek_frame` on the nearest keyframe and then decodes up to
  the target time.
- Animated GIF/APNG/WebP use the per-frame delays from the file. A delay of
  0 is treated as 100 ms, as browsers do.
- The frame-time braille graph in the timeline panel shows render time against
  the frame budget (green / yellow / red, like btop's CPU graph).

Threads are `pthread`s. The UI thread owns all terminal I/O, and the decoder
thread never writes to the terminal.

---

## 8. Configuration

`~/.config/charcoal/charcoal.conf` (`$XDG_CONFIG_HOME` honoured):

```
theme         = "gruvbox_dark"
mode          = "half"
palette       = "standard"
color         = "auto"
dither        = "none"
vim_keys      = true
rounded       = true
panels        = "viewer info controls histogram timeline"
cache_mb      = 256
```

Command-line options override the config. A missing or invalid config falls
back to the defaults, with a warning in the status line.

---

## 9. Export

| Extension | Content |
|---|---|
| `.txt` | Plain characters, no color |
| `.ans` | Characters + ANSI escape codes (shown by `cat`) |
| `.html` | `<pre>` with inline-styled spans, dark background, monospace |

Video export writes one frame (`--frame` / `--time`). Exporting a whole video
is out of scope for v1.

---

## 10. Architecture

```
charcoal/
├── include/            buf.h, media.h, render.h, tui.h, app.h
├── src/
│   ├── main.c          argument parsing, mode dispatch
│   ├── app.c           app state, event loop, key handling
│   ├── config.c        config + theme file parsing
│   ├── media/
│   │   ├── decoder.c   FFmpeg open/probe/decode/seek, kind detection
│   │   ├── frame.c     RGB frame type, alloc/free
│   │   └── player.c    decoder thread, ring buffer, clock
│   ├── render/
│   │   ├── resize.c    area-average resize, cell aspect
│   │   ├── adjust.c    brightness/contrast/gamma/saturation LUT
│   │   ├── glyph.c     glyph modes → cell grid
│   │   ├── dither.c    Floyd–Steinberg, Bayer
│   │   ├── color.c     truecolor → 256 / 16 quantization
│   │   └── export.c    txt / ans / html writers
│   └── tui/
│       ├── term.c      raw mode, alt screen, size, signals, input decoding
│       ├── screen.c    cell grid, double buffer, diff flush
│       ├── draw.c      boxes, titles, buttons, text, braille graphs
│       ├── theme.c     theme tables, built-ins
│       ├── layout.c    panel placement, resize
│       └── views/      viewer.c, panels.c (info, controls, status),
│                       help.c; later histogram, timeline, options
├── tests/              unit + golden tests
├── samples/            small test media checked into the repo
├── docs/SPEC.md
└── Makefile
```

Core types:

```c
typedef struct Frame {
    int width;
    int height;
    uint8_t *rgb;       // width * height * 3
    int64_t pts_us;     // presentation time, microseconds
} Frame;

typedef struct Cell {
    uint32_t ch;        // Unicode code point
    uint32_t fg;        // 0xRRGGBB
    uint32_t bg;        // 0xRRGGBB, or CELL_BG_DEFAULT
    uint8_t attr;       // bold, dim, ...
} Cell;

typedef struct Grid {
    int cols;
    int rows;
    Cell *cells;
} Grid;
```

`render/` is pure (frame + settings → grid) with no terminal or FFmpeg calls,
so it can be unit tested and used by `render`/`export` without the TUI.

---

## 11. Build and dependencies

- C11 (`-std=c11 -D_POSIX_C_SOURCE=200809L`), GCC or Clang
- FFmpeg development libraries via `pkg-config`: `libavformat`, `libavcodec`,
  `libavutil`, `libswscale`
  - Fedora: `sudo dnf install ffmpeg-free-devel` (or `ffmpeg-devel` from
    RPM Fusion for more codecs)
  - Debian/Ubuntu: `sudo apt install libavformat-dev libavcodec-dev libswscale-dev`
  - macOS: `brew install ffmpeg`
- pthreads, libm
- No curses library. The TUI is hand-written ANSI, like btop.

charcoal is an installed command-line tool used like `btop`: it runs as
`charcoal` from any directory, not as `./charcoal` from the repo.

Make targets: `make` (debug, `-g -O0`, plus `-fsanitize=address,undefined`
when the runtimes are installed), `make release` (`-O2`), `make test`,
`make golden`, `make install` / `make uninstall` (`PREFIX`, default
`/usr/local`, and `DESTDIR` for packaging), `make clean`. Header dependencies
(`-MMD -MP`) and flag-change rebuilds are carried over from the ascii_art
Makefile fix.

Code style: `//` comments only, 4-space indent, braces on the same line,
`typedef struct Name {…} Name;`, a `// Prototype` block for static helpers,
include guards in headers.

---

## 12. Performance targets

- Still image: under 50 ms to decode and render a 12 MP JPEG. This excludes
  process startup: Fedora's FFmpeg pulls in ~240 shared libraries, which costs
  ~50 ms before `main` runs.
- Settings change on a still: re-render under 16 ms at 200×60 cells.
- Video: 1080p30 H.264 played at full rate into a 200×60 viewer on a laptop
  CPU, with no dropped frames in `half` mode.
- Idle CPU near 0% when a still is shown (event-driven, no busy loop).

---

## 13. Testing

- **Unit tests** (`tests/`, a tiny assert-based runner, no framework): resize,
  LUT, glyph selection per mode, dithering, color quantization, config and
  theme parsing, ANSI diff output.
- **Golden tests**: `charcoal render samples/X --mode M --color mono` compared
  to checked-in `.txt` output.
- **Manual TUI checklist** per milestone: resize, `Ctrl+Z`/`fg`, every key
  binding, tiny terminal, terminal restored after a crash.
- Debug builds run with ASan + UBSan, and `valgrind` is clean on a full
  video playthrough.

---

## 14. Milestones

| # | Milestone | Done when |
|---|---|---|
| M0 ✓ | Repo, Makefile, spec | This document is committed |
| M1 ✓ | `render` for still images through FFmpeg, `ascii` mode, true color, fit terminal | Matches ascii_art output on its test images; opens PNG/JPEG/WebP/AVIF |
| M2 ✓ | All glyph modes, palettes, color depths, dithering, adjustments | Golden tests for every mode pass |
| M3 ✓ | TUI shell: term, screen diffing, boxes, theme, viewer + info + controls panels, help overlay | Can browse settings live on a still image; resize and `Ctrl+Z` are safe |
| M4 | Animation and video playback, timeline panel, seeking | 1080p30 target met; GIF/APNG/WebP animate correctly |
| M5 | Options overlay, config file, themes, histogram panel, export | Settings persist; all 5 built-in themes |
| M6 | Polish: mouse, frame-time graph, edge mode tuning, README with screenshots | Daily-driver quality |

---

## 15. Decisions

Resolved 2026-11-18:

1. **Directory browser**: yes, but after M6. `charcoal <directory>` will open
   a file browser panel to flip through images with `n`/`p`. Until then,
   passing a directory is an error.
2. **Zoom**: zooming goes past 1:1. Beyond one source pixel per sub-pixel, the
   image is upscaled with nearest-neighbor so pixel art stays crisp.
3. **Default mode**: `half`, since it is the closest to the real image.
   `ascii` stays one keypress away (`m`).
4. **Logo**: the "charcoal smudge" design shown in 6.1.
5. **Background**: terminals are assumed dark, so ramp modes give bright
   pixels the dense glyphs; `-i` is for light terminals (see 5.3).
6. **Controls**: a btop-style cursor in the controls panel; panning moved to
   `Shift`+arrows / `HJKL` (see 6.2, 6.3).
7. **Viewer fit**: the viewer enlarges small images to fill the panel (6.2).
8. **No `blocks` mode**: it was `ascii` with the block palette forced, so it
   was removed; `-m ascii -p blocks` gives the same output and `-m blocks`
   says so.
9. **No pixel modes**: image protocols stay a non-goal (see 2).
