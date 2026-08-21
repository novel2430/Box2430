#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:135}
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

cp tests/fixtures/config-restart.toml "$tmp_dir/config.toml"
Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c "$tmp_dir/config.toml" >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
DISPLAY=$display xterm -title RestartClient >"$tmp_dir/client.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name RestartClient >/dev/null 2>&1" || fail "client missing"
window=$(DISPLAY=$display xdotool search --name RestartClient | head -n 1)
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3}')\" = 2" ||
    fail "initial config was not applied"

DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $window | grep -q 'Map State: IsUnMapped'" ||
    fail "client was not hidden on the inactive workspace before restart"

sed -i 's/width = 2/width = 7/' "$tmp_dir/config.toml"
DISPLAY=$display xdotool key super+r
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3}')\" = 7" ||
    fail "restart did not exec and reload startup config"
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $window | grep -q 'Map State: IsViewable'" ||
    fail "hidden workspace client was not rediscovered after restart"
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $(printf '0x%x' "$window")" ||
    fail "hidden workspace client was not managed after restart"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$window")" ||
    fail "hidden workspace client did not regain focus after restart"
kill -0 "$wm_pid" 2>/dev/null || fail "restart did not preserve a running WM process"

kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "restart exec failed\|box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "restart logged an error"
fi
echo "PASS: Xvfb process restart/config reload scenario"
