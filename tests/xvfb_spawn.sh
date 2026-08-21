#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:145}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
sigchld_bin=${BOX2430_SIGCHLD_BIN:-./build/debug/x11-sigchld-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= child_pid= xterm_pids=

cleanup() {
    for pid in $xterm_pids "$child_pid" "$wm_pid" "$xvfb_pid"; do
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

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
PATH="$(dirname "$sigchld_bin"):$PATH" DISPLAY=$display \
    "$box2430_bin" -c tests/fixtures/config-spawn.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"
sleep 0.1

DISPLAY=$display xdotool keydown super key s keyup super
if ! wait_for "DISPLAY=$display xdotool search --name SpawnSigchldDefault >/dev/null 2>&1"; then
    sed -n '1,120p' "$tmp_dir/wm.log" >&2
    fail "spawn child did not inherit SIGCHLD=SIG_DFL"
fi
window=$(DISPLAY=$display xdotool search --name SpawnSigchldDefault | head -n 1)
child_pid=$(DISPLAY=$display xprop -id "$window" _NET_WM_PID 2>/dev/null |
    awk '{print $NF}')

DISPLAY=$display xdotool keydown super key e keyup super
wait_for "grep -q 'box2430: spawn box2430-command-that-does-not-exist:' '$tmp_dir/wm.log'" ||
    fail "spawn exec failure did not report program name and error"

# Repeated key-bound spawns must create distinct managed clients even though
# center placement makes identical xterms completely overlap in FREE mode.
DISPLAY=$display xdotool keydown super key t keyup super
if ! wait_for "test \"\$(DISPLAY=$display xdotool search --name SpawnedXterm 2>/dev/null | wc -l)\" = 1"; then
    sed -n '1,160p' "$tmp_dir/wm.log" >&2
    fail "first key-bound xterm spawn missing"
fi
DISPLAY=$display xdotool keydown super key t keyup super
wait_for "test \"\$(DISPLAY=$display xdotool search --name SpawnedXterm 2>/dev/null | wc -l)\" = 2" ||
    fail "second key-bound xterm spawn missing"
xterm_pids=$(DISPLAY=$display xprop -root _NET_CLIENT_LIST | sed 's/.*=//' |
    tr ',' '\n' | while read -r id; do
        id=$(printf '%s' "$id" | tr -d ' ')
        [ -n "$id" ] || continue
        DISPLAY=$display xprop -id "$id" WM_CLASS 2>/dev/null | grep -q 'XTerm' || continue
        DISPLAY=$display xprop -id "$id" _NET_WM_PID 2>/dev/null | awk '{print $NF}'
    done)

top=$(DISPLAY=$display xdotool getactivewindow)
DISPLAY=$display xdotool key super+x
wait_for "! DISPLAY=$display xwininfo -id $top >/dev/null 2>&1" ||
    fail "closing top spawned xterm failed"
wait_for "test \"\$(DISPLAY=$display xdotool search --name SpawnedXterm 2>/dev/null | wc -l)\" = 1" ||
    fail "closing one overlapping xterm removed the wrong number of clients"
DISPLAY=$display xdotool keydown super key t keyup super
wait_for "test \"\$(DISPLAY=$display xdotool search --name SpawnedXterm 2>/dev/null | wc -l)\" = 2" ||
    fail "xterm spawn after close missing"

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
echo "PASS: Xvfb spawn SIGCHLD/error-reporting scenario"
