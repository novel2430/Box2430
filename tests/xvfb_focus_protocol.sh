#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:140}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
focus_bin=${BOX2430_FOCUS_BIN:-./build/debug/x11-focus-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= take_pid= none_pid=

cleanup() {
    for pid in "$none_pid" "$take_pid" "$wm_pid" "$xvfb_pid"; do
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

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" >"$tmp_dir/wm.log" 2>&1 & wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display "$focus_bin" take TakeFocusClient "$tmp_dir/take.marker" \
    >"$tmp_dir/take.log" 2>&1 & take_pid=$!
wait_for "test -f $tmp_dir/take.marker" || fail "WM_TAKE_FOCUS was not delivered"
take=$(DISPLAY=$display xdotool search --name TakeFocusClient | head -n 1)
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$take")" ||
    fail "globally-active client was not tracked as active"

# A client explicitly declaring InputHint=False without WM_TAKE_FOCUS cannot
# become the WM's keyboard-focused client.
DISPLAY=$display "$focus_bin" none NoFocusClient "$tmp_dir/none.marker" \
    >"$tmp_dir/none.log" 2>&1 & none_pid=$!
wait_for "DISPLAY=$display xdotool search --name NoFocusClient >/dev/null 2>&1" || fail "no-focus client missing"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi "$(printf '0x%x' "$take")" ||
    fail "non-focusable client stole active keyboard focus"
[ ! -e "$tmp_dir/none.marker" ] || fail "non-focusable client received WM_TAKE_FOCUS"

kill "$take_pid" 2>/dev/null || true; take_pid=
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q 0x0" ||
    fail "focus fallback selected a non-focusable client"

kill "$none_pid" 2>/dev/null || true; none_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb ICCCM WM_TAKE_FOCUS/input-hint scenario"
