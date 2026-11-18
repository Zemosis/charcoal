# charcoal

A terminal ASCII art viewer for images, animations and video, written in C,
with a btop-inspired interface.

> Status: M3 done (interactive viewer for still images). Next: animation and
> video playback (M4). See
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
(`libavformat`, `libavcodec`, `libavutil`, `libswscale`). Debug builds use
ASan and UBSan when their runtimes are installed.

```bash
# Fedora
sudo dnf install ffmpeg-free-devel libasan libubsan
# Debian/Ubuntu
sudo apt install libavformat-dev libavcodec-dev libswscale-dev
```

```bash
make            # debug build (ASan + UBSan)
make release    # optimized build
make test       # unit tests (no FFmpeg needed)
make golden     # render samples/ and compare with tests/golden/
```

## Installing

```bash
make release && sudo make install    # /usr/local/bin/charcoal
make install PREFIX=~/.local         # ~/.local/bin/charcoal, no sudo
make uninstall                       # same PREFIX as install
```

## Usage

```bash
charcoal photo.jpg                       # interactive viewer (? for help)
charcoal render photo.jpg -m ascii       # classic ASCII ramp
charcoal render photo.jpg -m braille -d floyd
charcoal render photo.jpg -m quad -c 256 --contrast 1.3
charcoal render photo.jpg -m ascii -p detailed -c mono -w 80 -i   # light terminal
charcoal export photo.jpg -o photo.ans   # .txt or .ans
```

In the viewer, `↑`/`↓` (or `j`/`k`) pick a setting in the controls panel and
`←`/`→` (or `h`/`l`) change it; `m`, `p`, `c`, `d`, `i`, `f` jump straight to
mode, palette, color, dither, invert and fit. `+`/`-` zoom, `Shift`+arrows pan,
`r` resets, `?` shows every key and `q` quits. When stdout is not a terminal,
`charcoal <file>` prints once like `render`.
