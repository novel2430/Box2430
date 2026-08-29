#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:128}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= base_pid= special_pid= dialog_pid= allow_pid=

cleanup() {
    for pid in "$base_pid" "$special_pid" "$dialog_pid" "$allow_pid" "$wm_pid" "$xvfb_pid"; do
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
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-rules.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display xterm -class RuleBase -name rule-base -title Ordinary \
    -geometry 30x8+17+23 >"$tmp_dir/base.log" 2>&1 &
base_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -q 0x" || fail "base rule client missing"
base=$(DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -o '0x[0-9a-fA-F]*' | head -n 1)
DISPLAY=$display xwininfo -id "$base" | grep -q 'Map State: IsUnMapped' || fail "rule workspace destination remained visible"
[ "$(field "$base" 'Border width:')" = 0 ] || fail "rule border=false not applied"
[ "$(field "$base" 'Absolute upper-left X:')" = 17 ] || fail "rule client placement not applied"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q 0x0 || fail "hidden rule client stole focus"

DISPLAY=$display xterm -class RuleApp -name rule-special -title 'Rule Special' \
    -geometry 30x8+17+23 >"$tmp_dir/special.log" 2>&1 &
special_pid=$!
wait_for "test \"\$(DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -o '0x[0-9a-fA-F]*' | wc -l)\" -ge 2" || fail "ordered-rule client missing"
special=$(DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -o '0x[0-9a-fA-F]*' | head -n 1)
DISPLAY=$display xwininfo -id "$special" | grep -q 'Map State: IsUnMapped' || fail "later workspace rule not applied"
[ "$(field "$special" 'Border width:')" = 2 ] || fail "later border rule did not override"
[ "$(field "$special" 'Absolute upper-left X:')" != 17 ] || fail "later placement rule did not override"

DISPLAY=$display xdotool windowstate --add FULLSCREEN "$special"
sleep 0.05
if DISPLAY=$display xprop -id "$special" _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN; then
    fail "later deny fullscreen policy did not override fake"
fi

# Metadata changes are not a reactive rule engine.
DISPLAY=$display xdotool set_window --name OrdinaryNow "$special"
DISPLAY=$display xdotool key super+3
wait_for "DISPLAY=$display xwininfo -id $special | grep -q 'Map State: IsViewable'" || fail "workspace 3 did not reveal rule client"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$special")" ||
    fail "workspace 3 did not focus its available fallback client"
[ "$(field "$special" 'Border width:')" = 2 ] || fail "title change reactively reapplied rules"

DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $base | grep -q 'Map State: IsViewable'" || fail "workspace 2 did not reveal base client"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$base")" ||
    fail "workspace 2 did not focus its available fallback client"
[ "$(field "$base" 'Border width:')" = 0 ] || fail "base rule state was lost"

DISPLAY=$display "$fixture_bin" DIALOG FixtureDialog 19 29 180 90 >"$tmp_dir/dialog.log" 2>&1 &
dialog_pid=$!
wait_for "DISPLAY=$display xdotool search --name FixtureDialog >/dev/null 2>&1" || fail "dialog client missing"
dialog=$(DISPLAY=$display xdotool search --name FixtureDialog | head -n 1)
[ "$(field "$dialog" 'Absolute upper-left X:')" = 19 ] || fail "dialog placement config not applied"
[ "$(field "$dialog" 'Absolute upper-left Y:')" = 29 ] || fail "dialog placement y not applied"

DISPLAY=$display xterm -class AllowApp -title AllowFullscreen >"$tmp_dir/allow.log" 2>&1 &
allow_pid=$!
wait_for "DISPLAY=$display xdotool search --name AllowFullscreen >/dev/null 2>&1" ||
    fail "allow-policy client missing"
allow=$(DISPLAY=$display xdotool search --name AllowFullscreen | head -n 1)
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $(printf '0x%x' "$allow")" ||
    fail "allow-policy client was not managed"
DISPLAY=$display xdotool windowstate --add FULLSCREEN "$allow"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $allow | awk '/Border width:/ {print \$3}')\" = 0" ||
    fail "allow fullscreen policy did not enter real fullscreen"
[ "$(field "$allow" 'Width:')" = 800 ] || fail "allow fullscreen width incorrect"
[ "$(field "$allow" 'Height:')" = 600 ] || fail "allow fullscreen height incorrect"
DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$allow"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $allow | awk '/Border width:/ {print \$3}')\" = 2" ||
    fail "allow fullscreen policy did not restore border"

kill "$base_pid" 2>/dev/null || true; base_pid=
kill "$special_pid" 2>/dev/null || true; special_pid=
kill "$dialog_pid" 2>/dev/null || true; dialog_pid=
kill "$allow_pid" 2>/dev/null || true; allow_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb one-shot ordered rules scenario"
