#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:139}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
color_bin=${BOX2430_COLOR_BIN:-./build/debug/x11-window-color}
tmp_dir=$(mktemp -d)
xephyr_pid= wm_pid= left_pid= right_pid=

cleanup() {
    for pid in "$right_pid" "$left_pid" "$wm_pid" "$xephyr_pid"; do
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
color_width() {
    set -- $(DISPLAY=$display "$color_bin" "$1" "$2")
    echo $(($3 - $1 + 1))
}
assert_centered() {
    window=$1 expected=$2 label=$3
    set -- $(DISPLAY=$display "$color_bin" "$window" '#000055')
    [ $(($1 + $3 + 1)) -eq "$expected" ] || fail "$label"
}
workspace_x() {
    set -- $(DISPLAY=$display "$color_bin" "$1" "$2")
    echo $((($1 + $3) / 2))
}

DISPLAY=${DISPLAY:-:0} Xephyr "$display" \
    -screen 800x600+0+0 -screen 640x480+800+0 +xinerama -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-bar-widgets-multimon.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "monitor-0 bar missing"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-1$' >/dev/null 2>&1" || fail "monitor-1 bar missing"
bar0=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
bar1=$(DISPLAY=$display xdotool search --name '^box2430-bar-1$' | head -n 1)

DISPLAY=$display xterm -title Left >"$tmp_dir/left.log" 2>&1 &
left_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Left$' >/dev/null 2>&1" || fail "left client missing"
wait_for "DISPLAY=$display $color_bin $bar0 '#000055' >/dev/null 2>&1" || fail "left monitor title missing"

DISPLAY=$display xdotool key super+ctrl+Right
wait_pointer_ge 800 || fail "monitor 1 was not selected"
DISPLAY=$display xterm -title 'A much longer title remembered by the non-selected second monitor' >"$tmp_dir/right.log" 2>&1 &
right_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^A much longer title remembered' >/dev/null 2>&1" || fail "right client missing"
wait_for "DISPLAY=$display $color_bin $bar1 '#000055' >/dev/null 2>&1" || fail "right monitor title missing"
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display $color_bin $bar1 '#660066' >/dev/null 2>&1" || fail "right monitor mode did not enter MONOCLE"
wait_for "DISPLAY=$display xdotool search --name '^box2430-tabbar-1$' >/dev/null 2>&1" || fail "right monitor tab bar missing"
tab1=$(DISPLAY=$display xdotool search --name '^box2430-tabbar-1$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $tab1 | grep -q 'Map State: IsViewable'" || fail "right MONOCLE tab bar not visible"

DISPLAY=$display xdotool key super+ctrl+Left
wait_pointer_lt 800 || fail "monitor 0 was not restored"
wait_for "DISPLAY=$display $color_bin $bar1 '#000055' >/dev/null 2>&1" || fail "non-selected monitor lost semantic title"
right_width=$(color_width "$bar1" '#000055')
left_width=$(color_width "$bar0" '#000055')
[ "$right_width" -gt "$left_width" ] || fail "non-selected monitor title fell back to global focused client"
assert_centered "$bar0" 800 "monitor-0 title is not physically centered"
assert_centered "$bar1" 640 "monitor-1 title is not physically centered"
DISPLAY=$display xwininfo -id "$tab1" | grep -q 'Map State: IsViewable' || fail "non-selected MONOCLE tab disappeared"

# Changing only monitor 0's active workspace must not clear monitor 1's title.
DISPLAY=$display xdotool key super+2
wait_for "! DISPLAY=$display $color_bin $bar0 '#000055' >/dev/null 2>&1" || fail "monitor-0 empty workspace retained stale title"
wait_for "DISPLAY=$display $color_bin $bar1 '#000055' >/dev/null 2>&1" || fail "monitor-0 workspace change affected monitor-1 title"

# A workspace-bar click selects its monitor even when the clicked workspace is
# empty and already active.  Unlike explicit monitor navigation, it must leave
# the pointer at the clicked label.
kill "$right_pid" 2>/dev/null || true
right_pid=
wait_for "! DISPLAY=$display $color_bin $bar1 '#000055' >/dev/null 2>&1" || fail "empty right workspace retained stale title"
active_x=$(workspace_x "$bar1" '#003300')
DISPLAY=$display xdotool mousemove --window "$bar1" "$active_x" 12 click 1
[ "$(pointer_x)" -lt 900 ] || fail "workspace click warped pointer away from label"
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '800, 24, 640, 456'" || fail "active empty workspace click did not select monitor 1"
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display $color_bin $bar1 '#006600' >/dev/null 2>&1" || fail "command after active workspace click targeted wrong monitor"

# Selecting a different empty workspace through the other monitor's bar must
# update both that monitor's active workspace and the global monitor selection.
DISPLAY=$display xdotool key super+ctrl+Left
wait_pointer_lt 800 || fail "monitor 0 was not restored before empty workspace test"
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 24, 800, 576'" || fail "monitor 0 workarea was not restored"
set -- $(DISPLAY=$display "$color_bin" "$bar1" '#003300')
workspace_width=$(($3 - $1 + 1))
next_workspace_x=$(($3 + 1 + workspace_width / 2))
DISPLAY=$display xdotool mousemove --window "$bar1" "$next_workspace_x" 12 click 1
[ "$(pointer_x)" -lt 900 ] || fail "empty workspace click warped pointer away from label"
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '800, 24, 640, 456'" || fail "different empty workspace click did not select monitor 1"
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display $color_bin $bar1 '#660066' >/dev/null 2>&1" || fail "command after empty workspace switch targeted wrong monitor"

# Clicking exposed root background selects the monitor under the pointer without
# using the explicit monitor command's center warp.
DISPLAY=$display xdotool mousemove 100 300 click 1
[ "$(pointer_x)" = 100 ] || fail "root click warped pointer away from click position"
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 24, 800, 576'" || fail "root click did not select monitor 0"
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display $color_bin $bar0 '#660066' >/dev/null 2>&1" || fail "command after root click targeted wrong monitor"

kill "$left_pid" 2>/dev/null || true
left_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xephyr per-monitor native-bar title/mode/true-center scenario"
