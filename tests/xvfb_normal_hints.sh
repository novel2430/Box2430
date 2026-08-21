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
assert_position() {
    [ "$(field "$1" 'Absolute upper-left X:')" = "$2" ] || fail "$4: wrong x"
    [ "$(field "$1" 'Absolute upper-left Y:')" = "$3" ] || fail "$4: wrong y"
}
stop_client() {
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
    client_pid=
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-normal-hints.toml >"$tmp_dir/wm.log" 2>&1 &
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

# Maximum constraints run after aspect and increment normalization, as in dwm.
DISPLAY=$display xdotool windowsize "$client" 900 900
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Width:/ {print \$2; exit}')\" = 510" || fail "maximum size was not applied"
assert_size "$client" 510 410 "maximum size"
stop_client

# Base-relative increments are exact: (157-100) rounds down by 20 and
# (147-100) rounds down by 10.
DISPLAY=$display "$fixture_bin" increment >"$tmp_dir/increment.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name IncrementHintsClient >/dev/null 2>&1" || fail "increment fixture missing"
client=$(DISPLAY=$display xdotool search --name IncrementHintsClient | head -n 1)
DISPLAY=$display xdotool windowsize "$client" 157 147
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 140" || fail "original increments were not applied"
assert_size "$client" 140 140 "base-relative increment"

# Updating WM_NORMAL_HINTS invalidates the cached 20x10 increments. The next
# request must lazily read the new base=80, increments=30x25 values.
DISPLAY=$display "$fixture_bin" update "$client"
sleep 0.05
DISPLAY=$display xdotool windowsize "$client" 157 147
wait_for "test \"\$(DISPLAY=$display xwininfo -id $client | awk '/Height:/ {print \$2; exit}')\" = 130" || fail "updated increments were not applied"
assert_size "$client" 140 130 "runtime hint invalidation"
stop_client

# A window wholly beyond the right/bottom workarea edges is brought back so
# its complete outer frame touches those edges (2px configured border).
DISPLAY=$display "$fixture_bin" offscreen >"$tmp_dir/offscreen.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name OffscreenHintsClient >/dev/null 2>&1" || fail "offscreen fixture missing"
client=$(DISPLAY=$display xdotool search --name OffscreenHintsClient | head -n 1)
assert_size "$client" 200 120 "offscreen initial size"
assert_position "$client" 596 476 "offscreen initial correction"
stop_client

# Merely partial overflow remains application-controlled.
DISPLAY=$display "$fixture_bin" partial >"$tmp_dir/partial.log" 2>&1 & client_pid=$!
wait_for "DISPLAY=$display xdotool search --name PartialHintsClient >/dev/null 2>&1" || fail "partial fixture missing"
client=$(DISPLAY=$display xdotool search --name PartialHintsClient | head -n 1)
assert_size "$client" 200 120 "partial initial size"
assert_position "$client" 750 550 "partial initial placement"
stop_client

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb cached WM_NORMAL_HINTS and initial geometry scenario"
