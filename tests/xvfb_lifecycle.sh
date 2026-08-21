#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:141}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
lifecycle_bin=${BOX2430_LIFECYCLE_BIN:-./build/debug/x11-lifecycle-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= transient_pid= override_pid= ordinary_pid= iconic_pid= dock_pid= border_pid=

cleanup() {
    for pid in "$border_pid" "$transient_pid" "$override_pid" "$ordinary_pid" "$iconic_pid" "$dock_pid" "$wm_pid" "$xvfb_pid"; do
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

# Create ordinary and transient clients before the dock so XQueryTree order
# cannot accidentally establish the correct workarea or parent relationship.
DISPLAY=$display "$fixture_bin" NORMAL StartupOrdinary 10 10 200 100 >"$tmp_dir/ordinary.log" 2>&1 &
ordinary_pid=$!
wait_for "DISPLAY=$display xdotool search --name StartupOrdinary >/dev/null 2>&1" || fail "startup ordinary client missing"
ordinary=$(DISPLAY=$display xdotool search --name StartupOrdinary | head -n 1)

# IconicState is a cold-start discovery signal only.  This window starts
# unmapped and must be adopted into the ordinary visible workspace model.
DISPLAY=$display "$lifecycle_bin" iconic StartupIconic >"$tmp_dir/iconic.ids" 2>"$tmp_dir/iconic.log" &
iconic_pid=$!
wait_for "test -s $tmp_dir/iconic.ids" || fail "iconic startup fixture did not start"
iconic=$(head -n 1 "$tmp_dir/iconic.ids")
DISPLAY=$display xwininfo -id "$iconic" | grep -q 'Map State: IsUnMapped' || fail "iconic startup fixture unexpectedly mapped"
DISPLAY=$display xprop -id "$iconic" WM_STATE | grep -q 'window state: Iconic' || fail "iconic startup fixture lacks IconicState"

DISPLAY=$display "$lifecycle_bin" transient StartupParent StartupDialog >"$tmp_dir/transient.ids" 2>"$tmp_dir/transient.log" &
transient_pid=$!
wait_for "test -s $tmp_dir/transient.ids" || fail "transient fixture did not start"
read -r parent dialog <"$tmp_dir/transient.ids"

DISPLAY=$display "$lifecycle_bin" transient StartupOverrideParent StartupOverrideDialog >"$tmp_dir/override.ids" 2>"$tmp_dir/override.log" &
override_pid=$!
wait_for "test -s $tmp_dir/override.ids" || fail "override transient fixture did not start"
read -r override_parent override_dialog <"$tmp_dir/override.ids"

DISPLAY=$display "$fixture_bin" DOCK StartupDock 0 0 800 40 40 >"$tmp_dir/dock.log" 2>&1 &
dock_pid=$!
wait_for "DISPLAY=$display xdotool search --name StartupDock >/dev/null 2>&1" || fail "startup dock missing"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-lifecycle.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_WORKAREA | grep -q '0, 40, 800, 560'" || fail "startup dock was not managed first"
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $(printf '0x%x' "$dialog")" || fail "startup dialog was not managed"
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $(printf '0x%x' "$iconic")" || fail "IconicState startup client was not adopted"
wait_for "DISPLAY=$display xwininfo -id $iconic | grep -q 'Map State: IsViewable'" || fail "adopted IconicState client was not normalized to visible"
DISPLAY=$display xprop -id "$iconic" WM_STATE | grep -q 'window state: Normal' || fail "adopted IconicState client retained iconic runtime state"

ordinary_width=$(field "$ordinary" 'Width:')
ordinary_height=$(field "$ordinary" 'Height:')
expected_x=$(((800 - ordinary_width) / 2))
expected_y=$((40 + (560 - ordinary_height) / 2))
[ "$(field "$ordinary" 'Absolute upper-left X:')" = "$expected_x" ] || fail "ordinary startup placement ignored dock workarea"
[ "$(field "$ordinary" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "ordinary startup placement ignored dock workarea"

wait_for "DISPLAY=$display xwininfo -id $parent | grep -q 'Map State: IsUnMapped'" || fail "parent rule did not place parent on workspace 2"
wait_for "DISPLAY=$display xwininfo -id $dialog | grep -q 'Map State: IsUnMapped'" || fail "startup transient did not inherit parent workspace"
wait_for "DISPLAY=$display xwininfo -id $override_parent | grep -q 'Map State: IsUnMapped'" || fail "override parent was not placed on workspace 2"
DISPLAY=$display xdotool key super+2
wait_for "DISPLAY=$display xwininfo -id $parent | grep -q 'Map State: IsViewable'" || fail "parent did not appear on workspace 2"
wait_for "DISPLAY=$display xwininfo -id $dialog | grep -q 'Map State: IsViewable'" || fail "transient did not follow parent to workspace 2"
wait_for "DISPLAY=$display xwininfo -id $override_dialog | grep -q 'Map State: IsUnMapped'" || fail "explicit dialog rule did not override parent workspace"
DISPLAY=$display xdotool key super+3
wait_for "DISPLAY=$display xwininfo -id $override_dialog | grep -q 'Map State: IsViewable'" || fail "rule-overridden transient did not appear on workspace 3"

DISPLAY=$display "$lifecycle_bin" border BorderClient 7 >"$tmp_dir/border.ids" 2>"$tmp_dir/border.log" &
border_pid=$!
wait_for "test -s $tmp_dir/border.ids" || fail "border fixture did not start"
border=$(head -n 1 "$tmp_dir/border.ids")
wait_for "test \"\$(DISPLAY=$display xwininfo -id $border | awk '/Border width:/ {print \$3}')\" = 4" || fail "WM border was not applied"

kill "$wm_pid"
wait "$wm_pid"
wm_pid=
wait_for "test \"\$(DISPLAY=$display xwininfo -id $border | awk '/Border width:/ {print \$3}')\" = 7" || fail "application border was not restored"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $iconic | awk '/Border width:/ {print \$3}')\" = 3" || fail "adopted IconicState client border was not restored"

if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb startup scan/transient/border lifecycle scenario"
