#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:134}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= one_pid= two_pid= three_pid=

cleanup() {
    for pid in "$three_pid" "$two_pid" "$one_pid" "$wm_pid" "$xvfb_pid"; do
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
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tabs.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display xterm -title TabOne >"$tmp_dir/one.log" 2>&1 & one_pid=$!
wait_for "DISPLAY=$display xdotool search --name TabOne >/dev/null 2>&1" || fail "first client missing"
one=$(DISPLAY=$display xdotool search --name TabOne | head -n 1)
DISPLAY=$display xterm -title TabTwo >"$tmp_dir/two.log" 2>&1 & two_pid=$!
wait_for "DISPLAY=$display xdotool search --name TabTwo >/dev/null 2>&1" || fail "second client missing"
two=$(DISPLAY=$display xdotool search --name TabTwo | head -n 1)

DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name box2430-tabbar-0 >/dev/null 2>&1" || fail "tab bar did not map"
bar=$(DISPLAY=$display xdotool search --name box2430-tabbar-0 | head -n 1)
[ "$(DISPLAY=$display xwininfo -id "$bar" | awk '/Height:/ {print $2; exit}')" = 31 ] ||
    fail "configured tab bar height was not applied"
[ "$(DISPLAY=$display xwininfo -id "$two" | awk '/Absolute upper-left Y:/ {print $4; exit}')" = 31 ] ||
    fail "MONOCLE client did not start below tab bar"
[ "$(DISPLAY=$display xwininfo -id "$two" | awk '/Height:/ {print $2; exit}')" = 569 ] ||
    fail "MONOCLE client did not use tab-excluded content height"
DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi "$(printf '0x%x' "$bar")" &&
    fail "WM-owned tab bar leaked into client list"

# A client mapped after entering MONOCLE must immediately use the MONOCLE
# content area while retaining its independent FREE geometry for mode exit.
DISPLAY=$display xterm -title TabThree -geometry 25x7+100+100 >"$tmp_dir/three.log" 2>&1 & three_pid=$!
wait_for "DISPLAY=$display xdotool search --name TabThree >/dev/null 2>&1" ||
    fail "MONOCLE-time client missing"
three=$(DISPLAY=$display xdotool search --name TabThree | head -n 1)
wait_active "$three" || fail "MONOCLE-time client did not focus"

# Keyboard tab selection uses the same stable visual tab order. An out-of-range
# index is a deliberate no-op rather than a command error or fallback focus.
DISPLAY=$display xdotool key alt+1
wait_active "$one" || fail "Alt+1 did not focus first MONOCLE tab"
DISPLAY=$display xdotool key alt+2
wait_active "$two" || fail "Alt+2 did not focus second MONOCLE tab"
DISPLAY=$display xdotool key alt+4
wait_active "$two" || fail "out-of-range keyboard tab focus was not a no-op"

[ "$(DISPLAY=$display xwininfo -id "$three" | awk '/Absolute upper-left Y:/ {print $4; exit}')" = 31 ] ||
    fail "MONOCLE-time client used FREE y geometry"
[ "$(DISPLAY=$display xwininfo -id "$three" | awk '/Height:/ {print $2; exit}')" = 569 ] ||
    fail "MONOCLE-time client used FREE height"

# Stable tab order begins One, Two, Three. Button1 selects the clicked first
# tab, while WheelDown uses the same cyclic order and advances to Two.
DISPLAY=$display xdotool mousemove --window "$bar" 10 10 click 1
wait_active "$one" || fail "left click did not focus clicked tab"
DISPLAY=$display xdotool mousemove --window "$bar" 10 10 click 5
wait_active "$two" || fail "wheel down did not follow cyclic tab order"

# Right click is unbound by default/config and must not change focus.
DISPLAY=$display xdotool mousemove --window "$bar" 10 10 click 3
wait_active "$two" || fail "right click was not a no-op"

DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Height:/ {print \$2; exit}')\" = 600" ||
    fail "fullscreen did not cover full monitor"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $two | awk '/Absolute upper-left Y:/ {print \$4; exit}')\" = 31" ||
    fail "fullscreen exit did not restore MONOCLE content area"

DISPLAY=$display xdotool mousemove --window "$bar" 10 10 click 2
wait_for "! DISPLAY=$display xwininfo -id $one >/dev/null 2>&1" || fail "middle click did not close tab"
one_pid=

DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xwininfo -id $bar | grep -q 'Map State: IsUnMapped'" ||
    fail "tab bar remained mapped outside MONOCLE"
DISPLAY=$display xdotool key alt+1
wait_active "$two" || fail "keyboard tab focus was not a no-op in FREE mode"
three_width=$(DISPLAY=$display xwininfo -id "$three" | awk '/Width:/ {print $2; exit}')
three_height=$(DISPLAY=$display xwininfo -id "$three" | awk '/Height:/ {print $2; exit}')
[ "$(DISPLAY=$display xwininfo -id "$three" | awk '/Absolute upper-left X:/ {print $4; exit}')" = $(((800 - three_width) / 2)) ] ||
    fail "MONOCLE-time client did not restore centered FREE x geometry"
[ "$(DISPLAY=$display xwininfo -id "$three" | awk '/Absolute upper-left Y:/ {print $4; exit}')" = $(((600 - three_height) / 2)) ] ||
    fail "MONOCLE-time client did not restore centered FREE y geometry"

kill "$two_pid" 2>/dev/null || true; two_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb MONOCLE Xft tab bar/input scenario"
