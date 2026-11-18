# charcoal

A terminal ASCII art viewer for images, animations and video, written in C,
with a btop-inspired interface.

> Status: M1 in progress (`render` for still images). See
> [docs/SPEC.md](docs/SPEC.md) for the full specification and milestones.

## Planned features

- Opens almost any image, animated image or video file through FFmpeg
  (PNG, JPEG, WebP, AVIF, GIF, APNG, MP4, MKV, WebM, ...)
- Glyph modes: `ascii`, `blocks`, `half`, `quad`, `braille`, `edges`
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
charcoal photo.jpg                       # fit the terminal, half blocks
charcoal render photo.jpg -m ascii       # classic ASCII ramp
charcoal render photo.jpg -m ascii -p detailed -c mono -w 80
charcoal export photo.jpg -o photo.ans   # .txt or .ans
```

Until the interactive viewer lands (M3), `charcoal <file>` prints once like
`render`.
