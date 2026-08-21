#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:173}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FOCUS_COMPAT_BIN:-./build/debug/x11-focus-compat-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= pair_pid=

cleanup() {
    for pid in "$pair_pid" "$wm_pid" "$xvfb_pid"; do
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
wait_active() {
    wanted=$(printf '0x%x' "$1")
    wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $wanted"
}
wait_input_focus() {
    wanted=$(printf '%d' "$1")
    wait_for "test \"\$(DISPLAY=$display xdotool getwindowfocus)\" = \"$wanted\""
}
button_count() {
    DISPLAY=$display xprop -id "$1" _BOX2430_TEST_BUTTON_COUNT |
        awk '{print $NF}'
}
wait_button_count() {
    attempts=0
    while [ "$(button_count "$1")" != "$2" ]; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 200 ]; then return 1; fi
        sleep 0.02
    done
}
start_pair() {
    ids_file=$1
    DISPLAY=$display "$fixture_bin" pair >"$ids_file" 2>"$tmp_dir/pair.log" &
    pair_pid=$!
    wait_for "test -s $ids_file" || fail "focus compatibility fixture did not start"
    read -r a b <"$ids_file"
    wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $(printf '0x%x' "$b")" || fail "focus clients were not managed"
}
stop_pair() {
    kill "$pair_pid" 2>/dev/null || true
    wait "$pair_pid" 2>/dev/null || true
    pair_pid=
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-focus-compat.toml >"$tmp_dir/default-wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "default WM did not start"
start_pair "$tmp_dir/default.ids"

# The last mapped client starts focused. The focused window has no catch-all
# click-focus grab, while the unfocused client still does.
wait_active "$b" || fail "second client did not start focused"
DISPLAY=$display "$fixture_bin" probe-grab "$b" || fail "focused client retained catch-all passive grab"
if DISPLAY=$display "$fixture_bin" probe-grab "$a"; then
    fail "unfocused client lacks click-focus passive grab"
fi

# The first click focuses A and is replayed to the application. Once focused,
# a second ordinary click is delivered directly and its grab becomes available.
DISPLAY=$display xdotool mousemove --window "$a" 20 20 click 1
wait_active "$a" || fail "first click did not focus unfocused client"
wait_button_count "$a" 1 || fail "focus click was not replayed to application"
DISPLAY=$display "$fixture_bin" probe-grab "$a" || fail "focused client catch-all grab was not removed"
if DISPLAY=$display "$fixture_bin" probe-grab "$b"; then
    fail "newly unfocused client lacks catch-all passive grab"
fi
DISPLAY=$display xdotool click 1
wait_button_count "$a" 2 || fail "focused client did not receive ordinary click directly"

# A broken client cannot permanently steal X input focus away from Box2430's
# semantic focused client.
DISPLAY=$display "$fixture_bin" focus "$b"
wait_input_focus "$a" || fail "FocusIn conflict did not restore semantic focus"
wait_active "$a" || fail "FocusIn correction changed semantic active client"

# Root FocusIn uses the same semantic-focus recovery path as a client conflict.
DISPLAY=$display "$fixture_bin" focus-root
wait_input_focus "$a" || fail "root FocusIn did not restore semantic focus"
wait_active "$a" || fail "root FocusIn correction changed semantic active client"

# Default _NET_ACTIVE_WINDOW policy marks B urgent without moving focus.
DISPLAY=$display "$fixture_bin" activate "$b"
wait_active "$a" || fail "default _NET_ACTIVE_WINDOW request stole focus"
wait_for "DISPLAY=$display xprop -id $b WM_HINTS | grep -qi urgency" || fail "default _NET_ACTIVE_WINDOW request did not mark client urgent"

stop_pair
kill "$wm_pid"; wait "$wm_pid"; wm_pid=

# Explicit focus policy preserves the previous practical behavior.
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-active-focus.toml >"$tmp_dir/focus-wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "focus-policy WM did not start"
start_pair "$tmp_dir/focus.ids"
DISPLAY=$display xdotool mousemove --window "$a" 20 20 click 1
wait_active "$a" || fail "could not establish focus-policy baseline"
DISPLAY=$display "$fixture_bin" activate "$b"
wait_active "$b" || fail "configured _NET_ACTIVE_WINDOW focus policy was ignored"
wait_input_focus "$b" || fail "configured active request did not set X input focus"

stop_pair
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/default-wm.log" "$tmp_dir/focus-wm.log"; then
    sed -n '1,180p' "$tmp_dir/default-wm.log" "$tmp_dir/focus-wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb FocusIn/click-grab/replay/_NET_ACTIVE_WINDOW scenario"
