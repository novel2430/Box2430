#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:139}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
urgency_bin=${BOX2430_URGENCY_BIN:-./build/debug/x11-set-urgency}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= one_pid= two_pid= overlap_pid=

cleanup() {
    for pid in "$overlap_pid" "$two_pid" "$one_pid" "$wm_pid" "$xvfb_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; fi
    done
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }
image_convert() {
    if command -v magick >/dev/null 2>&1; then
        magick "$@"
    else
        convert "$@"
    fi
}
wait_for() {
    attempts=0
    while ! sh -c "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 200 ]; then return 1; fi
        sleep 0.02
    done
}
wait_managed() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $wanted"
}
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
pixel_at() {
    image_convert "$1" -format "%[pixel:p{$2,$3}]" info:
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-sloppy.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

DISPLAY=$display xterm -title SloppyOne -geometry 30x8+20+20 >"$tmp_dir/one.log" 2>&1 & one_pid=$!
DISPLAY=$display xterm -title SloppyTwo -geometry 30x8+350+20 >"$tmp_dir/two.log" 2>&1 & two_pid=$!
wait_for "DISPLAY=$display xdotool search --name SloppyOne >/dev/null 2>&1" || fail "first client missing"
wait_for "DISPLAY=$display xdotool search --name SloppyTwo >/dev/null 2>&1" || fail "second client missing"
one=$(DISPLAY=$display xdotool search --name SloppyOne | head -n 1)
two=$(DISPLAY=$display xdotool search --name SloppyTwo | head -n 1)
wait_managed "$one" || fail "first client was not managed"
wait_managed "$two" || fail "second client was not managed"

DISPLAY=$display xdotool mousemove --window "$one" 20 20
wait_active "$one" || fail "sloppy enter did not focus first client"

# Raising a newly mapped window under a stationary pointer creates crossing
# events. enforce_stacking must discard those stale enters rather than treating
# them as user pointer motion.
DISPLAY=$display xterm -title SloppyOverlap -geometry 30x8+20+20 >"$tmp_dir/overlap.log" 2>&1 & overlap_pid=$!
wait_for "DISPLAY=$display xdotool search --name SloppyOverlap >/dev/null 2>&1" || fail "overlap client missing"
wait_active "$one" || fail "restack stale EnterNotify changed sloppy focus"
kill "$overlap_pid" 2>/dev/null || true; overlap_pid=
wait_active "$one" || fail "overlap withdrawal stale EnterNotify changed sloppy focus"

DISPLAY=$display xdotool mousemove 790 590
wait_active "$one" || fail "root enter incorrectly cleared sloppy focus"
DISPLAY=$display xdotool mousemove --window "$two" 20 20
wait_active "$two" || fail "sloppy enter did not focus second client"

one_x=$(DISPLAY=$display xwininfo -id "$one" | awk '/Absolute upper-left X:/ {print $4}')
one_y=$(DISPLAY=$display xwininfo -id "$one" | awk '/Absolute upper-left Y:/ {print $4}')
DISPLAY=$display xwd -silent -root -out "$tmp_dir/before.xwd"
before=$(pixel_at "$tmp_dir/before.xwd" "$one_x" "$one_y")
DISPLAY=$display "$urgency_bin" "$one" input
DISPLAY=$display "$urgency_bin" "$one" 1
wait_for "DISPLAY=$display xprop -id $one WM_HINTS | grep -qi urgency" || fail "urgency hint was not set"
DISPLAY=$display xwd -silent -root -out "$tmp_dir/urgent.xwd"
urgent=$(pixel_at "$tmp_dir/urgent.xwd" "$one_x" "$one_y")
[ "$urgent" != "$before" ] || fail "urgency did not propagate to unfocused border"

DISPLAY=$display xdotool mousemove --window "$one" 20 20
wait_active "$one" || fail "urgent client did not focus"
wait_for "! DISPLAY=$display xprop -id $one WM_HINTS | grep -qi urgency" || fail "focus did not clear XUrgencyHint"
DISPLAY=$display xprop -id "$one" WM_HINTS | grep -qi 'accepts input.*True' || fail "clearing urgency overwrote another WM_HINTS value"
DISPLAY=$display xwd -silent -root -out "$tmp_dir/focused.xwd"
focused=$(pixel_at "$tmp_dir/focused.xwd" "$one_x" "$one_y")
[ "$focused" != "$urgent" ] || fail "focusing urgent client did not clear urgent presentation"

DISPLAY=$display xdotool mousemove --window "$two" 20 20
wait_active "$two" || fail "second client did not refocus"
DISPLAY=$display "$urgency_bin" "$one" input
sleep 0.05
DISPLAY=$display xprop -id "$one" WM_HINTS | grep -qi urgency && fail "unrelated WM_HINTS update revived urgency"
DISPLAY=$display xwd -silent -root -out "$tmp_dir/after-hints.xwd"
after_hints=$(pixel_at "$tmp_dir/after-hints.xwd" "$one_x" "$one_y")
[ "$after_hints" = "$before" ] || fail "unrelated WM_HINTS update revived urgent border"

kill "$two_pid" "$one_pid" 2>/dev/null || true; two_pid= one_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb sloppy focus/root retention/urgency scenario"
