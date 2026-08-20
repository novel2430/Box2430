#!/bin/sh
set -eu

display=${MICROBOX_TEST_DISPLAY:-:130}
microbox_bin=${MICROBOX_BIN:-./build/debug/microbox}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid= drag_pid=

cleanup() {
    for pid in "$drag_pid" "$client_pid" "$wm_pid" "$xvfb_pid"; do
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
        if [ "$attempts" -ge 150 ]; then return 1; fi
        sleep 0.02
    done
}
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
DISPLAY=$display xterm -title MouseClient -geometry 30x8 >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name MouseClient >/dev/null 2>&1" || fail "client missing"
window=$(DISPLAY=$display xdotool search --name MouseClient | head -n 1)
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3}')\" = 2" ||
    fail "client was not managed before mouse input"

start_x=$(field "$window" 'Absolute upper-left X:')
start_y=$(field "$window" 'Absolute upper-left Y:')
start_w=$(field "$window" 'Width:')
start_h=$(field "$window" 'Height:')
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove_relative --sync 100 50 mouseup 1 keyup super
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Absolute upper-left X:/ {print \$4}')\" = $((start_x + 100))" || fail "explicit mouse move failed"
[ "$(field "$window" 'Absolute upper-left Y:')" = $((start_y + 50)) ] || fail "mouse move y failed"

# Hold the drag briefly so the four real override-redirect outline windows can
# be observed before release commits the snap.
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove --sync 0 300 sleep 0.5 mouseup 1 keyup super &
drag_pid=$!
wait_for "DISPLAY=$display xwininfo -root -tree | grep -q '400x2+0+0'" || fail "left snap preview did not appear"
wait "$drag_pid"; drag_pid=
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = 396" || fail "left snap did not commit on release"
[ "$(field "$window" 'Absolute upper-left X:')" = 0 ] || fail "left snap x incorrect"

# Starting a manual move from snapped state restores normal_geometry first.
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove_relative --sync 30 20 mouseup 1 keyup super
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = $start_w" || fail "drag from snap did not restore normal size"
restored_x=$(field "$window" 'Absolute upper-left X:')
[ "$restored_x" = 8 ] || fail "drag from snap did not use release-time normal geometry (x=$restored_x)"

resize_x=$(field "$window" 'Absolute upper-left X:')
resize_y=$(field "$window" 'Absolute upper-left Y:')
before_w=$(field "$window" 'Width:')
before_h=$(field "$window" 'Height:')
DISPLAY=$display xdotool keydown super mousemove --window "$window" 10 10 \
    mousedown 3 sleep 0.05 mousemove_relative --sync 80 40 mouseup 3 keyup super
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" -gt $before_w" || fail "bottom-right resize width failed"
[ "$(field "$window" 'Height:')" -gt "$before_h" ] || fail "bottom-right resize height failed"
[ "$(field "$window" 'Absolute upper-left X:')" = "$resize_x" ] || fail "resize changed x origin"
[ "$(field "$window" 'Absolute upper-left Y:')" = "$resize_y" ] || fail "resize changed y origin"
resized_w=$(field "$window" 'Width:')
resized_h=$(field "$window" 'Height:')

DISPLAY=$display xdotool key super+Up key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = $resized_w" || fail "maximize did not restore resized normal geometry"
[ "$(field "$window" 'Height:')" = "$resized_h" ] || fail "maximize restore lost resized height"

# Top-center hover previews and commits maximize; bottom-center commits nothing.
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove --sync 400 0 mouseup 1 keyup super
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = 796" || fail "top-edge maximize did not commit"
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove --sync 400 599 mouseup 1 keyup super
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = $resized_w" || fail "bottom-center incorrectly snapped"

DISPLAY=$display xdotool key super+m
monocle_w=$(field "$window" 'Width:')
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove_relative --sync 100 50 mouseup 1 keyup super
[ "$(field "$window" 'Width:')" = "$monocle_w" ] || fail "MONOCLE mouse command changed geometry"

kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "microbox: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb explicit mouse move/resize/snap scenario"
