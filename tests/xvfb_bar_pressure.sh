#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:180}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
color_bin=${BOX2430_COLOR_BIN:-./build/debug/x11-window-color}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid=

cleanup() {
    for pid in "$client_pid" "$wm_pid" "$xvfb_pid"; do
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
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
color_bounds() { DISPLAY=$display "$color_bin" "$1" "$2"; }

Xvfb "$display" -screen 0 96x200x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-bar-widgets.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)

# Give the center/title widget a large natural width.  At 96px the fixed edge
# groups consume the available bounds, so center must either disappear or fit
# strictly inside the remaining symmetric interval; it must never overwrite an
# edge group.
DISPLAY=$display "$fixture_bin" NORMAL \
    'Phase4 pressure title that is much wider than the monitor' 0 30 20 20 \
    >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Phase4 pressure title' >/dev/null 2>&1" || fail "pressure client missing"
wait_for "DISPLAY=$display $color_bin $bar '#003300' >/dev/null 2>&1" || fail "left workspace group missing"
wait_for "DISPLAY=$display $color_bin $bar '#111111' >/dev/null 2>&1" || fail "clipped workspace group lost all inactive items"
wait_for "DISPLAY=$display $color_bin $bar '#006600' >/dev/null 2>&1" || fail "right mode group missing"

set -- $(color_bounds "$bar" '#003300')
active_max=$3
set -- $(color_bounds "$bar" '#111111')
empty_max=$3
left_max=$active_max
[ "$empty_max" -gt "$left_max" ] && left_max=$empty_max
set -- $(color_bounds "$bar" '#006600')
mode_min=$1
[ "$left_max" -lt "$mode_min" ] || fail "fixed edge groups overlap under pressure"

if DISPLAY=$display "$color_bin" "$bar" '#000055' >"$tmp_dir/title.bounds" 2>/dev/null; then
    set -- $(cat "$tmp_dir/title.bounds")
    title_min=$1
    title_max=$3
    [ $((title_min + title_max + 1)) -eq 96 ] || fail "pressure title lost physical centering"
    [ "$title_min" -gt "$left_max" ] && [ "$title_max" -lt "$mode_min" ] ||
        fail "center title overlaps an edge group under pressure"
fi

kill "$client_pid" 2>/dev/null || true
wait "$client_pid" 2>/dev/null || true
client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xvfb native-bar narrow-layout pressure scenario"
