#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:143}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_SIZE_HINTS_BIN:-./build/debug/x11-size-hints-client}
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
assert_size() {
    [ "$(field "$1" 'Width:')" = "$2" ] || fail "$4: wrong width"
    [ "$(field "$1" 'Height:')" = "$3" ] || fail "$4: wrong height"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
DISPLAY=$display "$fixture_bin" >"$tmp_dir/client.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name SizeHintsClient >/dev/null 2>&1" || fail "fixture missing"
client=$(DISPLAY=$display xdotool search --name SizeHintsClient | head -n 1)

# Over-wide: aspect applies to base-adjusted size, then increments are applied.
DISPLAY=$display xdotool windowsize "$client" 500 200
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 340" || fail "max aspect was not applied"
assert_size "$client" 340 200 "max aspect"

# Over-tall exercises the min aspect branch with the same base/inc ordering.
DISPLAY=$display xdotool windowsize "$client" 150 400
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 110" || fail "min aspect was not applied"
assert_size "$client" 150 110 "min aspect"

# Existing min/base/increment behavior remains in the same path.
DISPLAY=$display xdotool windowsize "$client" 50 50
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 110" || fail "minimum size was not applied"
assert_size "$client" 110 80 "min/base/inc"

kill "$client_pid" 2>/dev/null || true; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb WM_NORMAL_HINTS aspect/base/inc/min/max scenario"
