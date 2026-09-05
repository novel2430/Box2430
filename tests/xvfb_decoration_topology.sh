#!/bin/sh
set -eu

export DISPLAY=${BOX2430_TEST_DISPLAY:-:238}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
monitor=./build/debug/x11-randr-monitor
fixture=./build/debug/x11-test-client
tmp_dir=$(mktemp -d)
pids= wm_pid=
cleanup() {
    result=$?
    for pid in $wm_pid $pids; do kill "$pid" 2>/dev/null || true; done
    if [ "$result" -ne 0 ] || [ "${failed:-false}" = true ]; then
        echo "Decoration topology logs retained at $tmp_dir" >&2
    else
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT INT TERM
fail() { failed=true; echo "FAIL: $*" >&2; exit 1; }
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
geometry() {
    xwininfo -id "$1" | awk '/Absolute upper-left X:/ {x=$NF}
        /Absolute upper-left Y:/ {y=$NF} /Width:/ {w=$NF} /Height:/ {h=$NF}
        /Border width:/ {b=$NF} END {print x,y,w,h,b}'
}
geometry_is() { [ "$(geometry "$1")" = "$2" ]; }
assert_geometry() { wait_until geometry_is "$1" "$2" || fail "$3: $(geometry "$1") != $2"; }
visible() { xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'; }
hidden() { ! visible "$1"; }
has_monitor() { xrandr --listmonitors | grep -q "$1"; }
has_deco() { [ -n "$(deco "$1")" ]; }
has_name() { [ -n "$(named "$1")" ]; }
active() { xprop -root _NET_ACTIVE_WINDOW | grep -q "$(printf '0x%x' "$1")"; }
set_monitor() {
    "$monitor" set "$@" hold >"$tmp_dir/monitor-$1.log" 2>&1 &
    pids="$! $pids"
    wait_until has_monitor "$1" || fail "monitor $1 missing"
}
spawn() {
    "$fixture" NORMAL "$1" "$2" "$3" 220 140 >"$tmp_dir/$1.log" 2>&1 &
    pids="$! $pids"
    wait_until has_name "$1" || fail 'client missing'
    spawned=$(named "$1")
    wait_until has_deco "$spawned" || fail 'decoration missing'
    wait_until active "$spawned" || fail 'client did not focus'
}

Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
pids="$!"
wait_until xdpyinfo >/dev/null 2>&1 || fail 'Xvfb did not start'
set_monitor left 0 0 400 600 screen
set_monitor right 400 0 400 600 none
"$box2430_bin" -c tests/fixtures/config-decoration.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_until has_name box2430-bar-1 || fail 'two-monitor WM missing'
spawn DecTopologyA 100 100
a=$spawned da=$(deco "$spawned")
xdotool key super+Left
assert_geometry "$a" '0 48 196 548 2' 'initial left snap'
xdotool key super+shift+2
wait_until hidden "$da" || fail 'hidden snapped titlebar'
xdotool key super+ctrl+Right
spawn DecTopologyB 500 120
b=$spawned db=$(deco "$spawned")
assert_geometry "$db" '500 96 224 24 0' 'right titlebar'

# Move through the existing titlebar drag path across the monitor boundary.
xdotool mousemove --window "$db" 30 12 mousedown 1
sleep 0.05
xdotool mousemove --sync 200 300
assert_geometry "$b" '88 228 220 140 2' 'cross-monitor motion'
xdotool mouseup 1
wait_until active "$b" || fail 'cross-monitor drag focus'
assert_geometry "$db" '88 204 224 24 0' 'cross-monitor titlebar'
xdotool key super+o
assert_geometry "$b" '488 228 220 140 2' 'monitor translation'
assert_geometry "$db" '488 204 224 24 0' 'translated titlebar'

xdotool key super+f
assert_geometry "$b" '400 0 400 600 0' 'right fullscreen'
hidden "$db" || fail 'fullscreen titlebar'
"$monitor" rename right right 450 0 350 600 none
"$monitor" notify-root
assert_geometry "$b" '450 0 350 600 0' 'fullscreen topology projection'
xdotool key super+f
assert_geometry "$b" '538 228 220 140 2' 'fullscreen topology normal restore'
assert_geometry "$db" '538 204 224 24 0' 'titlebar re-derived after topology'
xdotool key super+Up
assert_geometry "$b" '450 48 346 548 2' 'resized monitor maximize'
assert_geometry "$db" '450 24 350 24 0' 'resized maximize titlebar'
xdotool key super+m
assert_geometry "$b" '450 48 350 552 0' 'MONOCLE topology geometry'
hidden "$db" || fail 'MONOCLE topology titlebar'

# Removed-monitor clients migrate into the surviving workspace's FREE mode;
# maximize and hidden snap rematerialize from content authority.
"$monitor" delete right
"$monitor" notify-root
assert_geometry "$b" '0 48 396 548 2' 'removed monitor maximize migration'
assert_geometry "$db" '0 24 400 24 0' 'migrated decoration'
wait_until active "$b" || fail 'selected monitor/focus repair'
"$monitor" rename left left 0 0 800 600 screen
"$monitor" notify-root
assert_geometry "$b" '0 48 796 548 2' 'grown monitor maximize'
assert_geometry "$db" '0 24 800 24 0' 'grown monitor titlebar'
xdotool key super+Up
assert_geometry "$b" '88 228 220 140 2' 'translated normal restore'
xdotool key super+2
assert_geometry "$a" '0 48 396 548 2' 'hidden snap topology rematerialization'
assert_geometry "$da" '0 24 400 24 0' 'hidden snap titlebar return'
xdotool key super+n
assert_geometry "$a" '100 100 220 140 2' 'snap normal restore after topology'

"$monitor" rename left left 0 0 400 600 screen
set_monitor right 400 0 400 600 none
"$monitor" notify-root
wait_until has_name box2430-bar-1 || fail 'added monitor UI missing'
xdotool key super+o
assert_geometry "$a" '500 100 220 140 2' 'move to added monitor'
assert_geometry "$da" '500 76 224 24 0' 'added monitor decoration'
[ "$(deco "$a")" = "$da" ] || fail 'topology recreated client-owned decoration'

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
[ -z "$(deco "$a")" ] && [ -z "$(deco "$b")" ] || fail 'topology teardown orphan'
if grep -E 'X11 error|semantic invariant failed|AddressSanitizer|runtime error:' "$tmp_dir/wm.log"; then
    fail 'WM diagnostics'
fi
echo 'PASS: Xvfb decoration monitor crossing, translation, add/remove, geometry and restore'
