#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:158}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
urgency_bin=${BOX2430_URGENCY_BIN:-./build/debug/x11-set-urgency}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= one_pid= two_pid= none_pid=

cleanup() {
    for pid in "$none_pid" "$two_pid" "$one_pid" "$wm_pid" "$xvfb_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; fi
    done
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }
wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 200 ]; then return 1; fi
        sleep 0.02
    done
}
image_convert() {
    if command -v magick >/dev/null 2>&1; then magick "$@"; else convert "$@"; fi
}
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
wait_border() {
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $1 | awk '/Border width:/ {print \$3; exit}')\" = $2"
}
expected_pixel() { image_convert xc:"$1" -format '%[pixel:p{0,0}]' info:; }
snapshot() { DISPLAY=$display xwd -silent -root -out "$1"; }
pixel_at_window_corner() {
    window=$1 image=$2
    x=$(field "$window" 'Absolute upper-left X:')
    y=$(field "$window" 'Absolute upper-left Y:')
    image_convert "$image" -format "%[pixel:p{$x,$y}]" info:
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
wait_managed() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $wanted"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-border-modes.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display xterm -title BorderOne -geometry 30x8+40+60 >"$tmp_dir/one.log" 2>&1 & one_pid=$!
DISPLAY=$display xterm -title BorderTwo -geometry 30x8+420+60 >"$tmp_dir/two.log" 2>&1 & two_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^BorderOne$' >/dev/null 2>&1" || fail "first client missing"
wait_for "DISPLAY=$display xdotool search --name '^BorderTwo$' >/dev/null 2>&1" || fail "second client missing"
one=$(DISPLAY=$display xdotool search --name '^BorderOne$' | head -n 1)
two=$(DISPLAY=$display xdotool search --name '^BorderTwo$' | head -n 1)
wait_managed "$one" || fail "first client was not managed"
wait_managed "$two" || fail "second client was not managed"

DISPLAY=$display xdotool mousemove --window "$one" 20 20 click 1
wait_active "$one" || fail "first client did not focus"
wait_border "$one" 3 || fail "FREE focused border width mismatch"
wait_border "$two" 3 || fail "FREE unfocused border width mismatch"
snapshot "$tmp_dir/free.xwd"
[ "$(pixel_at_window_corner "$one" "$tmp_dir/free.xwd")" = "$(expected_pixel '#112233')" ] ||
    fail "FREE focused border color mismatch"
[ "$(pixel_at_window_corner "$two" "$tmp_dir/free.xwd")" = "$(expected_pixel '#445566')" ] ||
    fail "FREE unfocused border color mismatch"

DISPLAY=$display "$urgency_bin" "$two" 1
wait_for "DISPLAY=$display xprop -id $two WM_HINTS | grep -qi urgency" || fail "urgency did not set"
snapshot "$tmp_dir/free-urgent.xwd"
[ "$(pixel_at_window_corner "$two" "$tmp_dir/free-urgent.xwd")" = "$(expected_pixel '#778899')" ] ||
    fail "FREE urgent border color mismatch"

DISPLAY=$display xdotool key super+m
wait_border "$one" 5 || fail "MONOCLE focused border width mismatch"
wait_border "$two" 5 || fail "MONOCLE unfocused border width mismatch"
snapshot "$tmp_dir/monocle.xwd"
[ "$(pixel_at_window_corner "$one" "$tmp_dir/monocle.xwd")" = "$(expected_pixel '#aa1122')" ] ||
    fail "MONOCLE focused border color mismatch"

# Lower the still-focused client so the identically sized second MONOCLE client
# becomes visible without changing focus or clearing urgency.
DISPLAY=$display xdotool key super+l
snapshot "$tmp_dir/monocle-urgent.xwd"
[ "$(pixel_at_window_corner "$two" "$tmp_dir/monocle-urgent.xwd")" = "$(expected_pixel '#1122aa')" ] ||
    fail "MONOCLE urgent border color mismatch"
DISPLAY=$display "$urgency_bin" "$two" 0
wait_for "! DISPLAY=$display xprop -id $two WM_HINTS | grep -qi urgency" || fail "urgency did not clear"
snapshot "$tmp_dir/monocle-unfocused.xwd"
[ "$(pixel_at_window_corner "$two" "$tmp_dir/monocle-unfocused.xwd")" = "$(expected_pixel '#22aa11')" ] ||
    fail "MONOCLE unfocused border color mismatch"
DISPLAY=$display xdotool key super+r

DISPLAY=$display xdotool key super+f
wait_border "$one" 0 || fail "real fullscreen did not force zero border"
DISPLAY=$display xdotool key super+f
wait_border "$one" 5 || fail "fullscreen exit did not restore MONOCLE width"
snapshot "$tmp_dir/monocle-restored.xwd"
[ "$(pixel_at_window_corner "$one" "$tmp_dir/monocle-restored.xwd")" = "$(expected_pixel '#aa1122')" ] ||
    fail "fullscreen exit did not restore MONOCLE color"

DISPLAY=$display xdotool key super+m
wait_border "$one" 3 || fail "FREE width did not restore after MONOCLE"
snapshot "$tmp_dir/free-restored.xwd"
[ "$(pixel_at_window_corner "$one" "$tmp_dir/free-restored.xwd")" = "$(expected_pixel '#112233')" ] ||
    fail "FREE color did not restore after MONOCLE"

DISPLAY=$display xterm -title BorderNone -geometry 30x8+250+300 >"$tmp_dir/none.log" 2>&1 & none_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^BorderNone$' >/dev/null 2>&1" || fail "border=false client missing"
none=$(DISPLAY=$display xdotool search --name '^BorderNone$' | head -n 1)
wait_managed "$none" || fail "border=false client was not managed"
wait_border "$none" 0 || fail "border=false rule did not suppress FREE border"
DISPLAY=$display xdotool mousemove --window "$one" 20 20 click 1
wait_active "$one" || fail "first client did not refocus"
DISPLAY=$display xdotool key super+m
wait_border "$none" 0 || fail "border=false rule did not survive MONOCLE mode"
wait_border "$one" 5 || fail "MONOCLE width did not reapply"

kill "$none_pid" "$two_pid" "$one_pid" 2>/dev/null || true
none_pid= two_pid= one_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xvfb per-mode border width/color/rule/fullscreen scenario"
