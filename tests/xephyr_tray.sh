#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:149}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tray_bin=${BOX2430_TRAY_BIN:-./build/debug/x11-tray-test-client}
stacking_bin=${BOX2430_STACKING_BIN:-./build/debug/x11-stacking-order}
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
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
parent_of() { DISPLAY=$display xwininfo -id "$1" -tree | awk '/Parent window id:/ {print $4; exit}'; }
pointer_x() { DISPLAY=$display xdotool getmouselocation --shell | awk -F= '/^X=/{print $2}'; }
wait_pointer_ge() {
    attempts=0
    while [ "$(pointer_x)" -lt "$1" ]; do
        attempts=$((attempts + 1)); [ "$attempts" -lt 250 ] || return 1
        sleep 0.02
    done
}
wait_pointer_lt() {
    attempts=0
    while [ "$(pointer_x)" -ge "$1" ]; do
        attempts=$((attempts + 1)); [ "$attempts" -lt 250 ] || return 1
        sleep 0.02
    done
}

DISPLAY=${DISPLAY:-:0} Xephyr "$display" \
    -screen 800x600+0+0 -screen 640x480+800+0 +xinerama -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tray.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "monitor-0 bar missing"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-1$' >/dev/null 2>&1" || fail "monitor-1 bar missing"
bar0=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
bar1=$(DISPLAY=$display xdotool search --name '^box2430-bar-1$' | head -n 1)

DISPLAY=$display "$tray_bin" icon TrayRelocationIcon 32 32 64 32 >"$tmp_dir/icon.out" 2>"$tmp_dir/icon.err" &
icon_pid=$!
wait_for "test -s $tmp_dir/icon.out" || fail "tray icon did not dock"
set -- $(cat "$tmp_dir/icon.out"); icon=$1 host=$2
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 772" || fail "tray did not start on selected monitor 0"
[ "$(field "$host" 'Width:')" = 28 ] || fail "initial tray width incorrect"
[ "$(parent_of "$icon")" = "$host" ] || fail "icon not embedded in tray host"
DISPLAY=$display "$stacking_bin" "$bar0" "$host" || fail "tray host is not above monitor-0 bar"

# Selecting another monitor relocates the same host.  The embedded icon and
# selection remain alive; no undock/redock cycle is needed.
DISPLAY=$display xdotool key super+ctrl+Right
wait_pointer_ge 800 || fail "monitor 1 was not selected"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 1412" || fail "tray host did not relocate to monitor 1"
[ "$(parent_of "$icon")" = "$host" ] || fail "monitor relocation reparented tray icon"
current_host=$(DISPLAY=$display xwininfo -root -tree | awk '/"box2430-tray"/ {print $1; exit}')
[ "$current_host" = "$host" ] || fail "monitor relocation recreated tray host"
DISPLAY=$display "$stacking_bin" "$bar1" "$host" || fail "tray host is not above monitor-1 bar"

DISPLAY=$display xdotool key super+ctrl+Left
wait_pointer_lt 800 || fail "monitor 0 was not restored"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 772" || fail "tray host did not return to monitor 0"
[ "$(parent_of "$icon")" = "$host" ] || fail "return relocation disturbed icon embedding"

kill "$icon_pid"; wait "$icon_pid" 2>/dev/null || true; icon_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q 'box2430: X11 error' "$tmp_dir/wm.log"; then
    sed -n '1,180p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xephyr selected-monitor XEmbed tray relocation scenario"
