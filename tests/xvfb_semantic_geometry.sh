#!/bin/sh
set -eu

display=${MICROBOX_TEST_DISPLAY:-:141}
microbox_bin=${MICROBOX_BIN:-./build/debug/microbox}
fixture_bin=${MICROBOX_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= max_pid= snap_pid= dock_pid= next_dock_pid=

cleanup() {
    for pid in "$next_dock_pid" "$dock_pid" "$snap_pid" "$max_pid" "$wm_pid" "$xvfb_pid"; do
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
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
assert_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "$6: wrong x"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$6: wrong y"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "$6: wrong width"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "$6: wrong height"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" || fail "WM did not start"

DISPLAY=$display xterm -title SemanticMax -geometry 30x8 >"$tmp_dir/max.log" 2>&1 & max_pid=$!
wait_for "DISPLAY=$display xdotool search --name SemanticMax >/dev/null 2>&1" || fail "max client missing"
max=$(DISPLAY=$display xdotool search --name SemanticMax | head -n 1)
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $max | awk '/Width:/ {print \$2; exit}')\" = 796" || fail "maximize failed"
DISPLAY=$display xdotool key super+shift+2
wait_for "DISPLAY=$display xwininfo -id $max | grep -q 'Map State: IsUnMapped'" || fail "max client did not move inactive"

DISPLAY=$display xterm -title SemanticSnap -geometry 30x8 >"$tmp_dir/snap.log" 2>&1 & snap_pid=$!
wait_for "DISPLAY=$display xdotool search --name SemanticSnap >/dev/null 2>&1" || fail "snap client missing"
snap=$(DISPLAY=$display xdotool search --name SemanticSnap | head -n 1)
DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $snap | awk '/Width:/ {print \$2; exit}')\" = 396" || fail "snap failed"
DISPLAY=$display xdotool key super+shift+3
wait_for "DISPLAY=$display xwininfo -id $snap | grep -q 'Map State: IsUnMapped'" || fail "snap client did not move inactive"

DISPLAY=$display "$fixture_bin" DOCK SemanticDock30 0 0 800 30 30 >"$tmp_dir/dock30.log" 2>&1 & dock_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 30, 800, 570'" || fail "30px strut missing"

DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $max | grep -q 'Map State: IsViewable'" || fail "max workspace did not activate"
assert_geometry "$max" 0 30 796 566 "inactive maximized + strut"

DISPLAY=$display xdotool key super+3
wait_for "DISPLAY=$display xwininfo -id $snap | grep -q 'Map State: IsViewable'" || fail "snap workspace did not activate"
assert_geometry "$snap" 0 30 396 566 "inactive snapped + strut"
DISPLAY=$display xdotool mousemove --window "$snap" 20 20 click 1
wait_active "$snap" || fail "snap client did not focus"

DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $snap | awk '/Height:/ {print \$2; exit}')\" = 600" || fail "snap fullscreen entry failed"
DISPLAY=$display "$fixture_bin" DOCK SemanticDock50 0 0 800 50 50 >"$tmp_dir/dock50.log" 2>&1 & next_dock_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 50, 800, 550'" || fail "50px strut missing"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $snap | awk '/Height:/ {print \$2; exit}')\" = 546" || fail "snap fullscreen exit did not rematerialize"
assert_geometry "$snap" 0 50 396 546 "snapped fullscreen + strut"

DISPLAY=$display xdotool key super+2
assert_geometry "$max" 0 50 796 546 "maximized workspace after second strut"
DISPLAY=$display xdotool mousemove --window "$max" 20 20 click 1
wait_active "$max" || fail "max client did not focus"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $max | awk '/Height:/ {print \$2; exit}')\" = 600" || fail "maximize fullscreen entry failed"
kill "$next_dock_pid" "$dock_pid"; next_dock_pid= dock_pid=
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" || fail "dock removal did not clear workarea"
DISPLAY=$display "$fixture_bin" DOCK SemanticDock20 0 0 800 20 20 >"$tmp_dir/dock20.log" 2>&1 & next_dock_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 20, 800, 580'" || fail "20px strut missing"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $max | awk '/Height:/ {print \$2; exit}')\" = 576" || fail "maximize fullscreen exit did not rematerialize"
assert_geometry "$max" 0 20 796 576 "maximized fullscreen + strut"

kill "$next_dock_pid" "$snap_pid" "$max_pid" 2>/dev/null || true
next_dock_pid= snap_pid= max_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "microbox: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb semantic geometry/workarea/fullscreen scenario"
