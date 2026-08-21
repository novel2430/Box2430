#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:148}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xephyr_pid= wm_pid=

cleanup() {
    for pid in "$wm_pid" "$xephyr_pid"; do
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
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
assert_bar() {
    expected_w=$1 expected_y=$2 label=$3
    [ "$(field "$bar" 'Absolute upper-left X:')" = 0 ] || fail "$label: wrong x"
    [ "$(field "$bar" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$label: wrong y"
    [ "$(field "$bar" 'Width:')" = "$expected_w" ] || fail "$label: wrong width"
    [ "$(field "$bar" 'Height:')" = 24 ] || fail "$label: wrong height"
}

host_display=${DISPLAY:-:0}
DISPLAY=$host_display Xephyr "$display" -screen 800x600 -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-native-bar-top.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 24, 800, 576'" || fail "initial native bar workarea missing"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "initial native bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
assert_bar 800 0 "initial native bar"

DISPLAY=$display xrandr -s 640x480
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 640" || fail "Xephyr root did not shrink"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $bar | awk '/Width:/ {print \$2; exit}')\" = 640" || fail "native bar did not resize with topology"
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 24, 640, 456'" || fail "shrunk native bar workarea incorrect"
assert_bar 640 0 "shrunk native bar"
current=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
[ "$current" = "$bar" ] || fail "continued logical monitor recreated native bar"

DISPLAY=$display xrandr -s 800x600
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 800" || fail "Xephyr root did not grow"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $bar | awk '/Width:/ {print \$2; exit}')\" = 800" || fail "native bar did not grow with topology"
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 24, 800, 576'" || fail "grown native bar workarea incorrect"
assert_bar 800 0 "grown native bar"

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xephyr native-bar topology geometry/lifecycle scenario"
