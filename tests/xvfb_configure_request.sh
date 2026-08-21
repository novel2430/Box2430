#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:145}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
configure_bin=${BOX2430_CONFIGURE_BIN:-./build/debug/x11-configure-request}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid=

cleanup() {
    for pid in "$client_pid" "$wm_pid" "$xvfb_pid"; do
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
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5 expected_b=$6 label=$7
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "$label: wrong x"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$label: wrong y"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "$label: wrong width"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "$label: wrong height"
    [ "$(field "$window" 'Border width:')" = "$expected_b" ] || fail "$label: wrong border"
}
request_twice() {
    window=$1 mask=$2 x=$3 y=$4 width=$5 height=$6 border=$7
    expected_x=$8 expected_y=$9
    shift 9
    expected_w=$1 expected_h=$2 expected_b=$3 label=$4
    first=$(DISPLAY=$display "$configure_bin" "$window" "$mask" \
        "$x" "$y" "$width" "$height" "$border") || fail "$label: first request failed"
    assert_geometry "$window" "$expected_x" "$expected_y" "$expected_w" "$expected_h" "$expected_b" "$label first"
    second=$(DISPLAY=$display "$configure_bin" "$window" "$mask" \
        "$x" "$y" "$width" "$height" "$border") || fail "$label: repeated request failed"
    assert_geometry "$window" "$expected_x" "$expected_y" "$expected_w" "$expected_h" "$expected_b" "$label"
    set -- $first
    [ "$1 $2 $3 $4 $5" = "$expected_x $expected_y $expected_w $expected_h $expected_b" ] ||
        fail "$label: first ConfigureNotify did not report actual geometry"
    set -- $second
    [ "$1 $2 $3 $4 $5" = "$expected_x $expected_y $expected_w $expected_h $expected_b" ] ||
        fail "$label: repeated ConfigureNotify drifted"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-geometry-state.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 0, 800, 600'" || fail "WM did not start"

DISPLAY=$display "$fixture_bin" NORMAL ConfigureState 40 50 200 120 >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name ConfigureState >/dev/null 2>&1" || fail "client missing"
client=$(DISPLAY=$display xdotool search --name ConfigureState | head -n 1)
assert_geometry "$client" 40 50 200 120 4 "initial FREE geometry"

# Every requested geometry-mask combination is applied exactly once, while
# CWBorderWidth remains WM-owned. Repeating the same request must be stable.
request_twice "$client" xy 101 111 999 999 17 101 111 200 120 4 "position only"
request_twice "$client" wh 999 999 240 140 17 101 111 240 140 4 "size only"
request_twice "$client" xywh 121 131 260 160 17 121 131 260 160 4 "position + size"
request_twice "$client" b 999 999 999 999 17 121 131 260 160 4 "border only"
request_twice "$client" xyb 141 151 999 999 17 141 151 260 160 4 "position + border"
request_twice "$client" whb 999 999 280 180 17 141 151 280 180 4 "size + border"
request_twice "$client" xywhb 161 171 300 200 17 161 171 300 200 4 "position + size + border"

# Establish a known FREE restore rectangle for presentation-state tests.
DISPLAY=$display "$configure_bin" "$client" xywh 70 80 210 130 4 >/dev/null
assert_geometry "$client" 70 80 210 130 4 "FREE reset"

DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 392" || fail "snap did not apply"
assert_geometry "$client" 0 0 392 592 4 "snapped presentation"
notice=$(DISPLAY=$display "$configure_bin" "$client" xywhb 300 310 350 250 17)
set -- $notice
[ "$1 $2 $3 $4 $5 $6" = "0 0 392 592 4 1" ] || fail "snapped ConfigureNotify was not actual presentation"
assert_geometry "$client" 0 0 392 592 4 "snapped request ignored"
DISPLAY=$display xdotool key super+n
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 210" || fail "snap clear did not restore FREE geometry"
assert_geometry "$client" 70 80 210 130 4 "snap restore"

DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 792" || fail "maximize did not apply"
notice=$(DISPLAY=$display "$configure_bin" "$client" xywhb 320 330 360 260 19)
set -- $notice
[ "$1 $2 $3 $4 $5 $6" = "0 0 792 592 4 1" ] || fail "maximized ConfigureNotify was not actual presentation"
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 210" || fail "maximize clear did not restore FREE geometry"
assert_geometry "$client" 70 80 210 130 4 "maximize restore"

DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 568" || fail "MONOCLE did not apply"
notice=$(DISPLAY=$display "$configure_bin" "$client" xywhb 340 350 370 270 21)
set -- $notice
[ "$1 $2 $3 $4 $5 $6" = "0 24 792 568 4 1" ] || fail "MONOCLE ConfigureNotify was not actual presentation"
DISPLAY=$display xdotool key super+m
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 210" || fail "MONOCLE exit did not restore FREE geometry"
assert_geometry "$client" 70 80 210 130 4 "MONOCLE restore"

DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Border width:/ {print \$3; exit}')\" = 0" || fail "fullscreen did not apply"
notice=$(DISPLAY=$display "$configure_bin" "$client" xywhb 360 370 390 290 23)
set -- $notice
[ "$1 $2 $3 $4 $5 $6" = "0 0 800 600 0 1" ] || fail "fullscreen ConfigureNotify was not actual presentation"
DISPLAY=$display xprop -id "$client" _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN || fail "fullscreen EWMH state missing"
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Border width:/ {print \$3; exit}')\" = 4" || fail "fullscreen border did not restore"
assert_geometry "$client" 70 80 210 130 4 "fullscreen restore"

# Fake client fullscreen is EWMH state, not geometry ownership: ordinary FREE
# ConfigureRequests must remain usable while the fake request is present.
DISPLAY=$display xdotool windowstate --add FULLSCREEN "$client"
wait_for "DISPLAY=$display xprop -id $client _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN" || fail "fake fullscreen state missing"
request_twice "$client" xywhb 90 100 230 150 25 90 100 230 150 4 "fake fullscreen FREE configure"
DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
wait_for "! DISPLAY=$display xprop -id $client _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN" || fail "fake fullscreen state did not clear"

kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,180p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb ConfigureRequest geometry/state ownership scenario"
