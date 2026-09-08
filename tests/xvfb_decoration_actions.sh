#!/bin/sh
set -eu

export DISPLAY=${BOX2430_TEST_DISPLAY:-:239}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture=./build/debug/x11-test-client
mutator=./build/debug/x11-property-mutator
stack=./build/debug/x11-stacking-order
tmp_dir=$(mktemp -d)
pids= wm_pid=
cleanup() {
    result=$?
    for pid in $wm_pid $pids; do kill "$pid" 2>/dev/null || true; done
    if [ "$result" -ne 0 ]; then echo "Action logs retained at $tmp_dir" >&2
    else rm -rf "$tmp_dir"; fi
}
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }
wait_until() {
    attempts=0
    while ! "$@"; do
        attempts=$((attempts + 1))
        [ "$attempts" -lt 150 ] || return 1
        sleep 0.02
    done
}
named() { xdotool search --name "^$1$" 2>/dev/null | head -n 1; }
has_name() { [ -n "$(named "$1")" ]; }
deco() { named "box2430-decoration-$(printf '0x%x' "$1")"; }
mapped() { xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'; }
hidden() { ! mapped "$1"; }
geometry() {
    xwininfo -id "$1" | awk '/Absolute upper-left X:/ {x=$NF}
        /Absolute upper-left Y:/ {y=$NF} /Width:/ {w=$NF} /Height:/ {h=$NF}
        END {print x,y,w,h}'
}
geometry_is() { [ "$(geometry "$1")" = "$2" ]; }
assert_geometry() { wait_until geometry_is "$1" "$2" || fail "$3: $(geometry "$1")"; }
pointer_is() { [ "$(xdotool getmouselocation --shell | awk -F= '/^X=/ {x=$2} /^Y=/ {y=$2} END {print x,y}')" = "$1" ]; }
grab_free() { ./build/debug/decoration-test --pointer-grab; }
grab_held() { ! grab_free; }
spawn() {
    "$fixture" NORMAL "$1" "$2" 100 220 140 >"$tmp_dir/$1.log" 2>&1 &
    pids="$! $pids"
    wait_until has_name "$1" || fail 'fixture missing'
    spawned=$(named "$1")
    wait_until has_name "box2430-decoration-$(printf '0x%x' "$spawned")" || fail 'decoration missing'
}
click_title() { xdotool mousemove --window "$(deco "$1")" 30 12 click "$2"; }
clear_clicks() { xdotool mousemove 790 590 click 1; }

Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
pids=$!
wait_until xdpyinfo >/dev/null 2>&1 || fail 'Xvfb missing'
"$box2430_bin" -c tests/fixtures/config-decoration-bindings.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_until has_name box2430-bar-0 || fail 'WM missing'
spawn DecActionA 100
a=$spawned da=$(deco "$spawned")
spawn DecActionB 400
b=$spawned db=$(deco "$spawned")

# Custom raise activates the clicked owner; custom lower must not target focus.
click_title "$a" 3
wait_until "$stack" "$db" "$a" || fail 'right binding did not raise owner'
[ "$(xdotool getwindowfocus)" = "$a" ] || fail 'raise did not activate owner'
xdotool windowactivate "$b"
"$mutator" input "$a" false
click_title "$a" 1
wait_until "$stack" "$da" "$b" || fail 'click lower targeted focused client'
[ "$(xdotool getwindowfocus)" = "$b" ] || fail 'owner input policy ignored'
assert_geometry "$a" '100 100 220 140' 'lower moved geometry'

# Maximize also targets the unfocusable owner, and restores through the old path.
click_title "$a" 2
assert_geometry "$a" '0 48 796 548' 'middle binding did not maximize owner'
assert_geometry "$b" '400 100 220 140' 'middle binding changed focused client'
[ "$(xdotool getwindowfocus)" = "$b" ] || fail 'maximize stole focus'
click_title "$a" 2
assert_geometry "$a" '100 100 220 140' 'middle binding restore'
"$mutator" input "$a" true

clear_clicks
click_title "$a" 3
xdotool mousemove --window "$da" 30 12 click --repeat 2 --delay 50 1
assert_geometry "$a" '100 100 220 140' 'double_click none still maximized'
"$stack" "$da" "$b" || fail 'second click failed to lower'

# Secondary buttons are simple clicks: threshold excursions cancel, not move.
xdotool mousemove --window "$da" 30 12 mousedown 2
wait_until grab_held || fail 'middle press missing'
xdotool mousemove_relative --sync 4 0 mousemove_relative --sync -- -4 0 mouseup 2
wait_until grab_free || fail 'middle release leaked grab'
assert_geometry "$a" '100 100 220 140' 'middle motion moved/maximized owner'

# Binding changes do not affect threshold, preserved anchor, or snap completion.
clear_clicks
click_title "$a" 3
xdotool mousemove --window "$da" 30 12 mousedown 1
wait_until grab_held || fail 'primary press missing'
xdotool mousemove_relative --sync 3 0
assert_geometry "$a" '100 100 220 140' 'custom click changed threshold'
xdotool mouseup 1
wait_until "$stack" "$da" "$b" || fail 'subthreshold custom click did not lower'
click_title "$a" 3
xdotool mousemove --window "$da" 30 12 mousedown 1 mousemove_relative --sync 4 0
assert_geometry "$a" '104 100 220 140' 'configured drag lost threshold'
pointer_is '134 88' || fail 'configured drag warped pointer'
xdotool mouseup 1
wait_until grab_free || fail 'configured drag leaked grab'
"$stack" "$db" "$a" || fail 'drag incorrectly executed click lower'
xdotool key super+Up key super+Up
assert_geometry "$a" '104 100 220 140' 'configured drag did not commit normal geometry'
xdotool mousemove --window "$da" 30 12 mousedown 1 mousemove --sync 0 300
assert_geometry "$a" '-30 312 220 140' 'snap drag motion'
xdotool mouseup 1
assert_geometry "$a" '0 48 396 548' 'configured drag lost snap completion'
xdotool key super+n

# A secondary pending action must be cancelled when its titlebar disappears.
for transition in motif workspace monocle fullscreen; do
    xdotool windowactivate "$a"
    before=$(geometry "$a")
    xdotool mousemove --window "$da" 30 12 mousedown 2
    wait_until grab_held || fail 'secondary pending setup'
    case $transition in
        motif) "$mutator" motif "$a" off ;;
        workspace) xdotool key super+2 ;;
        monocle) xdotool key super+m ;;
        fullscreen) xdotool key super+f ;;
    esac
    wait_until hidden "$da" || fail "$transition did not hide titlebar"
    wait_until grab_free || fail "$transition secondary grab leak"
    hidden_geometry=$(geometry "$a")
    xdotool mouseup 2
    # Synthetic events addressed to the retained unmapped sibling do nothing.
    xdotool click --window "$da" 2
    assert_geometry "$a" "$hidden_geometry" "$transition hidden decoration ran action"
    case $transition in
        motif) "$mutator" motif "$a" on ;;
        workspace) xdotool key super+1 ;;
        monocle) xdotool key super+m ;;
        fullscreen) xdotool key super+f ;;
    esac
    wait_until mapped "$da" || fail "$transition restore titlebar"
    assert_geometry "$a" "$before" "$transition retained pending action"
done

spawn DecActionDestroy 400
victim=$spawned
xdotool mousemove --window "$(deco "$victim")" 30 12 mousedown 3
wait_until grab_held || fail 'secondary destroy setup'
xdotool windowkill "$victim"
wait_until grab_free || fail 'secondary owner destruction leaked grab'
xdotool mouseup 3
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -E 'X11 error|semantic invariant failed|AddressSanitizer|runtime error:|invalid.*config' "$tmp_dir/wm.log"; then
    fail 'WM diagnostics'
fi
echo 'PASS: Xvfb decoration actions, custom bindings, owner targets and unchanged drag'
