#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:145}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
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
        if [ "$attempts" -ge 150 ]; then return 1; fi
        sleep 0.02
    done
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tabs.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

wait_for "DISPLAY=$display xdotool search --name box2430-tabbar-0 >/dev/null 2>&1" ||
    fail "tab bar window missing"
bar=$(DISPLAY=$display xdotool search --name box2430-tabbar-0 | head -n 1)

# Empty MONOCLE is an empty presentation state: the tab window exists as an
# internal UI resource, but stays unmapped until there is something to show.
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xwininfo -id $bar | grep -q 'Map State: IsUnMapped'" ||
    fail "empty MONOCLE workspace mapped the tab bar"

# The first client crosses the 0 -> 1 boundary. It must both map the tab bar
# and use the tab-excluded MONOCLE content area immediately.
DISPLAY=$display xterm -title EmptyTabClient >"$tmp_dir/client.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name EmptyTabClient >/dev/null 2>&1" ||
    fail "client missing"
client=$(DISPLAY=$display xdotool search --name EmptyTabClient | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $bar | grep -q 'Map State: IsViewable'" ||
    fail "first MONOCLE client did not map the tab bar"
[ "$(DISPLAY=$display xwininfo -id "$client" | awk '/Absolute upper-left Y:/ {print $4; exit}')" = 31 ] ||
    fail "first MONOCLE client did not reserve tab height"
[ "$(DISPLAY=$display xwininfo -id "$client" | awk '/Height:/ {print $2; exit}')" = 569 ] ||
    fail "first MONOCLE client did not use tab-excluded content height"

# Removing the final client crosses the 1 -> 0 boundary and must suppress the
# empty tab presentation again.
kill "$client_pid"; wait "$client_pid" 2>/dev/null || true; client_pid=
wait_for "DISPLAY=$display xwininfo -id $bar | grep -q 'Map State: IsUnMapped'" ||
    fail "tab bar remained mapped after final MONOCLE client disappeared"

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb empty MONOCLE tab presentation scenario"
