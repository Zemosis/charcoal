# charcoal

A terminal ASCII art viewer for images, animations and video, written in C.

![charcoal showing a car on Mars as colored ASCII art](docs/screenshots/hero.png)

> Status: M6 (polish) in progress. See [docs/SPEC.md](docs/SPEC.md) for the
> full specification and milestones.

## Features

- Opens almost any image, animated image or video through FFmpeg (PNG, JPEG,
  WebP, AVIF, TIFF, GIF, APNG, MP4, MKV, WebM, ...), photos turned upright
- Five glyph modes, four palettes or your own, true color / 256 / 16 / mono,
  Floyd-Steinberg and Bayer dithering, brightness, contrast, gamma and
  saturation, all changeable live
- A full-screen viewer with panels, a braille histogram, zoom and pan, five
  themes, an options overlay and mouse support
- Smooth video playback with a timeline, seeking and speed control
- Flip through a folder with `n` / `N`, or browse with the file picker
- Scriptable `render` and `export` (`.txt`, `.ans`, `.html`)

## Modes

<table>
  <tr>
    <td><img src="docs/screenshots/mode-half.png" alt="half mode"></td>
    <td><img src="docs/screenshots/mode-quad.png" alt="quad mode"></td>
  </tr>
  <tr>
    <td align="center"><code>half</code>: two pixels per cell, the default</td>
    <td align="center"><code>quad</code>: four pixels per cell</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/mode-braille.png" alt="braille mode"></td>
    <td><img src="docs/screenshots/mode-edges.png" alt="edges mode"></td>
  </tr>
  <tr>
    <td align="center"><code>braille</code>: eight dots per cell (inverted)</td>
    <td align="center"><code>edges</code>: lines along edges over ASCII</td>
  </tr>
</table>

`ascii` is the classic mode at the top of this page.

## Dithering

<table>
  <tr>
    <td><img src="docs/screenshots/dither-none.png" alt="no dithering"></td>
    <td><img src="docs/screenshots/dither-floyd.png" alt="Floyd-Steinberg dithering"></td>
    <td><img src="docs/screenshots/dither-bayer.png" alt="Bayer dithering"></td>
  </tr>
  <tr>
    <td align="center"><code>none</code></td>
    <td align="center"><code>floyd</code>: error diffusion</td>
    <td align="center"><code>bayer</code>: steady in video</td>
  </tr>
</table>

## Palettes

<table>
  <tr>
    <td><img src="docs/screenshots/palette-standard.png" alt="standard palette"></td>
    <td><img src="docs/screenshots/palette-detailed.png" alt="detailed palette"></td>
  </tr>
  <tr>
    <td align="center"><code>standard</code> <code>@%#*+=-:. </code></td>
    <td align="center"><code>detailed</code>: 70 levels</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/palette-simple.png" alt="simple palette"></td>
    <td><img src="docs/screenshots/palette-blocks.png" alt="blocks palette"></td>
  </tr>
  <tr>
    <td align="center"><code>simple</code> <code>#*+:. </code></td>
    <td align="center"><code>blocks</code> <code>█▓▒░ </code></td>
  </tr>
</table>

## Color depths

<table>
  <tr>
    <td><img src="docs/screenshots/color-true.png" alt="true color"></td>
    <td><img src="docs/screenshots/color-256.png" alt="256 colors"></td>
  </tr>
  <tr>
    <td align="center"><code>true</code>: 24-bit</td>
    <td align="center"><code>256</code></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/color-16.png" alt="16 colors"></td>
    <td><img src="docs/screenshots/color-mono.png" alt="no color"></td>
  </tr>
  <tr>
    <td align="center"><code>16</code></td>
    <td align="center"><code>mono</code></td>
  </tr>
</table>

## Interface

<table>
  <tr>
    <td><img src="docs/screenshots/ui-picker.png" alt="file picker"></td>
    <td><img src="docs/screenshots/ui-options.png" alt="options"></td>
  </tr>
  <tr>
    <td align="center">File picker (<code>O</code>)</td>
    <td align="center">Options (<code>o</code>)</td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/ui-help.png" alt="help"></td>
    <td><img src="docs/screenshots/ui-menu.png" alt="menu"></td>
  </tr>
  <tr>
    <td align="center">Help (<code>?</code>)</td>
    <td align="center">Menu (<code>Esc</code>)</td>
  </tr>
</table>

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

## Installing

```bash
make release && sudo make install    # /usr/local/bin/charcoal
make install PREFIX=~/.local         # ~/.local/bin/charcoal, no sudo
make uninstall                       # same PREFIX as install
```

## Usage

```bash
charcoal                                 # pick a file in this folder
charcoal photo.jpg                       # interactive viewer (? for help)
charcoal ~/Pictures                      # browse a folder (n / N, O)
charcoal clip.mp4                        # plays, with a timeline
charcoal render photo.jpg -m ascii       # print once, for scripts and pipes
charcoal render photo.jpg -m braille -d floyd
charcoal render photo.jpg -m quad -c 256 --contrast 1.3
charcoal render photo.jpg -m ascii -p detailed -c mono -w 80 -i   # light terminal
charcoal render clip.mp4 -t 12.5         # the frame at 12.5 s
charcoal render anim.gif --frame 3       # the 4th frame
charcoal export photo.jpg -o photo.html  # .txt, .ans or .html
charcoal photo.jpg --theme nord
```

When stdout is not a terminal, `charcoal <file>` prints once like `render`.

### Keys

| Keys | |
|---|---|
| `↑` `↓` / `j` `k`, `←` `→` / `h` `l` | Pick a control, change it |
| `m` `p` `c` `d` `i` `f` | Mode, palette, color, dither, invert, fit |
| `b` `t` `g` `s` (capitals lower) | Brightness, contrast, gamma, saturation |
| `+` `-`, `Shift`+arrows / `H` `J` `K` `L` | Zoom, pan |
| `n` `N`, `O` | Next / previous file, file picker |
| `Space`, `,` `.`, `[` `]`, `{` `}` | Play, step a frame, seek 5 s, seek 30 s |
| `<` `>`, `R`, `Home` | Speed, repeat, back to the start |
| `e`, `o`, `?`, `Esc`, `q` | Export, options, help, menu, quit |

The mouse works too: the wheel zooms toward the cursor or changes the
control under it, dragging pans or scrubs the timeline, and buttons, tabs
and lists are clickable.

## Configuration

Press `o` in the viewer for the options; they are saved to
`~/.config/charcoal/charcoal.conf`. Themes: `gruvbox_dark` (default), `nord`,
`tokyo_night`, `mono`, `tty`, or your own
`~/.config/charcoal/themes/<name>.theme` with lines like
`main_bg = "#282828"` (keys: `main_bg`, `main_fg`, `title`, `hi_fg`,
`selected_bg`, `selected_fg`, `inactive_fg`, `box_border`, `graph_low`,
`graph_mid`, `graph_high`).
