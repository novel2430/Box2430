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

window_hex() {
    printf '0x%x' "$1"
}

wait_managed() {
    wanted=$(window_hex "$1")
    wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $wanted"
}

wait_active() {
    wanted=$(window_hex "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

geometry_matches() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5
    info=$(DISPLAY=$display xwininfo -id "$window" 2>/dev/null) || return 1
    x=$(printf '%s\n' "$info" | awk '/Absolute upper-left X:/ {print $4; exit}')
    y=$(printf '%s\n' "$info" | awk '/Absolute upper-left Y:/ {print $4; exit}')
    width=$(printf '%s\n' "$info" | awk '/Width:/ {print $2; exit}')
    height=$(printf '%s\n' "$info" | awk '/Height:/ {print $2; exit}')
    [ "$x" = "$expected_x" ] && [ "$y" = "$expected_y" ] &&
        [ "$width" = "$expected_w" ] && [ "$height" = "$expected_h" ]
}

wait_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5
    attempts=0
    while ! geometry_matches "$window" "$expected_x" "$expected_y" "$expected_w" "$expected_h"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 150 ]; then return 1; fi
        sleep 0.02
    done
}

wait_border() {
    window=$1 expected=$2
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3; exit}')\" = $expected"
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
wait_managed "$one" || fail "first client was not managed"
DISPLAY=$display xterm -title CoreTwo -geometry 30x8 >"$tmp_dir/two.log" 2>&1 &
two_pid=$!
wait_for "DISPLAY=$display xdotool search --name CoreTwo >/dev/null 2>&1" || fail "second client did not map"
two=$(DISPLAY=$display xdotool search --name CoreTwo | head -n 1)
wait_managed "$two" || fail "second client was not managed"
wait_active "$two" || fail "second client did not become active"

original_x=$(field "$two" 'Absolute upper-left X:')
original_y=$(field "$two" 'Absolute upper-left Y:')
original_w=$(field "$two" 'Width:')
original_h=$(field "$two" 'Height:')

# The built-in client fullscreen policy is fake: acknowledge the EWMH state
# without surrendering geometry authority.
DISPLAY=$display xdotool windowstate --add FULLSCREEN "$two"
wait_for "DISPLAY=$display xprop -id $two _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN" || fail "client fullscreen was not acknowledged"
wait_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h" ||
    fail "fake fullscreen changed geometry"
DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$two"
wait_for "! DISPLAY=$display xprop -id $two _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN" || fail "client fullscreen state did not clear"
wait_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h" ||
    fail "fake fullscreen restore changed geometry"

DISPLAY=$display xdotool key super+Left
wait_geometry "$two" 0 0 396 596 || fail "left snap did not apply"

DISPLAY=$display xdotool key super+Up
wait_geometry "$two" 0 0 796 596 || fail "maximize did not apply"
DISPLAY=$display xdotool key super+Up
wait_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h" ||
    fail "maximize did not restore normal geometry"

DISPLAY=$display xdotool key super+f
wait_border "$two" 0 || fail "fullscreen border remained"
wait_geometry "$two" 0 0 800 600 || fail "fullscreen geometry missing"
DISPLAY=$display xprop -id "$two" _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN || fail "fullscreen property missing"
DISPLAY=$display xdotool key super+f
wait_border "$two" 2 || fail "fullscreen border did not restore"
wait_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h" ||
    fail "fullscreen geometry did not restore"

DISPLAY=$display xdotool key super+m
wait_border "$two" 0 || fail "default MONOCLE border width was not applied"
wait_geometry "$one" 0 24 800 576 || fail "first MONOCLE presentation missing"
wait_geometry "$two" 0 24 800 576 || fail "second MONOCLE presentation missing"
DISPLAY=$display xdotool key super+Left
DISPLAY=$display xdotool key super+j
wait_active "$one" || fail "tab-order focus did not cycle"
# Waiting for the later focus command also provides an event-order barrier for
# the preceding snap command, which must be a no-op in MONOCLE.
wait_geometry "$two" 0 24 800 576 || fail "snap changed MONOCLE geometry"
DISPLAY=$display xdotool key super+m
wait_border "$two" 2 || fail "FREE border width did not restore"
wait_geometry "$two" "$original_x" "$original_y" "$original_w" "$original_h" ||
    fail "FREE geometry did not restore"

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
