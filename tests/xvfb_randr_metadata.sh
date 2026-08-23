#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:159}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
monitor_bin=${BOX2430_RANDR_MONITOR_BIN:-./build/debug/x11-randr-monitor}
tmp_dir=$(mktemp -d)
xvfb_pid= left_pid= overlay_pid= renamed_pid= old_name_pid= wm_pid= client_pid=

cleanup() {
    for pid in "$client_pid" "$wm_pid" "$old_name_pid" "$renamed_pid" \
        "$overlay_pid" "$left_pid" "$xvfb_pid"; do
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
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
window_named() {
    DISPLAY=$display xdotool search --name "^$1$" | head -n 1
}
window_geometry() {
    DISPLAY=$display xwininfo -id "$1" | awk '
        /Absolute upper-left X:/ {x=$NF}
        /Absolute upper-left Y:/ {y=$NF}
        /Width:/ {w=$NF}
        /Height:/ {h=$NF}
        END {print x, y, w, h}'
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp \
    >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

DISPLAY=$display "$monitor_bin" set left 0 0 400 600 screen hold \
    >"$tmp_dir/left.log" 2>&1 &
left_pid=$!
wait_for "DISPLAY=$display xrandr --listmonitors 2>/dev/null | grep -q 'left'" ||
    fail "left logical monitor was not created"
DISPLAY=$display "$monitor_bin" set overlay 400 0 400 600 none hold \
    >"$tmp_dir/overlay.log" 2>&1 &
overlay_pid=$!
wait_for "test \"\$(DISPLAY=$display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 2" ||
    fail "overlay logical monitor was not created"
DISPLAY=$display xrandr --listmonitors >"$tmp_dir/monitors-before.log"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-native-bar-top.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-1$' >/dev/null 2>&1" ||
    fail "two-monitor WM did not start"
bar0=$(window_named box2430-bar-0)
bar1=$(window_named box2430-bar-1)
tab0=$(window_named box2430-tabbar-0)
tab1=$(window_named box2430-tabbar-1)

# Put semantic selection and a focused client on the no-output monitor.
DISPLAY=$display xdotool mousemove --sync 600 300 click 1
DISPLAY=$display "$fixture_bin" NORMAL MetadataOnlyClient 500 120 180 120 \
    >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^MetadataOnlyClient$' >/dev/null 2>&1" ||
    fail "metadata-only client did not map"
client=$(window_named MetadataOnlyClient)
wait_active "$client" || fail "metadata-only client was not focused"
before_geometry=$(window_geometry "$client")

# Rename only the second logical monitor under one test-fixture server grab.
# The WM can receive the existing root trigger only after the server contains
# the complete same-count/same-geometry replacement observation.
DISPLAY=$display "$monitor_bin" rename overlay renamed 400 0 400 600 none hold \
    >"$tmp_dir/renamed.log" 2>&1 &
renamed_pid=$!
wait_for "test \"\$(DISPLAY=$display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 2" ||
    fail "renamed logical monitor count is wrong"
wait_for "DISPLAY=$display xrandr --listmonitors 2>/dev/null | grep -q 'renamed'" ||
    fail "logical monitor metadata did not change"
DISPLAY=$display xrandr --listmonitors >"$tmp_dir/monitors-after.log"
DISPLAY=$display "$monitor_bin" notify-root

# A later key event synchronizes behind the ConfigureNotify. Before changing
# workspace, verify the accepted observation did not disturb live projection.
wait_active "$client" || fail "metadata-only reconciliation changed focus"
after_geometry=$(window_geometry "$client")
if [ "$after_geometry" != "$before_geometry" ]; then
    cat "$tmp_dir/monitors-before.log" "$tmp_dir/monitors-after.log" >&2
    fail "metadata-only reconciliation changed client geometry ($before_geometry -> $after_geometry)"
fi
[ "$(window_named box2430-bar-0)" = "$bar0" ] ||
    fail "metadata-only reconciliation recreated monitor-0 bar"
[ "$(window_named box2430-bar-1)" = "$bar1" ] ||
    fail "metadata-only reconciliation recreated monitor-1 bar"
[ "$(window_named box2430-tabbar-0)" = "$tab0" ] ||
    fail "metadata-only reconciliation recreated monitor-0 tab bar"
[ "$(window_named box2430-tabbar-1)" = "$tab1" ] ||
    fail "metadata-only reconciliation recreated monitor-1 tab bar"

DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsUnMapped'" ||
    fail "metadata-only reconciliation changed selected-monitor/workspace ownership"
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsViewable'" ||
    fail "metadata-only client did not return on its original workspace"
wait_active "$client" || fail "metadata-only client focus did not restore"

# Add a same-geometry monitor carrying the old name. The already accepted
# `renamed` identity must retain the old monitor-1 state and its UI resources;
# a stale snapshot would instead match that state to the new `overlay` slot.
DISPLAY=$display "$monitor_bin" set overlay 400 0 400 600 none hold \
    >"$tmp_dir/old-name.log" 2>&1 &
old_name_pid=$!
wait_for "test \"\$(DISPLAY=$display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 3" ||
    fail "same-geometry identity probe monitor was not created"
DISPLAY=$display "$monitor_bin" notify-root
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-2$' >/dev/null 2>&1" ||
    fail "three-monitor reconciliation did not complete"
[ "$(window_named box2430-bar-1)" = "$bar1" ] ||
    fail "accepted metadata was not used for later monitor continuity"
[ "$(window_named box2430-tabbar-1)" = "$tab1" ] ||
    fail "accepted metadata did not preserve tab-bar continuity"
wait_active "$client" || fail "identity probe disturbed focused client"
[ "$(window_geometry "$client")" = "$before_geometry" ] ||
    fail "identity probe changed continued client geometry"

kill "$client_pid"; wait "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q 'box2430: X11 error' "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: live RandR metadata-only observation replacement and projection stability"
