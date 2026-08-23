#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:156}
capacity_display=${BOX2430_CAPACITY_TEST_DISPLAY:-:158}
runtime_display=${BOX2430_RUNTIME_TEST_DISPLAY:-:160}
disabled_display=${BOX2430_DISABLED_TEST_DISPLAY:-:157}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
snapshot_bin=${BOX2430_RANDR_SNAPSHOT_BIN:-./build/debug/randr-monitor-test}
monitor_bin=${BOX2430_RANDR_MONITOR_BIN:-./build/debug/x11-randr-monitor}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= capacity_pid= runtime_pid= disabled_pid= left_monitor_pid=
overlay_monitor_pid= many_monitor_pid= runtime_many_pid= wm_pid= runtime_wm_pid=
runtime_client_pid=

cleanup() {
    for pid in "$disabled_pid" "$runtime_client_pid" "$runtime_wm_pid" \
        "$runtime_many_pid" "$runtime_pid" "$capacity_pid" "$wm_pid" \
        "$many_monitor_pid" "$overlay_monitor_pid" "$left_monitor_pid" \
        "$xvfb_pid"; do
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
window_geometry() {
    DISPLAY=$runtime_display xwininfo -id "$1" | awk '
        /Absolute upper-left X:/ {x=$NF}
        /Absolute upper-left Y:/ {y=$NF}
        /Width:/ {w=$NF}
        /Height:/ {h=$NF}
        END {print x, y, w, h}'
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$runtime_display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp \
    >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$snapshot_bin"

# Explicit RandR logical monitors are not deduplicated merely because their
# rectangles are identical.
DISPLAY=$display "$monitor_bin" set left 0 0 800 600 screen hold \
    >"$tmp_dir/left-monitor.log" 2>&1 &
left_monitor_pid=$!
wait_for "DISPLAY=$display xrandr --listmonitors 2>/dev/null | grep -q 'left'" ||
    fail "left logical monitor was not created"
DISPLAY=$display "$monitor_bin" set overlay 0 0 800 600 none hold \
    >"$tmp_dir/overlay-monitor.log" 2>&1 &
overlay_monitor_pid=$!
wait_for "test \"\$(DISPLAY=$display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 2" ||
    fail "same-geometry logical monitor was not created"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-native-bar-top.toml \
    >"$tmp_dir/two-monitor-wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" ||
    fail "first same-geometry monitor bar was not created"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-1$' >/dev/null 2>&1" ||
    fail "second same-geometry monitor was collapsed"
kill "$wm_pid"
if ! wait "$wm_pid"; then
    sed -n '1,160p' "$tmp_dir/two-monitor-wm.log" >&2
    fail "same-geometry WM exited unsuccessfully"
fi
wm_pid=
kill "$overlay_monitor_pid" "$left_monitor_pid"
wait "$overlay_monitor_pid" "$left_monitor_pid" 2>/dev/null || true
overlay_monitor_pid= left_monitor_pid=
kill "$xvfb_pid"; wait "$xvfb_pid" 2>/dev/null || true
xvfb_pid=

# Capacity overflow is an invalid observation, not a reason to fabricate one
# root-sized monitor and continue. Use a fresh server: RandR logical monitors
# are server-side state and do not disappear merely because their creator exits.
Xvfb "$capacity_display" -screen 0 800x600x24 -nolisten tcp \
    >"$tmp_dir/xvfb-capacity.log" 2>&1 &
capacity_pid=$!
wait_for "DISPLAY=$capacity_display xdpyinfo >/dev/null 2>&1" ||
    fail "capacity Xvfb did not start"
DISPLAY=$capacity_display "$monitor_bin" many 32 hold \
    >"$tmp_dir/many-monitors.log" 2>&1 &
many_monitor_pid=$!
wait_for "test \"\$(DISPLAY=$capacity_display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 33" ||
    fail "over-capacity RandR fixture was not created"
if DISPLAY=$capacity_display "$box2430_bin" >"$tmp_dir/over-capacity-wm.log" 2>&1; then
    fail "WM started with an over-capacity RandR observation"
fi
grep -q 'reported 33 active logical monitors' "$tmp_dir/over-capacity-wm.log" || {
    sed -n '1,80p' "$tmp_dir/over-capacity-wm.log" >&2
    fail "missing clear over-capacity startup failure"
}
kill "$many_monitor_pid"; wait "$many_monitor_pid" 2>/dev/null || true
many_monitor_pid=
kill "$capacity_pid"; wait "$capacity_pid" 2>/dev/null || true
capacity_pid=

# Runtime query failure must fail closed: reject the attempted observation and
# retain the existing model, accepted snapshot, focus, geometry, and UI state.
Xvfb "$runtime_display" -screen 0 800x600x24 -nolisten tcp \
    >"$tmp_dir/xvfb-runtime.log" 2>&1 &
runtime_pid=$!
wait_for "DISPLAY=$runtime_display xdpyinfo >/dev/null 2>&1" ||
    fail "runtime-failure Xvfb did not start"
DISPLAY=$runtime_display "$box2430_bin" \
    -c tests/fixtures/config-native-bar-top.toml \
    >"$tmp_dir/runtime-wm.log" 2>&1 &
runtime_wm_pid=$!
wait_for "DISPLAY=$runtime_display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" ||
    fail "runtime-failure WM did not start"
runtime_bar=$(DISPLAY=$runtime_display xdotool search --name '^box2430-bar-0$' | head -n 1)
DISPLAY=$runtime_display "$fixture_bin" NORMAL RuntimeInvalidObservation \
    180 140 240 160 >"$tmp_dir/runtime-client.log" 2>&1 &
runtime_client_pid=$!
wait_for "DISPLAY=$runtime_display xdotool search --name '^RuntimeInvalidObservation$' >/dev/null 2>&1" ||
    fail "runtime-failure client did not map"
runtime_client=$(DISPLAY=$runtime_display xdotool search --name '^RuntimeInvalidObservation$' | head -n 1)
wait_active "$runtime_client" || fail "runtime-failure client was not focused"
runtime_geometry=$(window_geometry "$runtime_client")

# `many` publishes all test monitors under one server grab, so the WM cannot
# reconcile an intermediate valid count before observing the final 33.
DISPLAY=$runtime_display "$monitor_bin" many 32 hold \
    >"$tmp_dir/runtime-many.log" 2>&1 &
runtime_many_pid=$!
wait_for "test \"\$(DISPLAY=$runtime_display xrandr --listmonitors 2>/dev/null | awk '/Monitors:/ {print \$2}')\" = 33" ||
    fail "runtime over-capacity fixture was not created"
DISPLAY=$runtime_display "$monitor_bin" notify-root
wait_for "grep -q 'reported 33 active logical monitors' '$tmp_dir/runtime-wm.log'" || {
    sed -n '1,120p' "$tmp_dir/runtime-wm.log" >&2
    fail "runtime invalid observation was not rejected clearly"
}
kill -0 "$runtime_wm_pid" 2>/dev/null ||
    fail "runtime invalid observation stopped the WM"
[ "$(DISPLAY=$runtime_display xdotool search --name '^box2430-bar-0$' | head -n 1)" = "$runtime_bar" ] ||
    fail "runtime invalid observation recreated the accepted monitor bar"
[ "$(window_geometry "$runtime_client")" = "$runtime_geometry" ] ||
    fail "runtime invalid observation changed client geometry"
wait_active "$runtime_client" ||
    fail "runtime invalid observation changed focused client"

# Workspace commands still target the old selected monitor/model.
DISPLAY=$runtime_display xdotool key super+2
wait_for "DISPLAY=$runtime_display xwininfo -id $runtime_client | grep -q 'Map State: IsUnMapped'" ||
    fail "runtime invalid observation changed workspace ownership"
DISPLAY=$runtime_display xdotool key super+1
wait_for "DISPLAY=$runtime_display xwininfo -id $runtime_client | grep -q 'Map State: IsViewable'" ||
    fail "runtime invalid observation lost the accepted workspace"
wait_active "$runtime_client" ||
    fail "runtime invalid observation prevented focus restoration"

kill "$runtime_client_pid"; wait "$runtime_client_pid" 2>/dev/null || true
runtime_client_pid=
kill "$runtime_wm_pid"; wait "$runtime_wm_pid"; runtime_wm_pid=
kill "$runtime_many_pid"; wait "$runtime_many_pid" 2>/dev/null || true
runtime_many_pid=
kill "$runtime_pid"; wait "$runtime_pid" 2>/dev/null || true
runtime_pid=

Xvfb "$disabled_display" -screen 0 800x600x24 -extension RANDR -nolisten tcp \
    >"$tmp_dir/xvfb-disabled.log" 2>&1 &
disabled_pid=$!
wait_for "DISPLAY=$disabled_display xdpyinfo >/dev/null 2>&1" ||
    fail "RandR-disabled Xvfb did not start"
if DISPLAY=$disabled_display "$box2430_bin" >"$tmp_dir/wm.log" 2>&1; then
    fail "WM started without RandR"
fi
grep -q 'RandR extension is unavailable' "$tmp_dir/wm.log" || {
    sed -n '1,80p' "$tmp_dir/wm.log" >&2
    fail "missing clear RandR startup failure"
}
if grep -Eq 'BadAtom|X Error|X11 error' "$tmp_dir/wm.log"; then
    sed -n '1,80p' "$tmp_dir/wm.log" >&2
    fail "RandR preflight cleanup emitted a secondary X11 error"
fi

# RandR remains a passive query backend. Production code must not subscribe
# to output/CRTC/screen-change notification masks.
if grep -En 'XRRSelectInput|RRScreenChangeNotifyMask|RRCrtcChangeNotifyMask|RROutputChangeNotifyMask|RROutputPropertyNotifyMask' src/*.c src/*.h \
    >"$tmp_dir/randr-subscription-audit.log"; then
    cat "$tmp_dir/randr-subscription-audit.log" >&2
    fail "production source subscribes to RandR hotplug events"
fi

echo "PASS: RandR observation, isolated capacity/runtime-failure fixtures, version cleanup, and passive-query audit"
