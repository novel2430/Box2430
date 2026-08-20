#!/bin/sh
set -eu

display=${MICROBOX_TEST_DISPLAY:-:129}
microbox_bin=${MICROBOX_BIN:-./build/debug/microbox}
fixture_bin=${MICROBOX_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid= dock_pid= desktop_pid= notification_pid=

cleanup() {
    for pid in "$client_pid" "$dock_pid" "$desktop_pid" "$notification_pid" "$wm_pid" "$xvfb_pid"; do
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
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" || fail "initial workarea missing"

DISPLAY=$display xterm -title WorkareaClient -geometry 30x8 >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name WorkareaClient >/dev/null 2>&1" || fail "normal client missing"
client=$(DISPLAY=$display xdotool search --name WorkareaClient | head -n 1)
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 796" || fail "initial maximize failed"

DISPLAY=$display "$fixture_bin" DESKTOP FixtureDesktop 0 0 800 600 >"$tmp_dir/desktop.log" 2>&1 &
desktop_pid=$!
wait_for "DISPLAY=$display xdotool search --name FixtureDesktop >/dev/null 2>&1" || fail "desktop window missing"
desktop=$(DISPLAY=$display xdotool search --name FixtureDesktop | head -n 1)
[ "$(field "$desktop" 'Border width:')" = 0 ] || fail "desktop received normal border"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi "$(printf '0x%x' "$client")" || fail "desktop stole focus"

DISPLAY=$display "$fixture_bin" NOTIFICATION FixtureNotification 50 60 120 40 >"$tmp_dir/notification.log" 2>&1 &
notification_pid=$!
wait_for "DISPLAY=$display xdotool search --name FixtureNotification >/dev/null 2>&1" || fail "notification window missing"
notification=$(DISPLAY=$display xdotool search --name FixtureNotification | head -n 1)
[ "$(field "$notification" 'Border width:')" = 0 ] || fail "notification received normal border"
[ "$(field "$notification" 'Absolute upper-left X:')" = 50 ] || fail "notification was normally placed"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi "$(printf '0x%x' "$client")" || fail "notification stole focus"

DISPLAY=$display "$fixture_bin" DOCK FixtureDock 0 0 800 30 30 >"$tmp_dir/dock.log" 2>&1 &
dock_pid=$!
wait_for "DISPLAY=$display xdotool search --name FixtureDock >/dev/null 2>&1" || fail "dock window missing"
dock=$(DISPLAY=$display xdotool search --name FixtureDock | head -n 1)
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 30, 800, 570'" || fail "dock strut did not update workarea"
[ "$(field "$client" 'Absolute upper-left Y:')" = 30 ] || fail "maximized client did not follow workarea"
[ "$(field "$client" 'Height:')" = 566 ] || fail "maximized height did not follow workarea"
[ "$(field "$dock" 'Border width:')" = 0 ] || fail "dock received normal border"

first=$(DISPLAY=$display xprop -root _NET_CLIENT_LIST_STACKING | grep -o '0x[0-9a-fA-F]*' | head -n 1)
[ "$(printf '0x%x' "$desktop")" = "$first" ] || fail "desktop is not bottom stacking tier"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 600" || fail "fullscreen did not cover dock workarea"
last=$(DISPLAY=$display xprop -root _NET_CLIENT_LIST_STACKING | grep -o '0x[0-9a-fA-F]*' | tail -n 1)
[ "$(printf '0x%x' "$client")" = "$last" ] || fail "fullscreen is not top stacking tier"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 566" || fail "fullscreen did not restore maximized workarea"

kill "$dock_pid"; dock_pid=
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" || fail "dock removal did not restore workarea"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 596" || fail "maximize did not expand after dock removal"

kill "$desktop_pid" 2>/dev/null || true; desktop_pid=
kill "$notification_pid" 2>/dev/null || true; notification_pid=
kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "microbox: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb special-window/strut/stacking scenario"
