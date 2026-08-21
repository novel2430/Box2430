#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:199}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
lifecycle_bin=${BOX2430_LIFECYCLE_BIN:-./build/debug/x11-lifecycle-client}
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

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-lifecycle.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display "$lifecycle_bin" ordinary VisibilityClient >"$tmp_dir/client.ids" 2>"$tmp_dir/client.log" &
client_pid=$!
wait_for "test -s $tmp_dir/client.ids" || fail "visibility client did not start"
client=$(head -n 1 "$tmp_dir/client.ids")
client_hex=$(printf '0x%x' "$client")
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $client_hex" || fail "visibility client was not managed"

# Move the focused client to inactive workspace 2 without following it.
DISPLAY=$display xdotool key super+shift+2
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsUnMapped'" || fail "inactive workspace client remained mapped"

# A repeated client map request must not override WM-owned workspace visibility.
DISPLAY=$display "$lifecycle_bin" map "$client"
sleep 0.1
DISPLAY=$display xwininfo -id "$client" | grep -q 'Map State: IsUnMapped' || fail "MapRequest exposed an inactive workspace client"
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsViewable'" || fail "workspace activation did not reveal client after rejected MapRequest"

# Hide it again, then withdraw synthetically while the WM-generated unmap may
# still have an ignored_unmaps event pending.  Withdrawal owns lifecycle.
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsUnMapped'" || fail "client did not hide before withdrawal"
DISPLAY=$display "$lifecycle_bin" withdraw "$client"
wait_for "! DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $client_hex" || fail "synthetic withdrawal left stale managed client state"
wait_for "DISPLAY=$display xprop -id $client WM_STATE | grep -q 'window state: Withdrawn'" || fail "synthetic withdrawal did not normalize WM_STATE"

DISPLAY=$display xdotool key super+2
sleep 0.1
DISPLAY=$display xwininfo -id "$client" | grep -q 'Map State: IsUnMapped' || fail "withdrawn client remapped on workspace activation"
DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi "$client_hex" && fail "withdrawn client returned to managed client list"

kill "$wm_pid"
wait "$wm_pid"
wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb managed visibility/MapRequest/synthetic withdrawal scenario"
