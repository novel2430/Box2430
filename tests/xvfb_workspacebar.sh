#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:188}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
color_bin=${BOX2430_COLOR_BIN:-./build/debug/x11-window-color}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= target_pid= source_pid= follow_pid=

cleanup() {
    for pid in "$follow_pid" "$source_pid" "$target_pid" "$wm_pid" "$xvfb_pid"; do
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
map_state() {
    DISPLAY=$display xwininfo -id "$1" |
        awk -F: '/Map State:/ {gsub(/^ +/, "", $2); print $2; exit}'
}
wait_map_state() {
    window=$1 state=$2
    attempts=0
    while [ "$(map_state "$window")" != "$state" ]; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 200 ]; then return 1; fi
        sleep 0.02
    done
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
workspace_x() {
    set -- $(DISPLAY=$display "$color_bin" "$bar" "$1")
    echo $((($1 + $3) / 2))
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-workspacebar.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)

DISPLAY=$display xterm -title WorkspaceBarTarget -geometry 20x5 >"$tmp_dir/target.log" 2>&1 &
target_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^WorkspaceBarTarget$' >/dev/null 2>&1" || fail "target client missing"
target=$(DISPLAY=$display xdotool search --name '^WorkspaceBarTarget$' | head -n 1)
wait_map_state "$target" IsUnMapped || fail "workspace-2 target did not start hidden"

DISPLAY=$display xterm -title WorkspaceBarSource -geometry 20x5 >"$tmp_dir/source.log" 2>&1 &
source_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^WorkspaceBarSource$' >/dev/null 2>&1" || fail "source client missing"
source=$(DISPLAY=$display xdotool search --name '^WorkspaceBarSource$' | head -n 1)
wait_active "$source" || fail "source client did not focus"

# The binding context belongs to the workspace widget, not the whole bar.
DISPLAY=$display xdotool mousemove --window "$bar" 700 12 click 1
wait_map_state "$target" IsUnMapped || fail "non-workspace bar click activated a workspace"
wait_active "$source" || fail "non-workspace bar click changed focus"

# Button1 resolves the clicked workspace from the drawn workspace item.
wait_for "DISPLAY=$display $color_bin $bar '#222222' >/dev/null 2>&1" || fail "occupied workspace style missing"
click_x=$(workspace_x '#222222')
DISPLAY=$display xdotool mousemove --window "$bar" "$click_x" 12 click 1
wait_map_state "$target" IsViewable || fail "Button1 did not activate clicked workspace"

# Return to workspace 1 through the ordinary keyboard command, then Button2
# moves the focused client to the clicked workspace without following it.
DISPLAY=$display xdotool key super+1
wait_map_state "$target" IsUnMapped || fail "keyboard return to workspace 1 failed"
wait_active "$source" || fail "source focus did not restore on workspace 1"
click_x=$(workspace_x '#222222')
DISPLAY=$display xdotool mousemove --window "$bar" "$click_x" 12 click 2
wait_map_state "$source" IsUnMapped || fail "Button2 did not move focused client"
wait_map_state "$target" IsUnMapped || fail "Button2 unexpectedly followed destination"

# Button3 uses the same clicked workspace context but follows the moved client.
DISPLAY=$display xterm -title WorkspaceBarFollow -geometry 20x5 >"$tmp_dir/follow.log" 2>&1 &
follow_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^WorkspaceBarFollow$' >/dev/null 2>&1" || fail "follow client missing"
follow=$(DISPLAY=$display xdotool search --name '^WorkspaceBarFollow$' | head -n 1)
wait_active "$follow" || fail "follow client did not focus"
click_x=$(workspace_x '#222222')
DISPLAY=$display xdotool mousemove --window "$bar" "$click_x" 12 click 3
wait_map_state "$target" IsViewable || fail "Button3 did not activate destination"
wait_map_state "$source" IsViewable || fail "Button3 destination omitted previously moved client"
wait_map_state "$follow" IsViewable || fail "Button3 moved client did not remain visible"
wait_active "$follow" || fail "Button3 --follow did not keep moved client focused"

# Wheel actions are monitor-relative workspace commands. Keep the pointer over
# the active workspace item so the workspace widget owns the event.
wait_for "DISPLAY=$display $color_bin $bar '#003300' >/dev/null 2>&1" || fail "active workspace style missing"
active_x=$(workspace_x '#003300')
DISPLAY=$display xdotool mousemove --window "$bar" "$active_x" 12 click 5
wait_map_state "$follow" IsUnMapped || fail "WheelDown did not switch to next workspace"
wait_for "DISPLAY=$display $color_bin $bar '#003300' >/dev/null 2>&1" || fail "next workspace did not become active"
active_x=$(workspace_x '#003300')
DISPLAY=$display xdotool mousemove --window "$bar" "$active_x" 12 click 4
wait_map_state "$follow" IsViewable || fail "WheelUp did not switch to previous workspace"

kill "$follow_pid" "$source_pid" "$target_pid" 2>/dev/null || true
follow_pid= source_pid= target_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xvfb configurable workspace-bar mouse-binding scenario"
