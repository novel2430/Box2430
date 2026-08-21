#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:136}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid= second_pid=

cleanup() {
    for pid in "$second_pid" "$client_pid" "$wm_pid" "$xvfb_pid"; do
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

DISPLAY=$display xterm -title OlderClient >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name OlderClient >/dev/null 2>&1" ||
    fail "older client missing"
older=$(DISPLAY=$display xdotool search --name OlderClient | head -n 1)
DISPLAY=$display xterm -title NewestClient >"$tmp_dir/second.log" 2>&1 &
second_pid=$!
wait_for "DISPLAY=$display xdotool search --name NewestClient >/dev/null 2>&1" ||
    fail "newest client missing"
newest=$(DISPLAY=$display xdotool search --name NewestClient | head -n 1)
client=$newest
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Border width:/ {print \$3}')\" = 2" ||
    fail "client was not managed"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q '0x0' ||
    fail "focus_on_map=false was ignored"

# focus_on_map=false leaves the new client alone initially. Once its workspace
# is activated again, however, a missing history entry falls back from the
# newest end of stable order instead of leaving the workspace with root focus.
DISPLAY=$display xdotool key super+2 key super+1
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsViewable'" ||
    fail "client did not remap"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$client")" ||
    fail "workspace did not focus its newest fallback client"

# Once a client has actually been focused, workspace activation restores the
# focus-stack head rather than recomputing from stable order.
DISPLAY=$display xdotool key alt+Tab
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$older")" ||
    fail "older client did not receive focus"
DISPLAY=$display xdotool key super+2 key super+1
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$older")" ||
    fail "workspace did not restore its most recent focus"
DISPLAY=$display xdotool key alt+Tab
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$client")" ||
    fail "stable focus cycle changed after workspace restoration"

kill "$client_pid" 2>/dev/null || true; client_pid=
wait_for "! DISPLAY=$display xwininfo -id $older >/dev/null 2>&1" ||
    fail "older client did not withdraw"

# Moving a client to a workspace does not itself add focus history. If that
# destination has no history, activation falls back to its stable order, focuses
# the moved client, and records that real focus transition in the focus stack.
DISPLAY=$display xdotool key super+shift+2
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsUnMapped'" ||
    fail "client was not sent to workspace 2"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q '0x0' ||
    fail "empty source workspace retained focus"
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsViewable'" ||
    fail "sent client did not remap"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$client")" ||
    fail "destination workspace did not focus the sent client"

kill "$second_pid" 2>/dev/null || true; second_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb workspace focus-history fallback scenario"
