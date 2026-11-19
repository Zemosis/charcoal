# charcoal

A terminal ASCII art viewer for images, animations and video, written in C.

> Status: M6 in progress: file switching (`n` / `N`, `O` picker, folders) is
> done; mouse, frame-time graph and edges tuning are next. See
> [docs/SPEC.md](docs/SPEC.md) for the full specification and milestones.

## Planned features

- Opens almost any image, animated image or video file through FFmpeg
  (PNG, JPEG, WebP, AVIF, GIF, APNG, MP4, MKV, WebM, ...)
- Glyph modes: `ascii`, `half`, `quad`, `braille`, `edges`; palettes
  `standard`, `detailed`, `simple`, `blocks` or your own
- True color, 256, 16 color and mono output, with optional dithering
- Full-screen TUI with rounded panels, braille graphs, themes and live controls
- Smooth animation and video playback with seeking
- Scriptable `render` and `export` modes (`.txt`, `.ans`, `.html`)

## Building

Requires a C11 compiler and the FFmpeg development libraries
(`libavformat`, `libavcodec`, `libavutil`, `libswscale`). libwebp is optional
and adds animated WebP. Debug builds use ASan and UBSan when their runtimes
are installed.

```bash
# Fedora
sudo dnf install ffmpeg-free-devel libwebp-devel libasan libubsan
# Debian/Ubuntu
sudo apt install libavformat-dev libavcodec-dev libswscale-dev libwebp-dev
```

```bash
make            # debug build (ASan + UBSan)
make release    # optimized build
make test       # unit tests (no FFmpeg needed)
make golden     # render samples/ and compare with tests/golden/
```

## Configuration

Press `o` in the viewer for the options; they are saved to
`~/.config/charcoal/charcoal.conf`. Themes: `gruvbox_dark` (default), `nord`,
`tokyo_night`, `mono`, `tty`, or your own
`~/.config/charcoal/themes/<name>.theme` with lines like
`main_bg = "#282828"` (keys: `main_bg`, `main_fg`, `title`, `hi_fg`,
`selected_bg`, `selected_fg`, `inactive_fg`, `box_border`, `graph_low`,
`graph_mid`, `graph_high`).

## Installing

```bash
make release && sudo make install    # /usr/local/bin/charcoal
make install PREFIX=~/.local         # ~/.local/bin/charcoal, no sudo
make uninstall                       # same PREFIX as install
```

## Usage

```bash
charcoal photo.jpg                       # interactive viewer (? for help)
charcoal ~/Pictures                      # browse a folder (n / N, O)
charcoal render photo.jpg -m ascii       # classic ASCII ramp
charcoal render photo.jpg -m braille -d floyd
charcoal render photo.jpg -m quad -c 256 --contrast 1.3
charcoal render photo.jpg -m ascii -p detailed -c mono -w 80 -i   # light terminal
charcoal clip.mp4                        # plays, with a timeline
charcoal render clip.mp4 -t 12.5         # the frame at 12.5 s
charcoal render anim.gif --frame 3       # the 4th frame
charcoal export photo.jpg -o photo.html  # .txt, .ans or .html
charcoal photo.jpg --theme nord
```

In the viewer, `↑`/`↓` (or `j`/`k`) pick a setting in the controls panel and
`←`/`→` (or `h`/`l`) change it; `m`, `p`, `c`, `d`, `i`, `f` jump straight to
mode, palette, color, dither, invert and fit. `+`/`-` zoom, `Shift`+arrows pan,
`b`/`t`/`g`/`s` raise brightness, contrast, gamma and saturation (capitals
lower them), `e` exports the view, `o` opens the options, `r` resets, `?`
shows every key and `q` quits. For animations and video:
`Space` plays and pauses, `,`/`.` step a frame, `[`/`]` and `{`/`}` seek 5
and 30 seconds, `<`/`>` change speed, `R` toggles repeat and `Home` restarts. When stdout is not a terminal,
`charcoal <file>` prints once like `render`.
