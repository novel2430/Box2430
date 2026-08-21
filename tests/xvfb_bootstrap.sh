#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:124}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid=
wm_pid=
client_pid=
client2_pid=

cleanup() {
    if [ -n "$client_pid" ]; then kill "$client_pid" 2>/dev/null || true; fi
    if [ -n "$client2_pid" ]; then kill "$client2_pid" 2>/dev/null || true; fi
    if [ -n "$wm_pid" ]; then kill "$wm_pid" 2>/dev/null || true; fi
    if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 100 ]; then return 1; fi
        sleep 0.02
    done
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
if ! wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1"; then
    sed -n '1,120p' "$tmp_dir/xvfb.log" >&2
    fail "Xvfb did not start"
fi

# Start one client before the WM to exercise startup discovery.
DISPLAY=$display xterm -geometry 40x10+5+7 >"$tmp_dir/xterm.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --class XTerm >/dev/null 2>&1" || fail "xterm did not map"
window=$(DISPLAY=$display xdotool search --class XTerm | head -n 1)

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-default-no-bar.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -q 0x" || fail "client was not managed"

# A second WM must fail rather than stealing ownership.
if DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-default-no-bar.toml >"$tmp_dir/second.log" 2>&1; then
    fail "second WM acquired ownership"
fi
grep -q "another window manager owns" "$tmp_dir/second.log" || fail "ownership diagnostic missing"

state=$(DISPLAY=$display xprop -id "$window" WM_STATE)
echo "$state" | grep -q "window state: Normal" || fail "WM_STATE is not Normal"

info=$(DISPLAY=$display xwininfo -id "$window")
border=$(echo "$info" | awk '/Border width:/ {print $3}')
x=$(echo "$info" | awk '/Absolute upper-left X:/ {print $4}')
y=$(echo "$info" | awk '/Absolute upper-left Y:/ {print $4}')
width=$(echo "$info" | awk '/Width:/ {print $2; exit}')
height=$(echo "$info" | awk '/Height:/ {print $2; exit}')
[ "$border" = 2 ] || fail "expected border width 2, got $border"

expected_x=$(((800 - width) / 2))
expected_y=$(((600 - height) / 2))
[ "$x" = "$expected_x" ] || fail "window is not centered horizontally ($x != $expected_x)"
[ "$y" = "$expected_y" ] || fail "window is not centered vertically ($y != $expected_y)"

active=$(DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW)
echo "$active" | grep -qi "$(printf '0x%x' "$window")" || fail "active window is not the managed client"

# A later MapRequest follows the same manage path and click-to-focus can return
# focus to the first client without swallowing the pointer event.
DISPLAY=$display xterm -geometry 30x8+11+13 >"$tmp_dir/xterm2.log" 2>&1 &
client2_pid=$!
wait_for "test \"\$(DISPLAY=$display xdotool search --class XTerm 2>/dev/null | wc -l)\" -ge 2" || fail "second xterm did not map"
DISPLAY=$display xdotool mousemove --window "$window" 20 20 click 1
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$window")" || fail "click did not focus first client"

DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $window | grep -q 'Map State: IsUnMapped'" || fail "inactive workspace client remained visible"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q "0x0" || fail "empty workspace retained keyboard focus"
DISPLAY=$display xdotool key super+1
wait_for "DISPLAY=$display xwininfo -id $window | grep -q 'Map State: IsViewable'" || fail "workspace client did not remap"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$window")" || fail "workspace focus was not restored"

kill "$client2_pid"
client2_pid=

kill "$client_pid"
client_pid=
wait_for "! DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -q 0x" || fail "client list did not clear"

kill "$wm_pid"
wait "$wm_pid"
wm_pid=

echo "PASS: Xvfb bootstrap ownership/manage/unmanage scenario"
