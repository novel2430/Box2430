#!/bin/sh
set -eu
export DISPLAY=${BOX2430_TEST_DISPLAY:-:240}
export XCURSOR_THEME=Adwaita XCURSOR_SIZE=24
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture=./build/debug/x11-test-client
mutator=./build/debug/x11-property-mutator
cursor=./build/debug/x11-cursor-shape
hash=./build/debug/x11-window-hash
tmp_dir=$(mktemp -d)
pids= wm_pid=
cleanup() {
    result=$?
    for pid in $wm_pid $pids; do kill "$pid" 2>/dev/null || true; done
    if [ "$result" -ne 0 ]; then echo "Button logs retained at $tmp_dir" >&2
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
no_resource() { [ -z "$(deco "$1")" ]; }
geometry() {
    xwininfo -id "$1" | awk '/Absolute upper-left X:/ {x=$NF}
        /Absolute upper-left Y:/ {y=$NF} /Width:/ {w=$NF} /Height:/ {h=$NF}
        END {print x,y,w,h}'
}
geometry_is() { [ "$(geometry "$1")" = "$2" ]; }
assert_geometry() { wait_until geometry_is "$1" "$2" || fail "$3: $(geometry "$1")"; }
grab_free() { ./build/debug/decoration-test --pointer-grab; }
grab_held() { ! grab_free; }
spawn() {
    "$fixture" NORMAL "$1" "$2" 100 "$3" 140 >"$tmp_dir/$1.log" 2>&1 &
    spawned_pid=$! pids="$! $pids"
    wait_until has_name "$1" || fail 'fixture missing'
    spawned=$(named "$1")
    wait_until has_name "box2430-decoration-$(printf '0x%x' "$spawned")" || fail 'decoration missing'
}
start_wm() {
    "$box2430_bin" -c "$1" >"$tmp_dir/wm-$2.log" 2>&1 &
    wm_pid=$!
    wait_until has_name box2430-bar-0 || fail 'WM missing'
}
stop_wm() { kill "$wm_pid"; wait "$wm_pid"; wm_pid=; }
press_at() {
    xdotool mousemove --window "$1" "$2" 12 mousedown 1
    wait_until grab_held || fail 'button grab missing'
}
release() { xdotool mouseup 1; wait_until grab_free || fail 'release leaked grab'; }

Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
pids=$!
wait_until xdpyinfo >/dev/null 2>&1 || fail 'Xvfb missing'
start_wm tests/fixtures/config-decoration.toml builtin
spawn BtnA 100 220
a=$spawned da=$(deco "$spawned")
spawn BtnB 400 220
b=$spawned db=$(deco "$spawned")

# The default square buttons share the same regions used for painting/cursors.
xdotool windowactivate "$a"
xdotool mousemove --window "$da" 188 12
wait_until "$cursor" pointer || fail 'maximize cursor does not match selected theme'
normal_icon=$("$hash" "$da" 176 0 24 24)
xdotool mousemove --window "$da" 212 12
wait_until "$cursor" pointer || fail 'close cursor'
[ "$("$hash" "$da" 176 0 24 24)" = "$normal_icon" ] || fail 'hover changed visual'
xdotool mousemove --window "$da" 150 12
wait_until "$cursor" left_ptr || fail 'space did not restore title cursor'
# SPACE, not just title glyphs, is the old draggable surface.
press_at "$da" 150
xdotool mousemove_relative --sync 4 0
assert_geometry "$a" '104 100 220 140' 'space not draggable'
release
press_at "$da" 188
assert_geometry "$a" '104 100 220 140' 'button press maximized immediately'
[ "$("$hash" "$da" 176 0 24 24)" = "$normal_icon" ] || fail 'press changed visual'
release
assert_geometry "$a" '0 48 796 548' 'maximize button'
restore_icon=$("$hash" "$da" 752 0 24 24)
[ "$normal_icon" != "$restore_icon" ] || fail 'restore primitive missing'
press_at "$da" 764
release
assert_geometry "$a" '104 100 220 140' 'restore normal geometry'
# Keyboard state changes redraw the same authoritative visual.
xdotool key super+Up
assert_geometry "$a" '0 48 796 548' 'keyboard maximize'
[ "$("$hash" "$da" 752 0 24 24)" = "$restore_icon" ] || fail 'keyboard restore visual stale'
xdotool key super+Up
assert_geometry "$a" '104 100 220 140' 'keyboard restore'
[ "$("$hash" "$da" 176 0 24 24)" = "$normal_icon" ] || fail 'normal visual stale'

# Button movement never becomes drag, and release on a different part cancels.
press_at "$da" 212
xdotool mousemove --window "$da" 150 12
release
assert_geometry "$a" '104 100 220 140' 'close cancellation became drag'
press_at "$da" 188
xdotool mousemove --window "$da" 212 12
release
assert_geometry "$a" '104 100 220 140' 'cross-button release executed'
# A pair of button clicks toggles twice; no third title double-click toggle.
press_at "$da" 188; release
assert_geometry "$a" '0 48 796 548' 'first button of pair'
press_at "$da" 764; release
assert_geometry "$a" '104 100 220 140' 'button polluted title double click'

# Owner target and WM_DELETE_WINDOW (fixture exits cleanly, not killed connection).
xdotool windowactivate "$a"
press_at "$db" 188; release
assert_geometry "$b" '0 48 796 548' 'maximize targeted focused client instead of owner'
assert_geometry "$a" '104 100 220 140' 'maximize changed focused client'
press_at "$db" 764; release
assert_geometry "$b" '400 100 220 140' 'unfocused owner restore'
press_at "$db" 212
mapped "$b" || fail 'close happened on press'
[ "$(xdotool getwindowfocus)" = "$a" ] || fail 'button implicitly focused owner'
release
wait_until no_resource "$b" || fail 'close did not remove owner'
wait "$spawned_pid" || fail 'close bypassed WM_DELETE_WINDOW'
mapped "$a" || fail 'close killed focused client'

for transition in motif workspace monocle fullscreen; do
    xdotool windowactivate "$a"
    before=$(geometry "$a")
    press_at "$da" 212
    case $transition in
        motif) "$mutator" motif "$a" off ;;
        workspace) xdotool key super+2 ;;
        monocle) xdotool key super+m ;;
        fullscreen) xdotool key super+f ;;
    esac
    wait_until hidden "$da" || fail "$transition titlebar not hidden"
    wait_until grab_free || fail "$transition pressed button leaked grab"
    wait_until "$cursor" left_ptr || fail "$transition left button cursor"
    xdotool mouseup 1
    case $transition in
        motif) "$mutator" motif "$a" on ;;
        workspace) xdotool key super+1 ;;
        monocle) xdotool key super+m ;;
        fullscreen) xdotool key super+f ;;
    esac
    wait_until mapped "$da" || fail "$transition restore missing"
    assert_geometry "$a" "$before" "$transition pending button acted"
done
spawn BtnDestroy 400 220
victim=$spawned
press_at "$(deco "$victim")" 212
xdotool windowkill "$victim"
wait_until no_resource "$victim" || fail 'pressed owner orphan'
wait_until grab_free || fail 'pressed owner grab leak'
xdotool mouseup 1

# Narrow allocation clips later fixed buttons deterministically without X errors.
spawn BtnNarrow 400 10
narrow=$spawned dn=$(deco "$spawned")
xdotool mousemove --window "$dn" 7 12
wait_until "$cursor" pointer || fail 'narrow first button hit region'
xdotool windowkill "$narrow"
xdotool windowkill "$a"
spawn BtnShutdown 100 220
shutdown_owner=$spawned
press_at "$(deco "$shutdown_owner")" 212
stop_wm
wait_until grab_free || fail 'shutdown leaked button grab'
xdotool mouseup 1
mapped "$shutdown_owner" || fail 'shutdown button press closed original client'
xdotool windowkill "$shutdown_owner"

start_wm tests/fixtures/config-decoration-labels.toml labels
spawn BtnLabels 100 500
a=$spawned da=$(deco "$spawned")
cw=$(./build/debug/decoration-test --label-width CloseWindow monospace:size=18 3)
mw=$(./build/debug/decoration-test --label-width MAXIMIZE monospace:size=18 3)
[ "$cw" -gt 24 ] && [ "$mw" -gt 24 ] || fail 'text fixture must exceed square width'
xdotool mousemove --window "$da" "$((cw - 1))" 12
wait_until "$cursor" pointer || fail 'label width clipped to square'
label_icon=$("$hash" "$da" "$cw" 0 "$mw" 24)
press_at "$da" "$((cw + mw / 2))"; release
assert_geometry "$a" '0 48 796 548' 'reordered maximize text button'
[ "$("$hash" "$da" "$cw" 0 "$mw" 24)" != "$label_icon" ] || fail 'restore text stale'
press_at "$da" "$((cw + mw / 2))"; release
assert_geometry "$a" '100 100 500 140' 'label restore'
# click=none/double_click=none do not alter fixed button actions.
xdotool mousemove --window "$da" "$((cw + mw + 5))" 12
wait_until "$cursor" left_ptr || fail 'reordered spacer cursor'
press_at "$da" "$((cw - 1))"; release
wait_until no_resource "$a" || fail 'text close full-width hit region'
stop_wm

start_wm tests/fixtures/config-decoration-title-only.toml title
spawn BtnTitleOnly 100 220
a=$spawned da=$(deco "$spawned")
xdotool mousemove --window "$da" 212 12
wait_until "$cursor" left_ptr || fail 'removed close still has button cursor'
press_at "$da" 212; release
assert_geometry "$a" '100 100 220 140' 'removed button still acted'
press_at "$da" 188
stop_wm
wait_until grab_free || fail 'shutdown leaked input grab'
xdotool mouseup 1
if grep -E 'X11 error|semantic invariant failed|AddressSanitizer|runtime error:|invalid.*config' "$tmp_dir"/wm-*.log; then
    fail 'WM diagnostics'
fi
echo 'PASS: Xvfb decoration layout/buttons/labels/cursor/owner/cancellation'
