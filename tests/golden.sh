#!/usr/bin/env bash
# Golden tests: render samples and compare with checked-in output
# Usage: tests/golden.sh [--update]
set -u

cd "$(dirname "$0")/.."
BIN=./charcoal
DIR=tests/golden
update=0
[ "${1:-}" = "--update" ] && update=1

# name | arguments after "render"
CASES='
photo_ascii_detailed | samples/photo.png -m ascii -p detailed -c mono -w 51
photo_ascii_compat   | samples/photo.png -m ascii -p detailed -c mono -w 51 -i
photo_ascii_standard | samples/photo.png -m ascii -c mono -w 60
photo_ascii_floyd    | samples/photo.png -m ascii -c mono -w 60 -d floyd
photo_ascii_bayer    | samples/photo.png -m ascii -c mono -w 60 -d bayer
photo_blocks         | samples/photo.png -m ascii -p blocks -c mono -w 60
photo_half_mono      | samples/photo.png -m half -c mono -w 60
photo_half_floyd     | samples/photo.png -m half -c mono -w 60 -d floyd
photo_quad_mono      | samples/photo.png -m quad -c mono -w 60 -d floyd
photo_braille_mono   | samples/photo.png -m braille -c mono -w 60
photo_braille_floyd  | samples/photo.png -m braille -c mono -w 60 -d floyd
photo_edges          | samples/photo.png -m edges -c mono -w 60
photo_invert         | samples/photo.png -m ascii -c mono -i -w 60
photo_custom_palette | samples/photo.png -m ascii -c mono -p " .oO@" -w 40 -H 12
photo_adjust         | samples/photo.png -m ascii -c mono -w 60 --brightness 0.1 --contrast 1.5 --gamma 1.2
alpha_ascii          | samples/alpha.png -m ascii -c mono -p simple -w 16
pattern_half_color   | samples/pattern.png -c true -w 48
pattern_ascii_color  | samples/pattern.png -m ascii -c true -w 48
pattern_quad_color   | samples/pattern.png -m quad -c true -w 48
pattern_braille_color| samples/pattern.png -m braille -c true -w 48
pattern_half_256     | samples/pattern.png -c 256 -w 48
pattern_half_256_fs  | samples/pattern.png -c 256 -d floyd -w 48
pattern_half_16      | samples/pattern.png -c 16 -w 48
pattern_half_16_bayer| samples/pattern.png -c 16 -d bayer -w 48
pattern_quad_16      | samples/pattern.png -m quad -c 16 -w 48
pattern_saturation   | samples/pattern.png -c true --saturation 0 -w 48
anim_gif_first       | samples/anim.gif -m ascii -c mono -w 24
anim_gif_frame3      | samples/anim.gif -m ascii -c mono -w 24 --frame 3
anim_gif_time        | samples/anim.gif -m ascii -c mono -w 24 -t 0.5
anim_gif_past_end    | samples/anim.gif -m ascii -c mono -w 24 -t 60
anim_apng_frame3     | samples/anim.apng -m ascii -c mono -w 24 --frame 3
clip_webm_time       | samples/clip.webm -m quad -c true -w 24 -t 0.55
clip_webm_frame7     | samples/clip.webm -m ascii -c mono -w 24 --frame 7
anim_webp_frame3     | samples/anim.webp -m ascii -c mono -w 24 --frame 3
anim_webp_time       | samples/anim.webp -m half -c true -w 24 -t 0.5
'

pass=0
fail=0
skip=0

# Animated WebP needs charcoal built with libwebp
has_webp=0
$BIN --version | grep -q 'libwebp [0-9]' && has_webp=1
while IFS='|' read -r name args; do
    name=$(echo "$name" | xargs)
    [ -z "$name" ] && continue
    expected="$DIR/$name.txt"
    if [ $has_webp = 0 ] && [[ $name == *webp* ]]; then
        skip=$((skip + 1))
        continue
    fi
    actual=$(eval "$BIN render $args" 2>&1)

    if [ $update = 1 ]; then
        printf '%s\n' "$actual" > "$expected"
        echo "updated  $name"
    elif [ "$actual" = "$(cat "$expected" 2>/dev/null)" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL     $name"
        diff <(printf '%s\n' "$actual") "$expected" | head -5
    fi
done <<< "$CASES"

[ $update = 1 ] && exit 0
if [ $skip -gt 0 ]; then
    echo "golden   $pass passed, $fail failed, $skip skipped (no libwebp)"
else
    echo "golden   $pass passed, $fail failed"
fi
[ $fail = 0 ]
