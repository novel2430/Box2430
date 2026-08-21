#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:176}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
keymap_bin=${BOX2430_KEYMAP_BIN:-./build/debug/x11-keymap-probe}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid=

cleanup() {
    for pid in "$wm_pid" "$xvfb_pid"; do
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
Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-keymap.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

# Add a second physical keycode whose base keysym is Left. Box2430 binds
# Super+Left in the core fixture, so MappingNotify must rebuild the passive
# grabs and include this duplicate rather than only XKeysymToKeycode()'s first
# result.
duplicate_a=$(DISPLAY=$display "$keymap_bin" add-duplicate Left) ||
    fail "could not create duplicate Left keycode"
wait_for "DISPLAY=$display $keymap_bin probe-grab $duplicate_a 64" ||
    fail "duplicate Left keycode was not grabbed"

# Adding another duplicate while the WM is running generates MappingNotify.
# The rebuilt grab set must include the newly introduced keycode too.
duplicate_b=$(DISPLAY=$display "$keymap_bin" add-duplicate Left) ||
    fail "could not create runtime duplicate Left keycode"
[ "$duplicate_b" != "$duplicate_a" ] || fail "runtime duplicate reused the first keycode"
wait_for "DISPLAY=$display $keymap_bin probe-grab $duplicate_b 64" ||
    fail "MappingNotify did not grab new duplicate keycode"
wait_for "DISPLAY=$display $keymap_bin probe-grab $duplicate_a 64" ||
    fail "MappingNotify dropped an earlier duplicate keycode grab"

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb duplicate KeySym/all-KeyCode grab scenario"
