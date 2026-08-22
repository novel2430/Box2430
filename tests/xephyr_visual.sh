#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:141}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xephyr_pid= wm_pid= back_pid= front_pid= drag_pid=

cleanup() {
    for pid in "$drag_pid" "$front_pid" "$back_pid" "$wm_pid" "$xephyr_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; fi
    done
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }
image_convert() {
    if command -v magick >/dev/null 2>&1; then
        magick "$@"
    else
        convert "$@"
    fi
}
wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
capture() {
    DISPLAY=$display xwd -silent -root -out "build/evidence/$1.xwd"
    image_convert "build/evidence/$1.xwd" "build/evidence/$1.png"
}

mkdir -p build/evidence
DISPLAY=${DISPLAY:-:0} Xephyr "$display" -screen 800x600 -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 & xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-visual.toml >"$tmp_dir/wm.log" 2>&1 & wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display "$fixture_bin" NORMAL FreeBack 70 80 360 230 >"$tmp_dir/back.log" 2>&1 & back_pid=$!
wait_for "DISPLAY=$display xdotool search --name FreeBack >/dev/null 2>&1" || fail "back client missing"
DISPLAY=$display "$fixture_bin" NORMAL FreeActive 150 150 300 180 >"$tmp_dir/front.log" 2>&1 & front_pid=$!
wait_for "DISPLAY=$display xdotool search --name FreeActive >/dev/null 2>&1" || fail "front client missing"
front=$(DISPLAY=$display xdotool search --name FreeActive | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $front | grep -q 'Map State: IsViewable'" ||
    fail "front client did not map"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$front")" ||
    fail "front client did not focus"
capture xephyr-free

DISPLAY=$display xdotool keydown super mousemove --window "$front" 20 20 \
    mousedown 1 sleep 0.05 mousemove --sync 0 300 sleep 0.6 mouseup 1 keyup super & drag_pid=$!
wait_for "DISPLAY=$display xwininfo -root -tree | grep -q '400x2+0+0'" ||
    fail "snap preview outline missing"
capture xephyr-snap-preview
wait "$drag_pid"; drag_pid=
wait_for "test \"\$(DISPLAY=$display xwininfo -id $front | awk '/Width:/ {print \$2; exit}')\" = 396" ||
    fail "snap did not commit"
capture xephyr-snapped

DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $front | awk '/Width:/ {print \$2; exit}')\" = 796" ||
    fail "maximize did not apply"
capture xephyr-maximized
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $front | awk '/Width:/ {print \$2; exit}')\" = 800" ||
    fail "fullscreen did not apply"
capture xephyr-fullscreen

kill "$front_pid" "$back_pid" 2>/dev/null || true; front_pid= back_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,180p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xephyr FREE/snap-preview/snap/maximize/fullscreen visual scenario"
