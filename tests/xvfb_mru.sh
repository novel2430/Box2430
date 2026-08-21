#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:133}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= one_pid= two_pid= three_pid= wm_stopped=

cleanup() {
    if [ -n "$wm_stopped" ] && [ -n "$wm_pid" ]; then
        kill -CONT "$wm_pid" 2>/dev/null || true
    fi
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

DISPLAY=$display xterm -title MruOne >"$tmp_dir/one.log" 2>&1 & one_pid=$!
wait_for "DISPLAY=$display xdotool search --name MruOne >/dev/null 2>&1" || fail "first client missing"
one=$(DISPLAY=$display xdotool search --name MruOne | head -n 1)
DISPLAY=$display xterm -title MruTwo >"$tmp_dir/two.log" 2>&1 & two_pid=$!
wait_for "DISPLAY=$display xdotool search --name MruTwo >/dev/null 2>&1" || fail "second client missing"
two=$(DISPLAY=$display xdotool search --name MruTwo | head -n 1)
DISPLAY=$display xterm -title MruThree >"$tmp_dir/three.log" 2>&1 & three_pid=$!
wait_for "DISPLAY=$display xdotool search --name MruThree >/dev/null 2>&1" || fail "third client missing"
three=$(DISPLAY=$display xdotool search --name MruThree | head -n 1)
wait_active "$three" || fail "last mapped client was not focused"

# Queue a complete Alt-Tab while the WM cannot process the initiating press.
# A synchronous passive grab must preserve both releases until the WM takes
# over the keyboard; otherwise Alt release escapes to the newly focused client
# and the next cycle continues the stale snapshot.
kill -STOP "$wm_pid"
wm_stopped=1
DISPLAY=$display xdotool keydown alt key Tab keyup alt
kill -CONT "$wm_pid"
wm_stopped=
wait_active "$two" || fail "first one-step MRU cycle did not select second-newest client"
DISPLAY=$display xdotool keydown alt key Tab keyup alt
wait_active "$three" || fail "queued modifier release did not commit the MRU cycle"

# The frozen snapshot must traverse Three -> Two -> One without the
# intermediate focus on Two turning the second step into a Two/Three toggle.
DISPLAY=$display xdotool keydown alt key Tab
wait_active "$two" || fail "first MRU step did not select second-newest client"
DISPLAY=$display xdotool key Tab
wait_active "$one" || fail "held MRU cycle did not walk its frozen snapshot"
DISPLAY=$display xdotool keyup alt

# Releasing Alt commits One as newest. A new cycle therefore starts from the
# new real order One, Three, Two and moves to Three.
DISPLAY=$display xdotool keydown alt key Tab
wait_active "$three" || fail "modifier release did not commit final MRU focus"
DISPLAY=$display xdotool keyup alt

kill "$three_pid" "$two_pid" "$one_pid" 2>/dev/null || true
three_pid= two_pid= one_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb held-modifier MRU snapshot scenario"
