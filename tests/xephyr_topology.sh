#!/bin/sh
set -eu

display=${MICROBOX_TEST_DISPLAY:-:137}
microbox_bin=${MICROBOX_BIN:-./build/debug/microbox}
fixture_bin=${MICROBOX_FIXTURE_BIN:-./build/debug/x11-test-client}
urgency_bin=${MICROBOX_URGENCY_BIN:-./build/debug/x11-set-urgency}
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
wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}

host_display=${DISPLAY:-:0}
DISPLAY=$host_display Xephyr "$display" -screen 800x600 -nolisten tcp \
    >"$tmp_dir/xephyr.log" 2>&1 &
xephyr_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xephyr did not start"

DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-tabs.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
DISPLAY=$display "$fixture_bin" NORMAL '终端 TopologyClient' 100 100 320 180 \
    >"$tmp_dir/client.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name TopologyClient >/dev/null 2>&1" || fail "client missing"
client=$(DISPLAY=$display xdotool search --name TopologyClient | head -n 1)
DISPLAY=$display "$fixture_bin" NORMAL InactiveTab 120 120 300 160 \
    >"$tmp_dir/second.log" 2>&1 & second_pid=$!
wait_for "DISPLAY=$display xdotool search --name InactiveTab >/dev/null 2>&1" || fail "inactive client missing"
DISPLAY=$display "$fixture_bin" NORMAL ActiveTab 140 140 280 140 \
    >"$tmp_dir/third.log" 2>&1 & third_pid=$!
wait_for "DISPLAY=$display xdotool search --name ActiveTab >/dev/null 2>&1" || fail "active client missing"
DISPLAY=$display "$urgency_bin" "$client" 1
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name microbox-tabbar-0 >/dev/null 2>&1" || fail "tab bar missing"
bar=$(DISPLAY=$display xdotool search --name microbox-tabbar-0 | head -n 1)

DISPLAY=$display xrandr -s 640x480
wait_for "test \"\$(DISPLAY=$display xwininfo -root | awk '/Width:/ {print \$2; exit}')\" = 640" ||
    fail "Xephyr root did not resize"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $bar | awk '/Width:/ {print \$2; exit}')\" = 640" ||
    fail "Microbox did not reconcile changed Xinerama/root geometry"

mkdir -p build/evidence
DISPLAY=$display xwd -silent -root -out build/evidence/xephyr-topology.xwd
convert build/evidence/xephyr-topology.xwd build/evidence/xephyr-topology.png

kill "$third_pid" "$second_pid" "$client_pid" 2>/dev/null || true
third_pid= second_pid= client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "microbox: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xephyr topology reconciliation/visual evidence scenario"
