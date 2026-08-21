#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:133}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= one_pid= two_pid= three_pid=

cleanup() {
    for pid in "$three_pid" "$two_pid" "$one_pid" "$wm_pid" "$xvfb_pid"; do
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
        if [ "$attempts" -ge 150 ]; then return 1; fi
        sleep 0.02
    done
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display xterm -title CycleOne >"$tmp_dir/one.log" 2>&1 & one_pid=$!
wait_for "DISPLAY=$display xdotool search --name CycleOne >/dev/null 2>&1" || fail "first client missing"
one=$(DISPLAY=$display xdotool search --name CycleOne | head -n 1)
DISPLAY=$display xterm -title CycleTwo >"$tmp_dir/two.log" 2>&1 & two_pid=$!
wait_for "DISPLAY=$display xdotool search --name CycleTwo >/dev/null 2>&1" || fail "second client missing"
two=$(DISPLAY=$display xdotool search --name CycleTwo | head -n 1)
DISPLAY=$display xterm -title CycleThree >"$tmp_dir/three.log" 2>&1 & three_pid=$!
wait_for "DISPLAY=$display xdotool search --name CycleThree >/dev/null 2>&1" || fail "third client missing"
three=$(DISPLAY=$display xdotool search --name CycleThree | head -n 1)
wait_active "$three" || fail "last mapped client was not focused"

# Client order is insertion order One -> Two -> Three and focus does not mutate it.
DISPLAY=$display xdotool key alt+Tab
wait_active "$one" || fail "focus next did not wrap from Three to One"
DISPLAY=$display xdotool key alt+Tab
wait_active "$two" || fail "focus next did not advance from One to Two"

# An explicit non-cycle focus change must not alter the stable cycle order.
DISPLAY=$display xdotool windowactivate "$one"
wait_active "$one" || fail "explicit activation focus failed"
DISPLAY=$display xdotool key alt+Tab
wait_active "$two" || fail "ordinary focus reordered the stable client cycle"

# The reverse command uses exactly the same stable order.
DISPLAY=$display xdotool key super+k
wait_active "$one" || fail "focus prev did not move from Two to One"

# Removing the focused client falls forward to its next client in the same order.
kill "$one_pid"
one_pid=
wait_active "$two" || fail "focused-client removal did not fall forward in client order"

kill "$three_pid" "$two_pid" 2>/dev/null || true
three_pid= two_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb stable client-order focus cycle scenario"
