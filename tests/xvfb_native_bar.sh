#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:147}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
stacking_bin=${BOX2430_STACKING_BIN:-./build/debug/x11-stacking-order}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid= dock_pid=

cleanup() {
    for pid in "$dock_pid" "$client_pid" "$wm_pid" "$xvfb_pid"; do
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
assert_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5 label=$6
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "$label: wrong x"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$label: wrong y"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "$label: wrong width"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "$label: wrong height"
}
assert_below() {
    DISPLAY=$display "$stacking_bin" "$1" "$2" || fail "$3"
}
start_wm() {
    fixture=$1
    log=$2
    DISPLAY=$display "$box2430_bin" -c "$fixture" >"$tmp_dir/$log" 2>&1 &
    wm_pid=$!
    wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "$log did not start"
}
stop_wm() {
    kill "$wm_pid"
    wait "$wm_pid"
    wm_pid=
}
stop_client() {
    if [ -n "$client_pid" ]; then
        kill "$client_pid" 2>/dev/null || true
        wait "$client_pid" 2>/dev/null || true
        client_pid=
    fi
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

# Top bar with bottom tabs: reserve the post-strut workarea independently, stay
# between clients and tabs, and remain mapped under real fullscreen.
start_wm tests/fixtures/config-native-bar-top.toml top-wm.log
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 24, 800, 576'" ||
    fail "top bar reservation missing from _NET_WORKAREA"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" ||
    fail "top native bar window missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
assert_geometry "$bar" 0 0 800 24 "top bar"
DISPLAY=$display xwininfo -id "$bar" | grep -qi 'Override Redirect State: yes' ||
    fail "native bar is not override-redirect"
DISPLAY=$display xwininfo -id "$bar" | grep -q 'Map State: IsViewable' ||
    fail "top native bar is not mapped"

DISPLAY=$display xterm -title NativeBarTopClient -geometry 30x8 >"$tmp_dir/top-client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^NativeBarTopClient$' >/dev/null 2>&1" ||
    fail "top client missing"
client=$(DISPLAY=$display xdotool search --name '^NativeBarTopClient$' | head -n 1)
if DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi "$(printf '0x%x' "$bar")"; then
    fail "WM-owned native bar leaked into _NET_CLIENT_LIST"
fi
initial_height=$(field "$client" 'Height:')
expected_y=$((24 + (576 - initial_height) / 2))
[ "$(field "$client" 'Absolute upper-left Y:')" = "$expected_y" ] ||
    fail "FREE center placement did not use post-bar workarea"
DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 396" ||
    fail "snap did not use post-bar workarea"
assert_geometry "$client" 0 24 396 572 "top-bar snap"
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 572" ||
    fail "maximize did not use post-bar workarea"
assert_geometry "$client" 0 24 796 572 "top-bar maximize"
assert_below "$client" "$bar" "ordinary client is not below native bar"

DISPLAY=$display "$fixture_bin" DOCK NativeBarDock 0 0 800 30 30 >"$tmp_dir/dock.log" 2>&1 &
dock_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^NativeBarDock$' >/dev/null 2>&1" || fail "dock missing"
dock=$(DISPLAY=$display xdotool search --name '^NativeBarDock$' | head -n 1)
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 54, 800, 546'" ||
    fail "external strut and top native bar did not compose"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $bar | awk '/Absolute upper-left Y:/ {print \$4; exit}')\" = 30" ||
    fail "native bar did not move below external top strut"
assert_geometry "$bar" 0 30 800 24 "top bar after strut"
assert_geometry "$client" 0 54 796 542 "maximized client after strut + bar"
assert_below "$bar" "$dock" "native bar is not below external dock"

DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' >/dev/null 2>&1" || fail "bottom tab bar missing"
tab=$(DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $tab | grep -q 'Map State: IsViewable'" || fail "bottom tab bar did not map"
assert_geometry "$tab" 0 569 800 31 "bottom tab bar after top strut + bar"
assert_geometry "$client" 0 54 800 515 "bottom-tab MONOCLE content"
assert_below "$bar" "$tab" "native bar is not below MONOCLE tabs"
assert_below "$tab" "$dock" "MONOCLE tabs are not below external dock"

DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Border width:/ {print \$3; exit}')\" = 0" ||
    fail "fullscreen did not settle"
assert_geometry "$client" 0 0 800 600 "fullscreen over native UI"
DISPLAY=$display xwininfo -id "$bar" | grep -q 'Map State: IsViewable' || fail "fullscreen unmapped native bar"
DISPLAY=$display xwininfo -id "$tab" | grep -q 'Map State: IsViewable' || fail "fullscreen unmapped tab bar"
assert_below "$bar" "$client" "fullscreen client is not above native bar"
assert_below "$tab" "$client" "fullscreen client is not above MONOCLE tabs"
assert_below "$dock" "$client" "fullscreen client is not above external dock"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Absolute upper-left Y:/ {print \$4; exit}')\" = 54" ||
    fail "fullscreen exit did not restore bottom-tab MONOCLE content"

kill "$dock_pid" 2>/dev/null || true
wait "$dock_pid" 2>/dev/null || true
dock_pid=
stop_client
stop_wm

# Bottom bar with top tabs: workarea stays above the bar while the MONOCLE tab
# edge remains independently configurable.
start_wm tests/fixtures/config-native-bar-bottom.toml bottom-wm.log
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 576'" ||
    fail "bottom bar reservation missing from _NET_WORKAREA"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "bottom bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
assert_geometry "$bar" 0 576 800 24 "bottom bar"

DISPLAY=$display xterm -title NativeBarBottomClient -geometry 30x8 >"$tmp_dir/bottom-client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^NativeBarBottomClient$' >/dev/null 2>&1" || fail "bottom client missing"
client=$(DISPLAY=$display xdotool search --name '^NativeBarBottomClient$' | head -n 1)
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 572" ||
    fail "bottom maximize did not use post-bar workarea"
assert_geometry "$client" 0 0 796 572 "bottom-bar maximize"
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' >/dev/null 2>&1" || fail "top tab bar missing"
tab=$(DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $tab | grep -q 'Map State: IsViewable'" || fail "top tab bar did not map"
assert_geometry "$tab" 0 0 800 31 "top tab bar with bottom native bar"
assert_geometry "$client" 0 31 800 545 "top-tab MONOCLE content"
assert_below "$client" "$bar" "bottom MONOCLE client is not below native bar"
assert_below "$bar" "$tab" "bottom native bar is not below MONOCLE tabs"
stop_client
stop_wm

# Tab position remains independent when the native bar itself is disabled.
# This also keeps the V1.5 full-monitor workarea when disabled.
start_wm tests/fixtures/config-native-bar-disabled-bottom.toml disabled-wm.log
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" ||
    fail "disabled native bar still reserved workarea"
if DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1; then
    fail "disabled native bar still created a bar window"
fi
DISPLAY=$display xterm -title NativeBarDisabledClient -geometry 30x8 >"$tmp_dir/disabled-client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^NativeBarDisabledClient$' >/dev/null 2>&1" || fail "disabled-bar client missing"
client=$(DISPLAY=$display xdotool search --name '^NativeBarDisabledClient$' | head -n 1)
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' >/dev/null 2>&1" || fail "disabled-bar tab window missing"
tab=$(DISPLAY=$display xdotool search --name '^box2430-tabbar-0$' | head -n 1)
wait_for "DISPLAY=$display xwininfo -id $tab | grep -q 'Map State: IsViewable'" || fail "disabled-bar bottom tabs did not map"
assert_geometry "$tab" 0 569 800 31 "bottom tabs with native bar disabled"
assert_geometry "$client" 0 0 800 569 "bottom MONOCLE content with native bar disabled"
stop_client
stop_wm

for log in top-wm.log bottom-wm.log disabled-wm.log; do
    if grep -q "box2430: X11 error" "$tmp_dir/$log"; then
        sed -n '1,160p' "$tmp_dir/$log" >&2
        fail "unexpected X11 error in $log"
    fi
done

echo "PASS: Xvfb native-bar/workarea/top-bottom/fullscreen scenario"
