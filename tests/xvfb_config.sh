#!/bin/sh
set -eu

display=${MICROBOX_TEST_DISPLAY:-:127}
microbox_bin=${MICROBOX_BIN:-./build/debug/microbox}
tmp_dir=$(mktemp -d)
xvfb_pid=
wm_pid=
client_pid=

cleanup() {
    if [ -n "$client_pid" ]; then kill "$client_pid" 2>/dev/null || true; fi
    if [ -n "$wm_pid" ]; then kill "$wm_pid" 2>/dev/null || true; fi
    if [ -n "$xvfb_pid" ]; then kill "$xvfb_pid" 2>/dev/null || true; fi
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
wait_border() {
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $1 | awk '/Border width:/ {print \$3}')\" = $2"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-valid.toml >"$tmp_dir/valid.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "configured WM did not start"
DISPLAY=$display xterm -title ConfigValid -geometry 30x8+17+23 >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name ConfigValid >/dev/null 2>&1" || fail "configured client did not map"
window=$(DISPLAY=$display xdotool search --name ConfigValid | head -n 1)

wait_border "$window" 7 || fail "configured border width not applied"
[ "$(field "$window" 'Absolute upper-left X:')" = 17 ] || fail "client placement x not honored"
[ "$(field "$window" 'Absolute upper-left Y:')" = 23 ] || fail "client placement y not honored"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -q 0x0 || fail "focus_on_map=false was ignored"
DISPLAY=$display xdotool mousemove 30 35
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$window")" || fail "sloppy focus was not applied"

DISPLAY=$display xdotool key super+2
sleep 0.05
DISPLAY=$display xwininfo -id "$window" | grep -q 'Map State: IsViewable' || fail "inherit_defaults=false retained workspace binding"
DISPLAY=$display xdotool keydown super mousemove --window "$window" 20 20 \
    mousedown 1 mousemove_relative --sync 50 20 mouseup 1 keyup super
[ "$(field "$window" 'Absolute upper-left X:')" = 17 ] || fail "inherit_defaults=false retained mouse binding"

DISPLAY=$display xdotool key super+Left
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Width:/ {print \$2; exit}')\" = 186" || fail "configured snap ratio not applied"
DISPLAY=$display xdotool windowstate --add FULLSCREEN "$window"
sleep 0.05
if DISPLAY=$display xprop -id "$window" _NET_WM_STATE | grep -q _NET_WM_STATE_FULLSCREEN; then
    fail "deny fullscreen policy acknowledged client request"
fi

kill "$client_pid"
client_pid=
kill "$wm_pid"
wait "$wm_pid"
wm_pid=

DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-invalid-atomic.toml >"$tmp_dir/invalid.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "fallback WM did not start"
DISPLAY=$display xterm -title ConfigFallback -geometry 30x8+17+23 >"$tmp_dir/fallback.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name ConfigFallback >/dev/null 2>&1" || fail "fallback client did not map"
window=$(DISPLAY=$display xdotool search --name ConfigFallback | head -n 1)
wait_border "$window" 2 || fail "invalid config was partially applied"
[ "$(field "$window" 'Absolute upper-left X:')" != 17 ] || fail "invalid placement was partially applied"
DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi "$(printf '0x%x' "$window")" || fail "safe focus default did not apply"
grep -q "unknown config option focus.focus_on_mpa" "$tmp_dir/invalid.log" || fail "unknown option diagnostic missing"
grep -q "discarding invalid config" "$tmp_dir/invalid.log" || fail "atomic fallback diagnostic missing"

kill "$client_pid"
client_pid=
kill "$wm_pid"
wait "$wm_pid"
wm_pid=

DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-invalid-command.toml >"$tmp_dir/command.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "command-fallback WM did not start"
DISPLAY=$display xterm -title CommandFallback -geometry 30x8+17+23 >"$tmp_dir/command-client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name CommandFallback >/dev/null 2>&1" || fail "command-fallback client did not map"
window=$(DISPLAY=$display xdotool search --name CommandFallback | head -n 1)
wait_border "$window" 2 || fail "invalid binding command partially applied config"
grep -q "invalid command for binding Super+x" "$tmp_dir/command.log" || fail "binding command diagnostic missing"

kill "$client_pid"
client_pid=
kill "$wm_pid"
wait "$wm_pid"
wm_pid=

DISPLAY=$display "$microbox_bin" -c tests/fixtures/config-invalid-context.toml >"$tmp_dir/context.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "context-fallback WM did not start"
DISPLAY=$display xterm -title ContextFallback -geometry 30x8+17+23 >"$tmp_dir/context-client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name ContextFallback >/dev/null 2>&1" || fail "context-fallback client did not map"
window=$(DISPLAY=$display xdotool search --name ContextFallback | head -n 1)
wait_border "$window" 2 || fail "invalid command context partially applied config"
grep -q "invalid command for binding Super+x" "$tmp_dir/context.log" || fail "command context diagnostic missing"

kill "$client_pid"
client_pid=
kill "$wm_pid"
wait "$wm_pid"
wm_pid=

DISPLAY=$display "$microbox_bin" -c config.example.toml >"$tmp_dir/example.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED 2>/dev/null | grep -q _NET_ACTIVE_WINDOW" ||
    fail "full example config did not start"
if grep -q 'discarding invalid config' "$tmp_dir/example.log"; then
    sed -n '1,120p' "$tmp_dir/example.log" >&2
    fail "full example config failed validation"
fi
kill "$wm_pid"
wait "$wm_pid"
wm_pid=
echo "PASS: Xvfb strict atomic config scenario"
