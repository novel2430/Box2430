#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:141}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= max_pid= snap_pid= monocle_pid= dock_pid= next_dock_pid=

cleanup() {
    for pid in "$next_dock_pid" "$dock_pid" "$monocle_pid" "$snap_pid" "$max_pid" "$wm_pid" "$xvfb_pid"; do
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
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
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

# MONOCLE is a presentation layer: repeated workarea rematerialization must not
# overwrite the FREE rectangle used when MONOCLE eventually exits.
kill "$next_dock_pid" 2>/dev/null || true; next_dock_pid=
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" || fail "dock removal before MONOCLE failed"
DISPLAY=$display xterm -title SemanticMonocle -geometry 32x9+87+93 >"$tmp_dir/monocle.log" 2>&1 & monocle_pid=$!
wait_for "DISPLAY=$display xdotool search --name SemanticMonocle >/dev/null 2>&1" || fail "MONOCLE client missing"
monocle=$(DISPLAY=$display xdotool search --name SemanticMonocle | head -n 1)
free_x=$(field "$monocle" 'Absolute upper-left X:')
free_y=$(field "$monocle" 'Absolute upper-left Y:')
free_w=$(field "$monocle" 'Width:')
free_h=$(field "$monocle" 'Height:')
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $monocle | awk '/Absolute upper-left Y:/ {print \$4; exit}')\" = 24" || fail "MONOCLE entry failed"
assert_geometry "$monocle" 0 24 800 576 "MONOCLE initial presentation"

DISPLAY=$display "$fixture_bin" DOCK SemanticMonocleDock30 0 0 800 30 30 >"$tmp_dir/monocle-dock30.log" 2>&1 & dock_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 30, 800, 570'" || fail "MONOCLE 30px strut missing"
assert_geometry "$monocle" 0 54 800 546 "MONOCLE first workarea rematerialization"
kill "$dock_pid" 2>/dev/null || true; dock_pid=
DISPLAY=$display "$fixture_bin" DOCK SemanticMonocleDock50 0 0 800 50 50 >"$tmp_dir/monocle-dock50.log" 2>&1 & next_dock_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 50, 800, 550'" || fail "MONOCLE 50px strut missing"
assert_geometry "$monocle" 0 74 800 526 "MONOCLE second workarea rematerialization"
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $monocle | awk '/Width:/ {print \$2; exit}')\" = $free_w" || fail "MONOCLE exit did not restore FREE geometry"
assert_geometry "$monocle" "$free_x" "$free_y" "$free_w" "$free_h" "MONOCLE FREE restore after workarea changes"

# Nest real fullscreen above MONOCLE, change workarea while fullscreen hides the
# MONOCLE geometry, then unwind one layer at a time.
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $monocle | awk '/Absolute upper-left Y:/ {print \$4; exit}')\" = 74" || fail "nested MONOCLE entry failed"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $monocle | awk '/Height:/ {print \$2; exit}')\" = 600" || fail "nested fullscreen entry failed"
assert_geometry "$monocle" 0 0 800 600 "MONOCLE fullscreen presentation"
kill "$next_dock_pid" 2>/dev/null || true; next_dock_pid=
DISPLAY=$display "$fixture_bin" DOCK SemanticMonocleDock20 0 0 800 20 20 >"$tmp_dir/monocle-dock20.log" 2>&1 & next_dock_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 20, 800, 580'" || fail "nested 20px strut missing"
assert_geometry "$monocle" 0 0 800 600 "fullscreen ignores workarea presentation"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $monocle | awk '/Absolute upper-left Y:/ {print \$4; exit}')\" = 44" || fail "fullscreen exit did not rematerialize MONOCLE"
assert_geometry "$monocle" 0 44 800 556 "fullscreen unwind to MONOCLE"
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $monocle | awk '/Width:/ {print \$2; exit}')\" = $free_w" || fail "nested MONOCLE exit did not restore FREE geometry"
assert_geometry "$monocle" "$free_x" "$free_y" "$free_w" "$free_h" "nested FREE restore"

kill "$next_dock_pid" "$monocle_pid" "$snap_pid" "$max_pid" 2>/dev/null || true
next_dock_pid= monocle_pid= snap_pid= max_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb semantic geometry/workarea/fullscreen scenario"
