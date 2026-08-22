#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:162}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
stacking_bin=${BOX2430_STACKING_BIN:-./build/debug/x11-stacking-order}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid= overlay_pid=

cleanup() {
    for pid in "$overlay_pid" "$client_pid" "$wm_pid" "$xvfb_pid"; do
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
assert_below() {
    DISPLAY=$display "$stacking_bin" "$1" "$2" || fail "$3"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-native-bar-top.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" ||
    fail "native bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)

DISPLAY=$display xterm -title OverlayStackClient -geometry 30x8 \
    >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^OverlayStackClient$' >/dev/null 2>&1" ||
    fail "ordinary client missing"
client=$(DISPLAY=$display xdotool search --name '^OverlayStackClient$' | head -n 1)
assert_below "$client" "$bar" "ordinary client is not below native bar"

DISPLAY=$display "$fixture_bin" OR_NOTIFICATION DunstLikeNotification 0 0 260 80 \
    >"$tmp_dir/overlay.log" 2>&1 &
overlay_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^DunstLikeNotification$' >/dev/null 2>&1" ||
    fail "override-redirect notification missing"
overlay=$(DISPLAY=$display xdotool search --name '^DunstLikeNotification$' | head -n 1)
DISPLAY=$display xwininfo -id "$overlay" | grep -qi 'Override Redirect State: yes' ||
    fail "notification fixture is not override-redirect"
if DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi "$(printf '0x%x' "$overlay")"; then
    fail "override-redirect notification leaked into _NET_CLIENT_LIST"
fi
assert_below "$bar" "$overlay" "notification did not start above native bar"

# Entering MONOCLE exercises client_raise() and enforce_stacking(), and maps the
# native tab bar.  The old absolute-raise policy moved both native UI windows
# above a dunst-style override-redirect notification here.
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' >/dev/null 2>&1" ||
    fail "MONOCLE tab bar missing"
tab=$(DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $tab | grep -q 'Map State: IsViewable'" ||
    fail "MONOCLE tab bar did not map"
assert_below "$client" "$bar" "ordinary client escaped above native bar after restack"
assert_below "$bar" "$tab" "native bar is not below MONOCLE tab bar"
assert_below "$bar" "$overlay" "restack raised native bar above notification"
assert_below "$tab" "$overlay" "restack raised MONOCLE tab bar above notification"

DISPLAY=$display xdotool key super+m
assert_below "$bar" "$overlay" "second restack raised native bar above notification"

kill "$overlay_pid" 2>/dev/null || true
wait "$overlay_pid" 2>/dev/null || true
overlay_pid=
kill "$client_pid" 2>/dev/null || true
wait "$client_pid" 2>/dev/null || true
client_pid=
kill "$wm_pid"
wait "$wm_pid"
wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

# Also cover a WM restart/startup while a dunst-style notification is already
# visible.  The notification remains unmanaged, but the newly created native
# UI must start below it rather than relying on a later notification raise.
DISPLAY=$display "$fixture_bin" OR_NOTIFICATION DunstLikeStartup 0 0 260 80 \
    >"$tmp_dir/startup-overlay.log" 2>&1 &
overlay_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^DunstLikeStartup$' >/dev/null 2>&1" ||
    fail "startup override-redirect notification missing"
overlay=$(DISPLAY=$display xdotool search --name '^DunstLikeStartup$' | head -n 1)

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-native-bar-top.toml \
    >"$tmp_dir/startup-wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" ||
    fail "startup native bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
assert_below "$bar" "$overlay" "startup native bar covered existing notification"

kill "$wm_pid"
wait "$wm_pid"
wm_pid=
kill "$overlay_pid" 2>/dev/null || true
wait "$overlay_pid" 2>/dev/null || true
overlay_pid=
if grep -q "box2430: X11 error" "$tmp_dir/startup-wm.log"; then
    sed -n '1,160p' "$tmp_dir/startup-wm.log" >&2
    fail "unexpected X11 error during startup overlay scenario"
fi

echo "PASS: Xvfb override-redirect notification/native-UI stacking scenario"
