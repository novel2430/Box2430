#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:198}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
observer_bin=${BOX2430_TRANSITION_OBSERVER_BIN:-./build/debug/x11-workspace-transition-observer}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= incoming_pid= outgoing_pid= observer_pid=

cleanup() {
    for pid in "$observer_pid" "$outgoing_pid" "$incoming_pid" "$wm_pid" "$xvfb_pid"; do
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
assert_state() {
    DISPLAY=$display xwininfo -id "$1" | grep -q "Map State: $2" ||
        fail "$3"
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
observe_switch() {
    old_window=$1 new_window=$2 key=$3 label=$4
    observer_log="$tmp_dir/observer-$label.log"
    DISPLAY=$display "$observer_bin" "$old_window" "$new_window" >"$observer_log" 2>&1 &
    observer_pid=$!
    wait_for "grep -q READY $observer_log" || fail "$label observer did not become ready"
    DISPLAY=$display xdotool key "$key"
    if ! wait "$observer_pid"; then
        observer_pid=
        sed -n '1,80p' "$observer_log" >&2
        fail "$label mapped/unmapped in the wrong order"
    fi
    observer_pid=
}
assert_tab_state() {
    wait_for "DISPLAY=$display xwininfo -id $1 | grep -q 'Map State: $2'" ||
        fail "$3"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-workspace-transition.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

# Prepare a maximized destination while it is still on the visible workspace,
# then move it to workspace 2 without following it.
DISPLAY=$display xterm -title TransitionIncoming -geometry 30x8 \
    >"$tmp_dir/incoming.log" 2>&1 &
incoming_pid=$!
wait_for "DISPLAY=$display xdotool search --name TransitionIncoming >/dev/null 2>&1" ||
    fail "incoming client missing"
incoming=$(DISPLAY=$display xdotool search --name TransitionIncoming | head -n 1)
wait_active "$incoming" || fail "incoming client was not focused on map"
DISPLAY=$display xdotool key super+Up
wait_for "test \"\$(DISPLAY=$display xwininfo -id $incoming | awk '/Width:/ {print \$2; exit}')\" = 796" ||
    fail "incoming client did not maximize"
DISPLAY=$display xdotool key super+shift+2
wait_for "DISPLAY=$display xwininfo -id $incoming | grep -q 'Map State: IsUnMapped'" ||
    fail "incoming client did not move to workspace 2"

DISPLAY=$display xterm -title TransitionOutgoing -geometry 25x7 \
    >"$tmp_dir/outgoing.log" 2>&1 &
outgoing_pid=$!
wait_for "DISPLAY=$display xdotool search --name TransitionOutgoing >/dev/null 2>&1" ||
    fail "outgoing client missing"
outgoing=$(DISPLAY=$display xdotool search --name TransitionOutgoing | head -n 1)

observe_switch "$outgoing" "$incoming" super+2 forward
assert_state "$incoming" IsViewable "incoming client was not viewable after switch"
assert_state "$outgoing" IsUnMapped "outgoing client remained mapped after switch"
wait_active "$incoming" || fail "incoming client did not receive focus"
[ "$(field "$incoming" 'Absolute upper-left X:')" = 0 ] || fail "maximized incoming x changed"
[ "$(field "$incoming" 'Absolute upper-left Y:')" = 0 ] || fail "maximized incoming y changed"
[ "$(field "$incoming" 'Width:')" = 796 ] || fail "maximized incoming width changed"
[ "$(field "$incoming" 'Height:')" = 596 ] || fail "maximized incoming height changed"

# raise_on_focus=true forces focus_client() through enforce_stacking() while
# both workspaces overlap. A real-fullscreen source must not be resurrected by
# the global fullscreen pass during that interval.
DISPLAY=$display xdotool key super+f
wait_for "test \"\$(DISPLAY=$display xwininfo -id $incoming | awk '/Border width:/ {print \$3}')\" = 0" ||
    fail "incoming client did not enter real fullscreen"
observe_switch "$incoming" "$outgoing" super+1 reverse-fullscreen
assert_state "$outgoing" IsViewable "reverse incoming client was not viewable"
assert_state "$incoming" IsUnMapped "fullscreen source remained mapped after reverse switch"
wait_active "$outgoing" || fail "reverse incoming client did not receive focus"

# Exercise tab-bar publication in every workspace-mode direction. Existing
# tab and stacking machinery remains responsible for the final bar state.
DISPLAY=$display xdotool key super+m
wait_for "DISPLAY=$display xdotool search --name box2430-tabbar-0 >/dev/null 2>&1" ||
    fail "tab bar did not appear in MONOCLE"
bar=$(DISPLAY=$display xdotool search --name box2430-tabbar-0 | head -n 1)
assert_tab_state "$bar" IsViewable "MONOCLE tab bar was not viewable"
DISPLAY=$display xdotool key super+2
assert_tab_state "$bar" IsUnMapped "MONOCLE to FREE left tab bar mapped"
DISPLAY=$display xdotool key super+m
assert_tab_state "$bar" IsViewable "FREE destination did not enter MONOCLE"
DISPLAY=$display xdotool key super+1
assert_tab_state "$bar" IsViewable "MONOCLE to MONOCLE hid tab bar"
DISPLAY=$display xdotool key super+m
assert_tab_state "$bar" IsUnMapped "MONOCLE source did not return to FREE"
DISPLAY=$display xdotool key super+2
assert_tab_state "$bar" IsViewable "FREE to MONOCLE did not show tab bar"
DISPLAY=$display xdotool key super+m
assert_tab_state "$bar" IsUnMapped "MONOCLE destination did not return to FREE"
DISPLAY=$display xdotool key super+1
assert_tab_state "$bar" IsUnMapped "FREE to FREE showed tab bar"

kill "$incoming_pid" "$outgoing_pid" 2>/dev/null || true
incoming_pid= outgoing_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb workspace transition ordering scenario"
