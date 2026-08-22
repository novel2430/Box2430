#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:144}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid=

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

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

run_invalid() {
    name=$1
    expected=$2
    log="$tmp_dir/$name.log"
    DISPLAY=$display "$box2430_bin" -c "tests/fixtures/$name.toml" >"$log" 2>&1 &
    wm_pid=$!
    wait_for "grep -q 'discarding invalid config' '$log'" || {
        sed -n '1,120p' "$log" >&2
        fail "$name was not rejected"
    }
    grep -Fq "$expected" "$log" || {
        sed -n '1,120p' "$log" >&2
        fail "$name diagnostic mismatch"
    }
    kill "$wm_pid"
    # The rejection diagnostic is emitted during config_load(), before
    # wm_run() installs Box2430's SIGTERM handler.  Treat either a graceful
    # WM shutdown or direct SIGTERM termination as expected cleanup here.
    wait "$wm_pid" 2>/dev/null || true
    wm_pid=
}

run_invalid config-v2-invalid-bar-key \
    "unknown config option appearance.bar.unknown"
run_invalid config-v2-invalid-widget-name \
    "unknown bar widget battery"
run_invalid config-v2-invalid-duplicate-widget \
    "duplicate bar widget title"
run_invalid config-v2-invalid-tab-state \
    "unknown config option appearance.tabs.hover"
run_invalid config-v2-invalid-workspace-state \
    "unknown config option appearance.bar.widgets.workspaces.selected"
run_invalid config-v2-invalid-mode-state \
    "unknown config option appearance.bar.widgets.mode.tiled"
run_invalid config-v2-invalid-tab-position \
    "invalid value for config option appearance.tabs.position"
run_invalid config-v2-invalid-source \
    "invalid value for config option appearance.tabs.source"
run_invalid config-v2-invalid-font-style \
    "invalid value for config option appearance.tabs.active.font_style"
run_invalid config-v2-invalid-color \
    "appearance.bar.widgets.title.fg must be #RRGGBB"
run_invalid config-v2-invalid-format-zero \
    "config option appearance.bar.widgets.status.format must contain exactly one %s"
run_invalid config-v2-invalid-format-many \
    "config option appearance.tabs.format must contain exactly one %s"
run_invalid config-v2-invalid-old-tabs \
    "unknown config option appearance.tabs.active_fg"
run_invalid config-v2-invalid-malformed-state \
    "config option appearance.tabs.active must be a table"

# A rejected V2 appearance candidate must not leak otherwise-valid values.
log="$tmp_dir/atomic.log"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-v2-invalid-atomic.toml >"$log" 2>&1 &
wm_pid=$!
wait_for "grep -q 'discarding invalid config' '$log'" || fail "atomic V2 config was not rejected"
DISPLAY=$display xterm -title V2AtomicFallback >"$tmp_dir/client.log" 2>&1 &
client_pid=$!
wait_for "DISPLAY=$display xdotool search --name V2AtomicFallback >/dev/null 2>&1" ||
    fail "atomic fallback client did not map"
window=$(DISPLAY=$display xdotool search --name V2AtomicFallback | head -n 1)
wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3; exit}')\" = 2" ||
    fail "rejected V2 UI config partially applied border width"
grep -Fq "invalid value for config option appearance.bar.widgets.title.source" "$log" ||
    fail "atomic V2 source diagnostic missing"

kill "$client_pid"; client_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
echo "PASS: Xvfb V2 native-UI config validation scenario"
