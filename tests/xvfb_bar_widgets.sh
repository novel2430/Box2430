#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:179}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
urgency_bin=${BOX2430_URGENCY_BIN:-./build/debug/x11-set-urgency}
color_bin=${BOX2430_COLOR_BIN:-./build/debug/x11-window-color}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= occupied_pid= urgent_pid= active_a_pid= active_b_pid=

cleanup() {
    for pid in "$active_b_pid" "$active_a_pid" "$urgent_pid" "$occupied_pid" "$wm_pid" "$xvfb_pid"; do
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
color_bounds() { DISPLAY=$display "$color_bin" "$1" "$2"; }
color_present() { DISPLAY=$display "$color_bin" "$1" "$2" >/dev/null 2>&1; }
Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-bar-widgets.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)

# Initial workspace state must distinguish active-empty from inactive-empty.
wait_for "DISPLAY=$display $color_bin $bar '#003300' >/dev/null 2>&1" || fail "active workspace style missing"
wait_for "DISPLAY=$display $color_bin $bar '#111111' >/dev/null 2>&1" || fail "empty workspace style missing"
wait_for "DISPLAY=$display $color_bin $bar '#006600' >/dev/null 2>&1" || fail "FREE mode style missing"
if color_present "$bar" "#000055"; then
    fail "empty title unexpectedly allocated title content"
fi

# Occupied and urgent hidden workspaces must update the bar without activation.
DISPLAY=$display xterm -title Phase4Occupied -geometry 20x5 >"$tmp_dir/occupied.log" 2>&1 &
occupied_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Phase4Occupied$' >/dev/null 2>&1" || fail "occupied client missing"
occupied=$(DISPLAY=$display xdotool search --name '^Phase4Occupied$' | head -n 1)
wait_for "test \"\$(DISPLAY=$display xwininfo -id $occupied | awk -F: '/Map State:/ {gsub(/^ +/, \"\", \$2); print \$2; exit}')\" = IsUnMapped" || fail "workspace-2 rule did not hide occupied client"
wait_for "DISPLAY=$display $color_bin $bar '#222222' >/dev/null 2>&1" || fail "occupied workspace style missing"

DISPLAY=$display xterm -title Phase4Urgent -geometry 20x5 >"$tmp_dir/urgent.log" 2>&1 &
urgent_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Phase4Urgent$' >/dev/null 2>&1" || fail "urgent client missing"
urgent=$(DISPLAY=$display xdotool search --name '^Phase4Urgent$' | head -n 1)
DISPLAY=$display "$urgency_bin" "$urgent" 1
wait_for "DISPLAY=$display $color_bin $bar '#440000' >/dev/null 2>&1" || fail "urgent workspace style missing"

# A non-focused urgent client on the active workspace resolves active_urgent.
DISPLAY=$display xterm -title Phase4ActiveA -geometry 20x5 >"$tmp_dir/active-a.log" 2>&1 &
active_a_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Phase4ActiveA$' >/dev/null 2>&1" || fail "active A missing"
active_a=$(DISPLAY=$display xdotool search --name '^Phase4ActiveA$' | head -n 1)
DISPLAY=$display xterm -title 'Phase4 very long centered title that must remain physically centered despite asymmetric edge groups 0123456789' -geometry 20x5 >"$tmp_dir/active-b.log" 2>&1 &
active_b_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Phase4 very long centered title' >/dev/null 2>&1" || fail "active B missing"
active_b=$(DISPLAY=$display xdotool search --name '^Phase4 very long centered title' | head -n 1)
DISPLAY=$display "$urgency_bin" "$active_a" 1
wait_for "DISPLAY=$display $color_bin $bar '#550000' >/dev/null 2>&1" || fail "active_urgent workspace style missing"

# The title group is centered on the physical 800px bar, not the residual area.
wait_for "DISPLAY=$display $color_bin $bar '#000055' >/dev/null 2>&1" || fail "title widget did not render"
set -- $(color_bounds "$bar" "#000055")
title_min=$1 title_max=$3
[ $((title_min + title_max + 1)) -eq 800 ] ||
    fail "title rectangle is not physically centered"
[ "$title_min" -gt 0 ] && [ "$title_max" -lt 799 ] ||
    fail "long title was not constrained inside edge groups"

# Mode is display-only and redraws from the existing workspace mode state.
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display $color_bin $bar '#660066' >/dev/null 2>&1" || fail "MONOCLE mode style missing after mode change"
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display $color_bin $bar '#006600' >/dev/null 2>&1" || fail "FREE mode style did not restore"

# Hit testing uses the already-drawn workspace item rectangle. Locate workspace 2
# by its unique occupied background, click inside that drawn rectangle, and
# require the normal workspace activation path to map its client.
set -- $(color_bounds "$bar" "#222222")
click_x=$((($1 + $3) / 2))
DISPLAY=$display xdotool mousemove --window "$bar" "$click_x" 12 click 1
wait_for "test \"\$(DISPLAY=$display xwininfo -id $occupied | awk -F: '/Map State:/ {gsub(/^ +/, \"\", \$2); print \$2; exit}')\" = IsViewable" || fail "workspace bar click did not activate workspace 2"
wait_for "DISPLAY=$display $color_bin $bar '#003300' >/dev/null 2>&1" || fail "clicked workspace did not resolve active style"
wait_for "DISPLAY=$display $color_bin $bar '#000055' >/dev/null 2>&1" || fail "workspace activation did not refresh title"

# With workspace 2 active, workspace 4 is the only empty item. Clicking its
# unique background must activate an empty workspace and clear the title.
set -- $(color_bounds "$bar" "#111111")
click_x=$((($1 + $3) / 2))
DISPLAY=$display xdotool mousemove --window "$bar" "$click_x" 12 click 1
wait_for "test \"\$(DISPLAY=$display xwininfo -id $occupied | awk -F: '/Map State:/ {gsub(/^ +/, \"\", \$2); print \$2; exit}')\" = IsUnMapped" || fail "workspace bar click did not activate empty workspace"
wait_for "! DISPLAY=$display $color_bin $bar '#000055' >/dev/null 2>&1" || fail "empty workspace retained stale title"

kill "$active_b_pid" "$active_a_pid" "$urgent_pid" "$occupied_pid" 2>/dev/null || true
active_b_pid= active_a_pid= urgent_pid= occupied_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xvfb native-bar core-widget/layout/workspace-interaction scenario"
