#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:150}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tray_bin=${BOX2430_TRAY_BIN:-./build/debug/x11-tray-test-client}
tmp_dir=$(mktemp -d)
xephyr_pid= wm_pid= icon_pid=

cleanup() {
    for pid in "$icon_pid" "$wm_pid" "$xephyr_pid"; do
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
parent_of() { DISPLAY=$display xwininfo -id "$1" -tree | awk '/Parent window id:/ {print $4; exit}'; }

DISPLAY=${DISPLAY:-:0} Xephyr "$display" -screen 800x600 -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tray.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display $tray_bin owner >/dev/null 2>&1" || fail "tray selection missing"

DISPLAY=$display "$tray_bin" icon TrayTopologyIcon 32 32 64 32 >"$tmp_dir/icon.out" 2>"$tmp_dir/icon.err" &
icon_pid=$!
wait_for "test -s $tmp_dir/icon.out" || fail "tray topology icon did not dock"
set -- $(cat "$tmp_dir/icon.out"); icon=$1 host=$2
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 772" || fail "initial tray geometry incorrect"
[ "$(parent_of "$icon")" = "$host" ] || fail "initial icon embedding incorrect"

# Monitor geometry reconciliation must only move/resize the stable root-level
# host.  Embedded clients remain parented to the same host throughout.
DISPLAY=$display xrandr -s 640x480
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 640" || fail "Xephyr root did not shrink"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 612" || fail "tray host did not follow shrunk monitor"
[ "$(parent_of "$icon")" = "$host" ] || fail "topology shrink disturbed tray icon embedding"
current_host=$(DISPLAY=$display xwininfo -root -tree | awk '/"box2430-tray"/ {print $1; exit}')
[ "$current_host" = "$host" ] || fail "topology shrink recreated tray host"

DISPLAY=$display xrandr -s 800x600
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 800" || fail "Xephyr root did not grow"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 772" || fail "tray host did not follow grown monitor"
[ "$(parent_of "$icon")" = "$host" ] || fail "topology grow disturbed tray icon embedding"
current_host=$(DISPLAY=$display xwininfo -root -tree | awk '/"box2430-tray"/ {print $1; exit}')
[ "$current_host" = "$host" ] || fail "topology grow recreated tray host"

kill "$icon_pid"; wait "$icon_pid" 2>/dev/null || true; icon_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q 'box2430: X11 error' "$tmp_dir/wm.log"; then
    sed -n '1,180p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xephyr XEmbed tray topology geometry/lifecycle scenario"
