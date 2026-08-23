#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:161}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
monitor_bin=${BOX2430_RANDR_MONITOR_BIN:-./build/debug/x11-randr-monitor}
tmp_dir=$(mktemp -d)
xvfb_pid= left_monitor_pid= right_monitor_pid= wm_pid=
left_one_pid= left_two_pid= right_one_pid= right_two_pid=

cleanup() {
    for pid in "$right_two_pid" "$right_one_pid" "$left_two_pid" \
        "$left_one_pid" "$wm_pid" "$right_monitor_pid" \
        "$left_monitor_pid" "$xvfb_pid"; do
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
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
wait_pointer_ge() {
    boundary=$1
    wait_for "test \"\$(DISPLAY=$display xdotool getmouselocation --shell | awk -F= '/^X=/{print \$2}')\" -ge $boundary"
}
wait_pointer_lt() {
    boundary=$1
    wait_for "test \"\$(DISPLAY=$display xdotool getmouselocation --shell | awk -F= '/^X=/{print \$2}')\" -lt $boundary"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp \
    >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$monitor_bin" set left 0 0 400 600 screen hold \
    >"$tmp_dir/left-monitor.log" 2>&1 &
left_monitor_pid=$!
wait_for "DISPLAY=$display xrandr --listmonitors 2>/dev/null | grep -q 'left'" ||
    fail "left logical monitor was not created"
DISPLAY=$display "$monitor_bin" set right 400 0 400 600 none hold \
    >"$tmp_dir/right-monitor.log" 2>&1 &
right_monitor_pid=$!
wait_for "test \"\$(DISPLAY=$display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 2" ||
    fail "right logical monitor was not created"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tabs.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" ||
    fail "WM did not start"

DISPLAY=$display xdotool mousemove 100 300 click 1
DISPLAY=$display xterm -title LeftTabOne >"$tmp_dir/left-one.log" 2>&1 &
left_one_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^LeftTabOne$' >/dev/null 2>&1" ||
    fail "first left client missing"
left_one=$(DISPLAY=$display xdotool search --name '^LeftTabOne$' | head -n 1)
DISPLAY=$display xterm -title LeftTabTwo >"$tmp_dir/left-two.log" 2>&1 &
left_two_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^LeftTabTwo$' >/dev/null 2>&1" ||
    fail "second left client missing"
left_two=$(DISPLAY=$display xdotool search --name '^LeftTabTwo$' | head -n 1)
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xwininfo -id \$(DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' | head -n 1) | grep -q 'Map State: IsViewable'" ||
    fail "left tab bar did not map"

DISPLAY=$display xdotool key super+ctrl+Right
wait_pointer_ge 400 || fail "right monitor was not selected"
DISPLAY=$display xterm -title RightTabOne >"$tmp_dir/right-one.log" 2>&1 &
right_one_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^RightTabOne$' >/dev/null 2>&1" ||
    fail "first right client missing"
right_one=$(DISPLAY=$display xdotool search --name '^RightTabOne$' | head -n 1)
DISPLAY=$display xterm -title RightTabTwo >"$tmp_dir/right-two.log" 2>&1 &
right_two_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^RightTabTwo$' >/dev/null 2>&1" ||
    fail "second right client missing"
right_two=$(DISPLAY=$display xdotool search --name '^RightTabTwo$' | head -n 1)
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xwininfo -id \$(DISPLAY=$display xdotool search --name '^box2430-tabbar-1$' | head -n 1) | grep -q 'Map State: IsViewable'" ||
    fail "right tab bar did not map"
right_tab=$(DISPLAY=$display xdotool search --name '^box2430-tabbar-1$' | head -n 1)

# Remember the first tab on the right, then select/focus the second tab on the
# left. Clicking the non-selected right tab must activate that client and make
# its monitor the command context without warping away from the tab bar.
DISPLAY=$display xdotool key alt+1
wait_active "$right_one" || fail "could not establish right tab focus history"
DISPLAY=$display xdotool key super+ctrl+Left
wait_pointer_lt 400 || fail "left monitor was not restored"
wait_active "$left_two" || fail "left monitor focus did not restore"
DISPLAY=$display xdotool mousemove --window "$right_tab" 10 10 click 1
wait_active "$right_one" || fail "cross-monitor tab click did not focus clicked client"
wait_pointer_ge 400 || fail "cross-monitor tab click warped the pointer"

# A command after the click must target the newly selected right monitor.
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $right_one | grep -q 'Map State: IsUnMapped'" ||
    fail "tab click did not select the clicked client's monitor"
DISPLAY=$display xwininfo -id "$left_one" | grep -q 'Map State: IsViewable' ||
    fail "right workspace command changed the left monitor"
DISPLAY=$display xdotool key super+1
wait_active "$right_one" || fail "right workspace focus did not restore"

# WheelDown on the non-selected right tab bar must continue from that
# workspace's remembered tab, not cycle the selected left workspace.
DISPLAY=$display xdotool key super+ctrl+Left
wait_active "$left_two" || fail "left focus did not restore before wheel test"
DISPLAY=$display xdotool mousemove --window "$right_tab" 10 10 click 5
wait_active "$right_two" || fail "cross-monitor tab wheel did not cycle right workspace"
wait_pointer_ge 400 || fail "cross-monitor tab wheel warped the pointer"

kill "$right_two_pid" "$right_one_pid" "$left_two_pid" "$left_one_pid" 2>/dev/null || true
right_two_pid= right_one_pid= left_two_pid= left_one_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb cross-monitor MONOCLE tab focus/input scenario"
