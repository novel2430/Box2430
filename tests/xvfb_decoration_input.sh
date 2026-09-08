#!/bin/sh
set -eu

export DISPLAY=${BOX2430_TEST_DISPLAY:-:237}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture=./build/debug/x11-test-client
mutator=./build/debug/x11-property-mutator
stack=./build/debug/x11-stacking-order
tmp_dir=$(mktemp -d)
pids= wm_pid=
cleanup() {
    result=$?
    for pid in $wm_pid $pids; do kill "$pid" 2>/dev/null || true; done
    if [ "$result" -ne 0 ]; then echo "Input logs retained at $tmp_dir" >&2
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
deco() { named "box2430-decoration-$(printf '0x%x' "$1")"; }
has_name() { [ -n "$(named "$1")" ]; }
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
no_resource() { [ -z "$(deco "$1")" ]; }
spawn() {
    "$fixture" NORMAL "$1" "$2" 100 220 140 >"$tmp_dir/$1.log" 2>&1 &
    pids="$! $pids"
    wait_until has_name "$1" || fail 'fixture missing'
    spawned=$(named "$1")
    wait_until has_name "box2430-decoration-$(printf '0x%x' "$spawned")" || fail 'decoration missing'
}
press() {
    xdotool mousemove --window "$(deco "$1")" 30 12 mousedown 1
    wait_until grab_held || fail 'titlebar did not grab pointer'
}
release() { xdotool mouseup 1; wait_until grab_free || fail 'release leaked grab'; }
clear_clicks() { xdotool mousemove 790 590 click 1; }
click_title() { xdotool mousemove --window "$(deco "$1")" 30 12 click 1; }

Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
pids=$!
wait_until xdpyinfo >/dev/null 2>&1 || fail 'Xvfb missing'
"$box2430_bin" -c tests/fixtures/config-decoration.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_until has_name box2430-bar-0 || fail 'WM missing'
spawn DecInputA 100
a=$spawned da=$(deco "$spawned")
spawn DecInputB 400
b=$spawned db=$(deco "$spawned")

# Press activates, but no movement/warp; first release raises immediately,
# without waiting out a possible double click interval.
"$stack" "$da" "$b" || fail 'initial stack'
press "$a"
pointer_is '130 88' || fail 'press warped pointer'
assert_geometry "$a" '100 100 220 140' 'press changed geometry'
"$stack" "$da" "$b" || fail 'press raised without focus-raise policy'
xdotool mouseup 1
for attempt in $(seq 1 10); do
    "$stack" "$db" "$a" && break
    sleep 0.005
done
"$stack" "$db" "$a" || fail 'single click was delayed'
assert_geometry "$a" '100 100 220 140' 'click moved client'

# Raising uses the titlebar owner even when ICCCM input policy keeps focus on B.
clear_clicks
"$mutator" input "$a" false
xdotool windowactivate "$b" key super+r
press "$a"
release
"$stack" "$db" "$a" || fail 'click raised focused client instead of owner'
[ "$(xdotool getwindowfocus)" = "$b" ] || fail 'input-hint policy ignored'
assert_geometry "$a" '100 100 220 140' 'unfocusable owner click moved geometry'
"$mutator" input "$a" true

# Default secondary clicks target the owner without implicit activation.
clear_clicks
xdotool windowactivate "$b"
xdotool mousemove --window "$da" 30 12 click 3
"$stack" "$db" "$a" || fail 'default right click changed stack'
[ "$(xdotool getwindowfocus)" = "$b" ] || fail 'default right click changed focus'
xdotool mousemove --window "$da" 30 12 click 2
wait_until "$stack" "$da" "$b" || fail 'default middle click did not lower owner'
[ "$(xdotool getwindowfocus)" = "$b" ] || fail 'middle click changed focus'
assert_geometry "$a" '100 100 220 140' 'secondary click moved client'

clear_clicks
press "$a"
xdotool mousemove_relative --sync 3 0
assert_geometry "$a" '100 100 220 140' 'subthreshold movement dragged'
release
assert_geometry "$a" '100 100 220 140' 'small movement was not click'
clear_clicks
press "$a"
xdotool mousemove_relative --sync 4 0
assert_geometry "$a" '104 100 220 140' '4px threshold did not start drag'
pointer_is '134 88' || fail 'threshold warped pointer'
xdotool mousemove_relative --sync 6 5
assert_geometry "$a" '110 105 220 140' 'anchored motion'
release
xdotool key super+Up key super+Up
assert_geometry "$a" '110 105 220 140' 'drag completion lost normal geometry'

clear_clicks
xdotool mousemove --window "$da" 30 12 click --repeat 2 --delay 50 1
assert_geometry "$a" '0 48 796 548' 'double click maximize'
xdotool mousemove --window "$da" 30 12 click --repeat 2 --delay 50 1
assert_geometry "$a" '110 105 220 140' 'double click restore'
clear_clicks
click_title "$a"
click_title "$b"
assert_geometry "$b" '400 100 220 140' 'cross-client double click'

# A click, an out-and-back drag, then a click are NOT a double click.
clear_clicks
xdotool mousemove --window "$da" 30 12 click 1 mousedown 1 \
    mousemove_relative --sync 4 0 sleep 0.02 \
    mousemove_relative --sync -- -4 0 mouseup 1 click 1
assert_geometry "$a" '110 105 220 140' 'drag polluted click history'

# The original content binding still warps immediately to content center.
clear_clicks
xdotool keydown super mousemove --window "$a" 20 20 mousedown 1
wait_until pointer_is '222 177' || fail 'content move lost center warp'
xdotool mouseup 1 keyup super

# Snap/maximize restore uses the same move runtime, preserving the title anchor.
xdotool key super+Up
assert_geometry "$a" '0 48 796 548' 'setup maximized drag'
press "$a"
pointer_is '30 36' || fail 'maximized press warped'
xdotool mousemove_relative --sync 10 10
assert_geometry "$a" '10 58 220 140' 'maximized drag did not preserve anchor/restore size'
pointer_is '40 46' || fail 'maximized drag warped'
release
press "$a"
xdotool mousemove --sync 0 300
preview() {
    preview_window=$(xwininfo -root -tree | awk '/400x2\+0\+24/ {print $1; exit}')
    [ -n "$preview_window" ] && mapped "$preview_window"
}
wait_until preview || fail 'titlebar move lost snap preview'
release
assert_geometry "$a" '0 48 396 548' 'titlebar snap release'
xdotool key super+n

# Cancellation is checked using another X client's actual pointer grab.
for transition in motif workspace monocle fullscreen; do
    clear_clicks
    before=$(geometry "$a")
    press "$a"
    case $transition in
        motif) "$mutator" motif "$a" off ;;
        workspace) xdotool key super+2 ;;
        monocle) xdotool key super+m ;;
        fullscreen) xdotool key super+f ;;
    esac
    wait_until hidden "$da" || fail "$transition did not hide titlebar"
    wait_until grab_free || fail "$transition pending grab leak"
    xdotool mousemove_relative --sync 20 20 mouseup 1
    case $transition in
        motif) "$mutator" motif "$a" on ;;
        workspace) xdotool key super+1 ;;
        monocle) xdotool key super+m ;;
        fullscreen) xdotool key super+f ;;
    esac
    wait_until mapped "$da" || fail "$transition did not restore titlebar"
    assert_geometry "$a" "$before" "$transition pending sequence moved client"
    click_title "$a"
    assert_geometry "$a" "$before" "$transition retained double-click history"
done

press "$a"
xdotool mousemove --sync 0 0
wait_until preview || fail 'cancel setup preview'
"$mutator" motif "$a" off
wait_until grab_free || fail 'hidden drag retained grab'
no_preview() { ! preview; }
wait_until no_preview || fail 'cancelled drag retained snap preview'
stopped=$(geometry "$a")
xdotool mousemove_relative --sync 30 20 mouseup 1
assert_geometry "$a" "$stopped" 'cancelled drag kept moving'
"$mutator" motif "$a" on

for phase in pending drag; do
    spawn "DecInputDestroy$phase" 400
    victim=$spawned
    press "$victim"
    if [ "$phase" = drag ]; then
        xdotool mousemove_relative --sync 8 0
        assert_geometry "$victim" '408 100 220 140' 'destroy drag setup'
    fi
    xdotool windowkill "$victim"
    wait_until no_resource "$victim" || fail 'destroy orphan'
    wait_until grab_free || fail 'destroy leaked pointer grab'
    xdotool mouseup 1
done
spawn DecInputHistory 400
victim=$spawned
click_title "$victim"
xdotool windowkill "$victim"
wait_until no_resource "$victim" || fail 'click-history owner teardown'
click_title "$b"
assert_geometry "$b" '400 100 220 140' 'destroyed owner retained click history'
press "$b"
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
wait_until grab_free || fail 'shutdown leaked grab'
xdotool mouseup 1
if grep -E 'X11 error|semantic invariant failed|AddressSanitizer|runtime error:' "$tmp_dir/wm.log"; then
    fail 'WM diagnostics'
fi
echo 'PASS: Xvfb titlebar click/threshold/no-warp/double-click/cancel state machine'
