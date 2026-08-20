#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:136}
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
        if [ "$attempts" -ge 200 ]; then return 1; fi
        sleep 0.02
    done
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-focus-history.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display xterm -title NeverFocused >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name NeverFocused >/dev/null 2>&1" ||
    fail "client missing"
client=$(DISPLAY=$display xdotool search --name NeverFocused | head -n 1)
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Border width:/ {print \$3}')\" = 2" ||
    fail "client was not managed"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q '0x0' ||
    fail "focus_on_map=false was ignored"

# Merely visiting another workspace must not invent focus history for a client
# that has never been focused.
DISPLAY=$display xdotool key super+2 key super+1
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsViewable'" ||
    fail "client did not remap"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q '0x0' ||
    fail "workspace return focused a never-focused client"

# Once focused, the workspace records and restores that exact client.
DISPLAY=$display xdotool mousemove --window "$client" 20 20 click 1
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$client")" ||
    fail "click did not establish focus history"
DISPLAY=$display xdotool key super+2 key super+1
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$client")" ||
    fail "workspace did not restore established focus history"

kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb absent-versus-established focus history scenario"
