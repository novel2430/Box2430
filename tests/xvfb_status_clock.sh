#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:181}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
property_bin=${BOX2430_PROPERTY_BIN:-./build/debug/x11-property-mutator}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
color_bin=${BOX2430_COLOR_BIN:-./build/debug/x11-window-color}
hash_bin=${BOX2430_HASH_BIN:-./build/debug/x11-window-hash}
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
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
color_bounds() { DISPLAY=$display "$color_bin" "$1" "$2"; }
rect_width() { echo $(( $3 - $1 + 1 )); }

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp -noreset >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

# Seed only legacy WM_NAME before startup.  Phase 5 must read this immediately
# because _NET_WM_NAME is absent.
DISPLAY=$display "$property_bin" wm-name root \
    'LEGACY STARTUP STATUS FROM WM_NAME'
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-status-clock.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
wait_for "DISPLAY=$display xdotool search --name '^box2430-bar-0$' >/dev/null 2>&1" || fail "bar missing"
bar=$(DISPLAY=$display xdotool search --name '^box2430-bar-0$' | head -n 1)
wait_for "DISPLAY=$display $color_bin $bar '#550000' >/dev/null 2>&1" || fail "startup WM_NAME status was not rendered"
set -- $(color_bounds "$bar" '#550000')
legacy_width=$(rect_width "$@")
[ "$legacy_width" -gt 100 ] || fail "startup fallback status width is unexpectedly small"
wait_for "DISPLAY=$display $color_bin $bar '#005555' >/dev/null 2>&1" || fail "clock widget was not rendered"
set -- $(color_bounds "$bar" '#005555')
clock_width=$(( $3 - $1 + 1 ))
[ "$clock_width" -gt 0 ] || fail "clock width is zero"

# The internally formatted seconds clock must change without any X event or
# external status producer waking the WM.
before_hash=$(DISPLAY=$display "$hash_bin" "$bar")
wait_for "test \"\$(DISPLAY=$display $hash_bin $bar)\" != '$before_hash'" || fail "clock did not refresh on the poll timeout"

# _NET_WM_NAME takes precedence and root PropertyNotify must redraw the status.
DISPLAY=$display "$property_bin" net-name root N
wait_for "set -- \$(DISPLAY=$display $color_bin $bar '#550000'); test \$((\$3 - \$1 + 1)) -lt $legacy_width" || fail "_NET_WM_NAME update did not replace legacy status"
set -- $(color_bounds "$bar" '#550000')
net_width=$(( $3 - $1 + 1 ))

# Changing WM_NAME while a valid _NET_WM_NAME exists must not override it.
DISPLAY=$display "$property_bin" wm-name root \
    'THIS LEGACY STATUS MUST STAY HIDDEN WHILE NET WM NAME EXISTS'
sleep 0.10
set -- $(color_bounds "$bar" '#550000')
[ $(( $3 - $1 + 1 )) -eq "$net_width" ] || fail "WM_NAME incorrectly overrode _NET_WM_NAME"

# Deleting the preferred property must fall back to the current WM_NAME.
DISPLAY=$display "$property_bin" net-name root none
wait_for "set -- \$(DISPLAY=$display $color_bin $bar '#550000'); test \$((\$3 - \$1 + 1)) -gt $net_width" || fail "deleting _NET_WM_NAME did not restore WM_NAME fallback"

# Return to a short preferred status so the centered title is initially visible
# before the explicit long-status pressure case below.
DISPLAY=$display "$property_bin" net-name root OK
wait_for "set -- \$(DISPLAY=$display $color_bin $bar '#550000'); test \$((\$3 - \$1 + 1)) -lt 100" || fail "short preferred status did not restore center space"

# Give the center a long title, then apply an extreme status string.  Status is
# the shrinkable edge widget; workspaces and clock must keep their natural
# widths, and no edge rectangles may overlap even when the title is squeezed
# completely out of the symmetric center region.
DISPLAY=$display "$fixture_bin" NORMAL \
    'Phase5 centered title that should yield under external status pressure 0123456789' \
    100 100 240 120 >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^Phase5 centered title' >/dev/null 2>&1" || fail "title client missing"
wait_for "DISPLAY=$display $color_bin $bar '#000055' >/dev/null 2>&1" || fail "title widget did not render before pressure"

set -- $(color_bounds "$bar" '#003300')
active_width=$(( $3 - $1 + 1 ))
DISPLAY=$display "$property_bin" net-name root \
    'STATUS-ABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZ-0123456789'
wait_for "DISPLAY=$display $color_bin $bar '#550000' >/dev/null 2>&1" || fail "long status disappeared"

set -- $(color_bounds "$bar" '#003300')
active_min=$1 active_max=$3
[ $((active_max - active_min + 1)) -eq "$active_width" ] || fail "fixed workspace width shrank before status"
set -- $(color_bounds "$bar" '#111111')
empty_max=$3
left_max=$active_max
[ "$empty_max" -gt "$left_max" ] && left_max=$empty_max
set -- $(color_bounds "$bar" '#550000')
status_min=$1 status_max=$3
set -- $(color_bounds "$bar" '#005555')
clock_min=$1 clock_max=$3
[ $((clock_max - clock_min + 1)) -eq "$clock_width" ] || fail "fixed clock width shrank under status pressure"
[ "$left_max" -lt "$status_min" ] || fail "long status overlapped left fixed widgets"
[ "$status_max" -lt "$clock_min" ] || fail "long status overlapped fixed clock widget"
if DISPLAY=$display "$color_bin" "$bar" '#000055' >/dev/null 2>&1; then
    set -- $(color_bounds "$bar" '#000055')
    title_min=$1 title_max=$3
    [ $((title_min + title_max + 1)) -eq 800 ] || fail "pressured title lost physical centering"
    [ "$title_min" -gt "$left_max" ] && [ "$title_max" -lt "$status_min" ] ||
        fail "pressured title overlapped an edge group"
fi

kill "$client_pid" 2>/dev/null || true
wait "$client_pid" 2>/dev/null || true
client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xvfb root-status/internal-clock/status-pressure scenario"
