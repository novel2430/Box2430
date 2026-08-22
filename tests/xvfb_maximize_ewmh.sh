#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:147}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
state_bin=${BOX2430_STATE_BIN:-./build/debug/x11-net-wm-state}
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
        if [ "$attempts" -ge 200 ]; then return 1; fi
        sleep 0.02
    done
}
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
assert_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5 expected_b=$6 label=$7
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "$label: wrong x"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$label: wrong y"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "$label: wrong width"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "$label: wrong height"
    [ "$(field "$window" 'Border width:')" = "$expected_b" ] || fail "$label: wrong border"
}
has_state() {
    DISPLAY=$display xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q "$2"
}
assert_normal() {
    window=$1 label=$2
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = 220" ||
        fail "$label: normal geometry did not settle"
    assert_geometry "$window" 60 70 220 140 4 "$label"
    if has_state "$window" _NET_WM_STATE_MAXIMIZED_HORZ ||
       has_state "$window" _NET_WM_STATE_MAXIMIZED_VERT; then
        fail "$label: maximize EWMH state remained"
    fi
}
assert_maximized() {
    window=$1 label=$2
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = 792" ||
        fail "$label: maximized geometry did not settle"
    assert_geometry "$window" 0 0 792 592 4 "$label"
    has_state "$window" _NET_WM_STATE_MAXIMIZED_HORZ || fail "$label: horizontal state missing"
    has_state "$window" _NET_WM_STATE_MAXIMIZED_VERT || fail "$label: vertical state missing"
}
send_state() {
    DISPLAY=$display "$state_bin" "$@"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-geometry-state.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED | grep -q _NET_WM_STATE_MAXIMIZED_HORZ" ||
    fail "WM did not advertise horizontal maximize"
DISPLAY=$display xprop -root _NET_SUPPORTED | grep -q _NET_WM_STATE_MAXIMIZED_VERT ||
    fail "WM did not advertise vertical maximize"

DISPLAY=$display "$fixture_bin" NORMAL GeometryMaximize 60 70 220 140 >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name GeometryMaximize >/dev/null 2>&1" || fail "client missing"
client=$(DISPLAY=$display xdotool search --name GeometryMaximize | head -n 1)
assert_normal "$client" "initial"

# GTK/Qt-style paired ADD/REMOVE requests reuse Box2430's existing maximize state.
send_state "$client" add _NET_WM_STATE_MAXIMIZED_HORZ _NET_WM_STATE_MAXIMIZED_VERT
assert_maximized "$client" "client ADD"
send_state "$client" add _NET_WM_STATE_MAXIMIZED_HORZ _NET_WM_STATE_MAXIMIZED_VERT
assert_maximized "$client" "duplicate ADD"
send_state "$client" remove _NET_WM_STATE_MAXIMIZED_HORZ _NET_WM_STATE_MAXIMIZED_VERT
assert_normal "$client" "client REMOVE"

# Box2430 has one full maximize concept: either EWMH maximize axis maps to it.
send_state "$client" toggle _NET_WM_STATE_MAXIMIZED_VERT
assert_maximized "$client" "single-axis toggle enter"
send_state "$client" toggle _NET_WM_STATE_MAXIMIZED_HORZ
assert_normal "$client" "single-axis toggle leave"

# Internal maximize advertises the same EWMH state, and clearing snap/maximize
# through an existing command clears the property too.
DISPLAY=$display xdotool key super+Up
assert_maximized "$client" "keyboard maximize"
DISPLAY=$display xdotool key super+n
assert_normal "$client" "snap none restore"

# Fullscreen and maximize must coexist in _NET_WM_STATE instead of replacing
# each other. Leaving fullscreen returns to the pre-existing maximize state.
send_state "$client" add _NET_WM_STATE_MAXIMIZED_HORZ _NET_WM_STATE_MAXIMIZED_VERT
assert_maximized "$client" "maximize before fullscreen"
DISPLAY=$display xdotool key super+g
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Border width:/ {print \$3; exit}')\" = 0" ||
    fail "fullscreen did not settle"
has_state "$client" _NET_WM_STATE_FULLSCREEN || fail "fullscreen state missing"
has_state "$client" _NET_WM_STATE_MAXIMIZED_HORZ || fail "maximize state lost in fullscreen"
has_state "$client" _NET_WM_STATE_MAXIMIZED_VERT || fail "maximize state lost in fullscreen"
DISPLAY=$display xdotool key super+f
assert_maximized "$client" "fullscreen restore to maximize"
send_state "$client" remove _NET_WM_STATE_MAXIMIZED_HORZ _NET_WM_STATE_MAXIMIZED_VERT
assert_normal "$client" "final restore"

kill "$client_pid"; wait "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb EWMH maximize compatibility scenario"
