#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:137}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
urgency_bin=${BOX2430_URGENCY_BIN:-./build/debug/x11-set-urgency}
tmp_dir=$(mktemp -d)
xephyr_pid= wm_pid= client_pid= second_pid= third_pid=

cleanup() {
    for pid in "$third_pid" "$second_pid" "$client_pid" "$wm_pid" "$xephyr_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; fi
    done
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }
image_convert() {
    if command -v magick >/dev/null 2>&1; then
        magick "$@"
    else
        convert "$@"
    fi
}
wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
assert_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5 label=$6
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "$label: wrong x"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$label: wrong y"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "$label: wrong width"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "$label: wrong height"
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

host_display=${DISPLAY:-:0}
DISPLAY=$host_display Xephyr "$display" -screen 800x600 -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-topology.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
DISPLAY=$display "$fixture_bin" NORMAL '终端 TopologyClient' 550 430 320 180 \
    >"$tmp_dir/client.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name 'TopologyClient$' >/dev/null 2>&1" || fail "client missing"
client=$(DISPLAY=$display xdotool search --name 'TopologyClient$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $client | grep -q 'Map State: IsViewable'" ||
    fail "topology client did not map"

# Follow the same setup shape as the existing workspace-transition regression:
# establish semantic state while the client is visible/focused, move it away
# without following, then create the final active client on the source workspace.
# This keeps topology setup independent of click hit-testing and source-workspace
# fallback ordering.  Use anchored names because xdotool name searches are
# case-insensitive regular expressions.
DISPLAY=$display "$fixture_bin" NORMAL TopologyInactive 120 120 300 160 \
    >"$tmp_dir/second.log" 2>&1 & second_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^TopologyInactive$' >/dev/null 2>&1" ||
    fail "inactive client missing"
inactive=$(DISPLAY=$display xdotool search --name '^TopologyInactive$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $inactive | grep -q 'Map State: IsViewable'" ||
    fail "inactive topology client did not map before setup"
wait_active "$inactive" || fail "inactive topology client did not start focused"
DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $inactive | awk '/Width:/ {print \$2; exit}')\" = 396" ||
    fail "inactive topology client did not snap"
DISPLAY=$display xdotool key super+shift+2
wait_for "DISPLAY=$display xwininfo -id $inactive | grep -q 'Map State: IsUnMapped'" ||
    fail "inactive topology client did not move to workspace 2"

DISPLAY=$display "$fixture_bin" NORMAL TopologyActive 140 140 280 140 \
    >"$tmp_dir/third.log" 2>&1 & third_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^TopologyActive$' >/dev/null 2>&1" ||
    fail "active client missing"
active=$(DISPLAY=$display xdotool search --name '^TopologyActive$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $active | grep -q 'Map State: IsViewable'" ||
    fail "active topology client did not map"
wait_active "$active" || fail "active topology client did not start focused"

# Visit workspace 2 once so its snapped client becomes a real focus-history
# target, then return and establish the source workspace's remembered focus.
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $inactive | grep -q 'Map State: IsViewable'" ||
    fail "inactive topology workspace did not activate"
wait_active "$inactive" || fail "inactive topology workspace did not focus its client"
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $inactive | grep -q 'Map State: IsUnMapped'" ||
    fail "inactive topology workspace did not hide after returning to workspace 1"
wait_active "$active" || fail "workspace 1 did not restore TopologyActive before topology change"

DISPLAY=$display "$urgency_bin" "$client" 1
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name box2430-tabbar-0 >/dev/null 2>&1" || fail "tab bar missing"
bar=$(DISPLAY=$display xdotool search --name box2430-tabbar-0 | head -n 1)
wait_active "$active" || fail "expected active client before topology change"

DISPLAY=$display xrandr -s 640x480
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 640" ||
    fail "Xephyr root did not resize"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $bar | awk '/Width:/ {print \$2; exit}')\" = 640" ||
    fail "Box2430 did not reconcile changed Xinerama/root geometry"
wait_active "$active" || fail "resolution change disturbed semantic focus"

DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $inactive | grep -q 'Map State: IsViewable'" ||
    fail "inactive workspace did not survive topology change"
wait_active "$inactive" || fail "inactive workspace focus target became invalid"
assert_geometry "$inactive" 0 0 316 476 "inactive snap after topology change"
DISPLAY=$display xdotool key super+1
wait_active "$active" || fail "workspace 1 focus did not restore after topology change"

# The client entered MONOCLE with a FREE rectangle near the old 800x600
# bottom-right edge. Shrinking the same logical monitor must clamp that latent
# FREE rectangle even while MONOCLE owns the visible presentation.
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 316" ||
    fail "MONOCLE exit restored stale pre-topology FREE x"
assert_geometry "$client" 316 296 320 180 "MONOCLE topology FREE restore"

# Snap is semantic state underneath real fullscreen. A later resolution change
# must resize fullscreen immediately, then unwind to snap against the new
# workarea rather than the old 640x480 rectangle.
DISPLAY=$display xdotool mousemove --window "$client" 20 20 click 1
wait_active "$client" || fail "topology client could not be focused"
DISPLAY=$display xdotool key super+Right
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 320" ||
    fail "snap before second topology change failed"
assert_geometry "$client" 320 0 316 476 "snap at 640x480"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 640" ||
    fail "fullscreen before second topology change failed"
assert_geometry "$client" 0 0 640 480 "fullscreen at 640x480"

DISPLAY=$display xrandr -s 800x600
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 800" ||
    fail "Xephyr root did not restore 800x600"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 800" ||
    fail "fullscreen did not rematerialize after resolution growth"
assert_geometry "$client" 0 0 800 600 "fullscreen after topology growth"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Absolute upper-left X:/ {print \$4; exit}')\" = 400" ||
    fail "fullscreen exit did not restore snap on new workarea"
assert_geometry "$client" 400 0 396 596 "snap after fullscreen topology change"

# Toggle maximize on/off to clear the snap layer and expose the preserved FREE
# restore rectangle. It should be the clamped 640x480-era FREE geometry, not a
# stale snap/fullscreen rectangle.
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 796" ||
    fail "maximize after topology change failed"
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 320" ||
    fail "maximize exit did not restore latent FREE geometry"
assert_geometry "$client" 316 296 320 180 "FREE restore after snap/fullscreen topology"

mkdir -p build/evidence
DISPLAY=$display xwd -silent -root -out build/evidence/xephyr-topology.xwd
image_convert build/evidence/xephyr-topology.xwd build/evidence/xephyr-topology.png

kill "$third_pid" "$second_pid" "$client_pid" 2>/dev/null || true
third_pid= second_pid= client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xephyr topology reconciliation/visual evidence scenario"
