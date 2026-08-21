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

kill "$right_pid" "$left_pid" 2>/dev/null || true
right_pid= left_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xephyr per-monitor native-bar title/mode/true-center scenario"
