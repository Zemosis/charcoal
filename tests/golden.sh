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
photo_ascii_standard | samples/photo.png -m ascii -c mono -w 60
photo_blocks         | samples/photo.png -m blocks -c mono -w 60
photo_half_mono      | samples/photo.png -m half -c mono -w 60
photo_invert         | samples/photo.png -m ascii -c mono -i -w 60
photo_custom_palette | samples/photo.png -m ascii -c mono -p " .oO@" -w 40 -H 12
alpha_ascii          | samples/alpha.png -m ascii -c mono -p simple -w 16
pattern_half_color   | samples/pattern.png -w 48
pattern_ascii_color  | samples/pattern.png -m ascii -w 48
'

pass=0
fail=0
while IFS='|' read -r name args; do
    name=$(echo "$name" | xargs)
    [ -z "$name" ] && continue
    expected="$DIR/$name.txt"
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
echo "golden   $pass passed, $fail failed"
[ $fail = 0 ]
