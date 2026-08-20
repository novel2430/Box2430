#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:142}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
modifier_bin=${BOX2430_NUMLOCK_BIN:-./build/debug/x11-set-numlock-modifier}
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

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

# Mod3 (index 5) is deliberately not the common Mod2 assignment.
DISPLAY=$display "$modifier_bin" 5 || fail "could not map NumLock to Mod3"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
DISPLAY=$display xterm -title NumLockClient -geometry 30x8 >"$tmp_dir/client.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name NumLockClient >/dev/null 2>&1" || fail "client missing"
client=$(DISPLAY=$display xdotool search --name NumLockClient | head -n 1)

DISPLAY=$display xdotool key Caps_Lock Num_Lock
DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 396" ||
    fail "binding failed with CapsLock and NumLock on Mod3"

# Restore FREE geometry, then prove mouse matching uses the same clean mask.
DISPLAY=$display xdotool key super+Up super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" != 796" ||
    fail "maximize did not restore"
before_x=$(field "$client" 'Absolute upper-left X:')
DISPLAY=$display xdotool keydown super mousemove --window "$client" 20 20 \
    mousedown 1 mousemove_relative --sync 40 0 mouseup 1 keyup super
after_x=$(field "$client" 'Absolute upper-left X:')
[ "$after_x" -gt "$before_x" ] || fail "mouse binding failed with NumLock on Mod3"

# Remap while Box2430 is running. XSetModifierMapping emits MappingNotify;
# Box2430 must refresh the mapping and replace its passive grabs.
DISPLAY=$display xdotool key Num_Lock Caps_Lock
DISPLAY=$display "$modifier_bin" 7 || fail "could not remap NumLock to Mod5"
sleep 0.05
DISPLAY=$display xdotool key Caps_Lock Num_Lock
DISPLAY=$display xdotool key super+Right
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 396" ||
    fail "binding failed after MappingNotify remapped NumLock to Mod5"
[ "$(field "$client" 'Absolute upper-left X:')" = 400 ] || fail "right snap did not apply after remap"

kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb dynamic NumLock/CapsLock/MappingNotify scenario"
