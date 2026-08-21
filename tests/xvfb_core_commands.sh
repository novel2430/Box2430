#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:126}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid=
wm_pid=
one_pid=
two_pid=

cleanup() {
    if [ -n "$one_pid" ]; then kill "$one_pid" 2>/dev/null || true; fi
    if [ -n "$two_pid" ]; then kill "$two_pid" 2>/dev/null || true; fi
    if [ -n "$wm_pid" ]; then kill "$wm_pid" 2>/dev/null || true; fi
    if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }

wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 150 ]; then return 1; fi
        sleep 0.02
    done
}

field() {
    DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'
}

assert_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "wrong x geometry"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "wrong y geometry"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "wrong width geometry"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "wrong height geometry"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
if ! wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1"; then
    sed -n '1,100p' "$tmp_dir/xvfb.log" >&2
    fail "Xvfb did not start"
fi

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED | grep -q _NET_WM_STATE_FULLSCREEN" || fail "WM did not initialize EWMH"

DISPLAY=$display xterm -title CoreOne -geometry 40x10 >"$tmp_dir/one.log" 2>&1 &
one_pid=$!
wait_for "DISPLAY=$display xdotool search --name CoreOne >/dev/null 2>&1" || fail "first client did not map"
one=$(DISPLAY=$display xdotool search --name CoreOne | head -n 1)
DISPLAY=$display xterm -title CoreTwo -geometry 30x8 >"$tmp_dir/two.log" 2>&1 &
two_pid=$!
wait_for "DISPLAY=$display xdotool search --name CoreTwo >/dev/null 2>&1" || fail "second client did not map"
two=$(DISPLAY=$display xdotool search --name CoreTwo | head -n 1)

original_x=$(field "$two" 'Absolute upper-left X:')
original_y=$(field "$two" 'Absolute upper-left Y:')
original_w=$(field "$two" 'Width:')
original_h=$(field "$two" 'Height:')

# The built-in client fullscreen policy is fake: acknowledge the EWMH state
# without surrendering geometry authority.
DISPLAY=$display xdotool windowstate --add FULLSCREEN "$two"
wait_for "DISPLAY=$display xprop -id $two _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN" || fail "client fullscreen was not acknowledged"
assert_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h"
DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$two"
wait_for "! DISPLAY=$display xprop -id $two _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN" || fail "client fullscreen state did not clear"
assert_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h"

DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Width:/ {print \$2; exit}')\" = 396" || fail "left snap did not apply"
assert_geometry "$two" 0 0 396 596

DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Width:/ {print \$2; exit}')\" = 796" || fail "maximize did not apply"
assert_geometry "$two" 0 0 796 596
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Width:/ {print \$2; exit}')\" = $original_w" || fail "maximize did not restore normal geometry"
assert_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h"

DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Border width:/ {print \$3}')\" = 0" || fail "fullscreen border remained"
assert_geometry "$two" 0 0 800 600
DISPLAY=$display xprop -id "$two" _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN || fail "fullscreen property missing"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Border width:/ {print \$3}')\" = 2" || fail "fullscreen border did not restore"
assert_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h"

DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Width:/ {print \$2; exit}')\" = 796" || fail "MONOCLE presentation missing"
assert_geometry "$one" 0 24 796 572
assert_geometry "$two" 0 24 796 572
DISPLAY=$display xdotool key super+Left
assert_geometry "$two" 0 24 796 572
DISPLAY=$display xdotool key super+j
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$one")" || fail "tab-order focus did not cycle"
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Width:/ {print \$2; exit}')\" = $original_w" || fail "FREE geometry did not restore"

DISPLAY=$display xdotool key super+shift+2
wait_for "DISPLAY=$display xwininfo -id $one | grep -q 'Map State: IsUnMapped'" || fail "move-workspace did not hide client"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$two")" || fail "move-workspace fallback focus failed"
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $one | grep -q 'Map State: IsViewable'" || fail "moved client did not appear on destination"
DISPLAY=$display xdotool mousemove --window "$one" 20 20 click 1
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$one")" ||
    fail "destination client could not be focused explicitly"
DISPLAY=$display xdotool key super+shift+1
wait_for "DISPLAY=$display xwininfo -id $one | grep -q 'Map State: IsUnMapped'" || fail "second move-workspace failed"
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $one | grep -q 'Map State: IsViewable'" || fail "returned client did not remap"

DISPLAY=$display xdotool key super+q
sleep 0.05
DISPLAY=$display xwininfo -id "$two" >/dev/null 2>&1 || fail "none binding still closed client"
DISPLAY=$display xdotool key super+x
wait_for "! DISPLAY=$display xwininfo -id $two >/dev/null 2>&1" || fail "WM_DELETE_WINDOW close failed"
two_pid=

kill "$one_pid" 2>/dev/null || true
one_pid=
kill "$wm_pid"
wait "$wm_pid"
wm_pid=

if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb core command/state scenario"
