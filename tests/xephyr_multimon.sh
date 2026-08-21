#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:138}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
lifecycle_bin=${BOX2430_LIFECYCLE_BIN:-./build/debug/x11-lifecycle-client}
tmp_dir=$(mktemp -d)
xephyr_pid= wm_pid= transient_pid= one_pid= two_pid= drag_pid=

cleanup() {
    for pid in "$drag_pid" "$two_pid" "$one_pid" "$transient_pid" "$wm_pid" "$xephyr_pid"; do
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
pointer_x() { DISPLAY=$display xdotool getmouselocation --shell | awk -F= '/^X=/{print $2}'; }
window_x() {
    DISPLAY=$display xdotool getwindowgeometry --shell "$1" |
        awk -F= '/^X=/{print $2}'
}
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
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

DISPLAY=${DISPLAY:-:0} Xephyr "$display" \
    -screen 800x600+0+0 -screen 640x480+800+0 +xinerama -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"

# Startup discovery must manage this dialog after its parent, then inherit the
# parent's rule-selected monitor and workspace.
DISPLAY=$display "$lifecycle_bin" transient MultimonParent MultimonDialog >"$tmp_dir/transient.ids" 2>"$tmp_dir/transient.log" &
transient_pid=$!
wait_for "test -s $tmp_dir/transient.ids" || fail "startup transient fixture did not start"
read -r transient_parent transient_dialog <"$tmp_dir/transient.ids"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-multimon.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
wait_for "DISPLAY=$display xwininfo -id $transient_parent | grep -q 'Map State: IsUnMapped'" || fail "startup parent was not placed on monitor 2 workspace 2"
wait_for "DISPLAY=$display xwininfo -id $transient_dialog | grep -q 'Map State: IsUnMapped'" || fail "startup transient did not inherit hidden workspace"
DISPLAY=$display xdotool key super+ctrl+Right
wait_pointer_ge 800 || fail "monitor 2 could not be selected for transient test"
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $transient_parent | grep -q 'Map State: IsViewable'" || fail "startup parent did not appear on monitor 2 workspace 2"
wait_for "DISPLAY=$display xwininfo -id $transient_dialog | grep -q 'Map State: IsViewable'" || fail "startup transient did not follow parent workspace"
[ "$(window_x "$transient_dialog")" -ge 800 ] || fail "startup transient did not inherit parent monitor"
DISPLAY=$display xdotool key super+1
DISPLAY=$display xdotool key super+ctrl+Left
wait_pointer_lt 800 || fail "monitor 0 could not be restored after transient test"
kill "$transient_pid"
wait "$transient_pid" 2>/dev/null || true
transient_pid=
wait_for "! DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $(printf '0x%x' "$transient_dialog")" || fail "startup transient fixture did not withdraw"

DISPLAY=$display xterm -title MonitorOne >"$tmp_dir/one.log" 2>&1 & one_pid=$!
wait_for "DISPLAY=$display xdotool search --name MonitorOne >/dev/null 2>&1" || fail "first client missing"
one=$(DISPLAY=$display xdotool search --name MonitorOne | head -n 1)

# Explicit monitor navigation selects an empty monitor and warps into it; the
# independent workspace on monitor 0 remains mapped.
DISPLAY=$display xdotool key super+ctrl+Right
wait_pointer_ge 800 || fail "empty monitor was not selected"
DISPLAY=$display xwininfo -id "$one" | grep -q 'Map State: IsViewable' ||
    fail "selecting another monitor changed source workspace visibility"

DISPLAY=$display xterm -title MonitorTwo >"$tmp_dir/two.log" 2>&1 & two_pid=$!
wait_for "DISPLAY=$display xdotool search --name MonitorTwo >/dev/null 2>&1" || fail "second client missing"
two=$(DISPLAY=$display xdotool search --name MonitorTwo | head -n 1)
wait_active "$two" || fail "client on selected second monitor was not focused"

# Per-monitor workspace 2 hides only monitor 1's client. Returning selection
# to monitor 0 restores its independent active workspace/focus.
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $two | grep -q 'Map State: IsUnMapped'" ||
    fail "second monitor workspace did not switch independently"
DISPLAY=$display xdotool key super+ctrl+Left
wait_pointer_lt 800 || fail "monitor prev did not select monitor 0"
wait_active "$one" || fail "monitor 0 focus did not restore"

# Default move-monitor targets the destination monitor's active workspace but
# does not follow. Selecting monitor 1 then restores/focuses the moved client.
DISPLAY=$display xdotool key super+shift+Right
wait_for "DISPLAY=$display xwininfo -id $one | grep -q 'Map State: IsViewable'" ||
    fail "move-monitor did not use target active workspace"
wait_pointer_lt 800 || fail "move-monitor unexpectedly followed"
[ "$(window_x "$one")" -ge 800 ] ||
    fail "move-monitor changed ownership without translating geometry"
DISPLAY=$display xdotool key super+ctrl+Right
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q '0x0' ||
    fail "monitor selection invented focus history for moved client"
DISPLAY=$display xdotool mousemove --window "$one" 20 20 click 1
wait_active "$one" || fail "moved client was not on monitor 1 active workspace"

# --keep-workspace plus --follow sends workspace-2 client to monitor 0's local
# workspace 2 and switches/selects that destination.
DISPLAY=$display xdotool key super+alt+Right
wait_active "$one" || fail "move-monitor --follow lost focused client"
[ "$(window_x "$one")" -lt 800 ] ||
    fail "move-monitor --keep-workspace did not translate back to monitor 0"
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $one | grep -q 'Map State: IsUnMapped'" ||
    fail "--keep-workspace did not preserve local workspace number"

# Direct workspace follow moves One back to workspace 1 and retains focus.
DISPLAY=$display xdotool key super+2
DISPLAY=$display xdotool key super+alt+1
wait_active "$one" || fail "move-workspace --follow did not retain focus"
DISPLAY=$display xterm -title DragAcross >"$tmp_dir/drag.log" 2>&1 & drag_pid=$!
wait_for "DISPLAY=$display xdotool search --name DragAcross >/dev/null 2>&1" || fail "drag client missing"
drag=$(DISPLAY=$display xdotool search --name DragAcross | head -n 1)
DISPLAY=$display xdotool keydown super mousemove --window "$drag" 20 20 \
    mousedown 1 mousemove --sync 1050 220 mouseup 1 keyup super
wait_active "$drag" || fail "cross-monitor drag lost focus"
[ "$(window_x "$drag")" -ge 800 ] ||
    fail "cross-monitor drag did not retain its root-space geometry"
[ "$(window_x "$drag")" -lt 1440 ] ||
    fail "cross-monitor drag was translated twice"

# If center-based release assigned monitor 1, moving to prev with --follow must
# select monitor 0. The pointer warp makes that internal ownership observable.
DISPLAY=$display xdotool key super+shift+Left
DISPLAY=$display xdotool key super+ctrl+Right
wait_pointer_ge 800 ||
    fail "cross-monitor release did not update monitor ownership by center"

mkdir -p build/evidence
DISPLAY=$display xwd -silent -root -out build/evidence/xephyr-multimon.xwd
convert build/evidence/xephyr-multimon.xwd build/evidence/xephyr-multimon.png

kill "$drag_pid" "$two_pid" "$one_pid" 2>/dev/null || true
drag_pid= two_pid= one_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,180p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xephyr per-monitor workspace/move/cross-drag scenario"
