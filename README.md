# charcoal

A terminal ASCII art viewer for images, animations and video, written in C,
with a btop-inspired interface.

> Status: planning. See [docs/SPEC.md](docs/SPEC.md) for the full specification
> and milestones.

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
(`libavformat`, `libavcodec`, `libavutil`, `libswscale`).

```bash
make            # debug build
make release    # optimized build
```
