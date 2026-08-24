#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:167}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
compat_client=${BOX2430_BSPWM_COMPAT_CLIENT_BIN:-./build/debug/bspwm-compat-client}
monitor_bin=${BOX2430_RANDR_MONITOR_BIN:-./build/debug/x11-randr-monitor}
urgency_bin=${BOX2430_URGENCY_BIN:-./build/debug/x11-set-urgency}
tmp_dir=$(mktemp -d)
socket_path=$tmp_dir/bspwm.sock
xvfb_pid= left_pid= right_pid= renamed_pid= wm_pid= subscriber_pid= left_one_pid= left_two_pid= right_pid_client= listener_pid=

cleanup() {
    for pid in "$listener_pid" "$right_pid_client" "$left_two_pid" "$left_one_pid" \
        "$subscriber_pid" "$wm_pid" "$renamed_pid" "$right_pid" "$left_pid" "$xvfb_pid"; do
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
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
wait_latest() {
    pattern=$1
    wait_for "test -s $tmp_dir/reports && tail -n 1 $tmp_dir/reports | grep -q '$pattern'"
}
start_wm() {
    config=$1 log=$2
    BSPWM_SOCKET=$socket_path DISPLAY=$display "$box2430_bin" -c "$config" \
        >"$log" 2>&1 &
    wm_pid=$!
    wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED 2>/dev/null | grep -q _NET_ACTIVE_WINDOW" ||
        fail "WM did not start for $config"
    # The EWMH property is initialized before the final startup scan. Give
    # instrumented builds time to enter wm_run() before teardown signals.
    sleep 0.1
}
stop_wm() {
    kill "$wm_pid"
    wait "$wm_pid"
    wm_pid=
    wait_for "! test -e $socket_path" || fail "owned compatibility socket remained"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$monitor_bin" set left 0 0 400 600 screen hold \
    >"$tmp_dir/left-monitor.log" 2>&1 &
left_pid=$!
wait_for "DISPLAY=$display xrandr --listmonitors 2>/dev/null | grep -q 'left'" ||
    fail "left logical monitor missing"
DISPLAY=$display "$monitor_bin" set right 400 0 400 600 none hold \
    >"$tmp_dir/right-monitor.log" 2>&1 &
right_pid=$!
wait_for "test \"\$(DISPLAY=$display xrandr --listmonitors | awk '/Monitors:/ {print \$2}')\" = 2" ||
    fail "right logical monitor missing"

# Disabled and invalid configurations must not create the optional runtime.
start_wm tests/fixtures/config-bspwm-compat-disabled.toml "$tmp_dir/disabled.log"
[ ! -e "$socket_path" ] || fail "enabled=false created a socket"
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
start_wm tests/fixtures/config-valid.toml "$tmp_dir/default.log"
[ ! -e "$socket_path" ] || fail "missing bspwm_compat table enabled the runtime"
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
for fixture in config-bspwm-compat-invalid-key.toml config-bspwm-compat-invalid-type.toml; do
    start_wm "tests/fixtures/$fixture" "$tmp_dir/$fixture.log"
    [ ! -e "$socket_path" ] || fail "$fixture partially enabled compatibility"
    grep -q 'discarding invalid config' "$tmp_dir/$fixture.log" ||
        fail "$fixture did not preserve atomic config rejection"
    kill "$wm_pid"; wait "$wm_pid"; wm_pid=
done
grep -q 'unknown config option bspwm_compat.socket' \
    "$tmp_dir/config-bspwm-compat-invalid-key.toml.log" ||
    fail "unknown compatibility key diagnostic missing"
grep -q 'config option bspwm_compat.enabled must be boolean' \
    "$tmp_dir/config-bspwm-compat-invalid-type.toml.log" ||
    fail "compatibility type diagnostic missing"

# The listener exists before autostart, and every compatibility fd is CLOEXEC.
export BOX2430_CLOEXEC_RESULT=$tmp_dir/cloexec
BSPWM_SOCKET=$socket_path DISPLAY=$display "$box2430_bin" \
    -c tests/fixtures/config-bspwm-compat.toml \
    --autostart tests/check_bspwm_compat_cloexec.sh >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "test -S $socket_path" || fail "BSPWM_SOCKET listener was not created"
wait_for "test -s $tmp_dir/cloexec" || fail "autostart CLOEXEC probe did not run"
[ "$(cat "$tmp_dir/cloexec")" = clean ] || fail "autostart inherited a compatibility fd"

"$compat_client" subscribe "$socket_path" 0 >"$tmp_dir/reports" &
subscriber_pid=$!
wait_latest '^WMleft:F1:f2:f3:f4:LT:mright:F1:f2:f3:f4:LT$' ||
    fail "initial multi-monitor report is wrong"
rm "$tmp_dir/cloexec"
DISPLAY=$display xdotool key super+x
wait_for "test -s $tmp_dir/cloexec" || fail "spawned CLOEXEC probe did not run"
[ "$(cat "$tmp_dir/cloexec")" = clean ] ||
    fail "spawned process inherited listener/client compatibility fds"
initial_lines=$(wc -l <"$tmp_dir/reports")
DISPLAY=$display xprop -root -f BOX2430_IRRELEVANT 8s -set BOX2430_IRRELEVANT unchanged
sleep 0.1
[ "$(wc -l <"$tmp_dir/reports")" = "$initial_lines" ] ||
    fail "unchanged authority emitted a duplicate report"

# monitor -f selects through workspace activation and never warps the pointer.
DISPLAY=$display xdotool mousemove --sync 100 100
pointer_before=$(DISPLAY=$display xdotool getmouselocation --shell | tr '\n' ' ')
"$compat_client" command "$socket_path" monitor -f right
wait_latest '^Wmleft:F1:f2:f3:f4:LT:Mright:F1:f2:f3:f4:LT$' ||
    fail "monitor focus did not select right"
pointer_after=$(DISPLAY=$display xdotool getmouselocation --shell | tr '\n' ' ')
[ "$pointer_after" = "$pointer_before" ] || fail "monitor focus warped the pointer"

DISPLAY=$display xterm -title CompatRight -geometry 20x6+500+80 \
    >"$tmp_dir/right-client.log" 2>&1 &
right_pid_client=$!
wait_for "DISPLAY=$display xdotool search --name '^CompatRight$' >/dev/null 2>&1" ||
    fail "right client did not map"
right_window=$(DISPLAY=$display xdotool search --name '^CompatRight$' | head -n 1)
wait_latest 'Mright:O1:f2:f3:f4:LT$' || fail "right occupied state missing"

"$compat_client" command "$socket_path" monitor -f left
wait_latest '^WMleft:F1:f2:f3:f4:LT:mright:O1:f2:f3:f4:LT$' ||
    fail "monitor focus did not return left"
DISPLAY=$display xterm -title CompatLeftOne -geometry 20x6+50+80 \
    >"$tmp_dir/left-one.log" 2>&1 &
left_one_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^CompatLeftOne$' >/dev/null 2>&1" ||
    fail "left workspace-1 client did not map"
left_one=$(DISPLAY=$display xdotool search --name '^CompatLeftOne$' | head -n 1)
wait_latest '^WMleft:O1:f2:f3:f4:LT:mright:O1:f2:f3:f4:LT$' ||
    fail "left occupied state missing"

# Absolute click target, numeric workspace names, and local wrap traversal.
"$compat_client" command "$socket_path" desktop -f left:^2
wait_latest '^WMleft:o1:F2:f3:f4:LT:mright:O1:f2:f3:f4:LT$' ||
    fail "absolute desktop focus failed"
wait_for "DISPLAY=$display xwininfo -id $left_one | grep -q 'Map State: IsUnMapped'" ||
    fail "absolute desktop focus did not use workspace transition"
DISPLAY=$display xterm -title CompatLeftTwo -geometry 20x6+50+180 \
    >"$tmp_dir/left-two.log" 2>&1 &
left_two_pid=$!
wait_for "DISPLAY=$display xdotool search --name '^CompatLeftTwo$' >/dev/null 2>&1" ||
    fail "left workspace-2 client did not map"
left_two=$(DISPLAY=$display xdotool search --name '^CompatLeftTwo$' | head -n 1)
wait_latest '^WMleft:o1:O2:f3:f4:LT:mright:O1:f2:f3:f4:LT$' ||
    fail "workspace-2 occupied report missing"
"$compat_client" command "$socket_path" desktop -f next.local
wait_latest '^WMleft:o1:o2:F3:f4:LT:' || fail "next.local did not advance"
"$compat_client" command "$socket_path" desktop -f prev.local
wait_latest '^WMleft:o1:O2:f3:f4:LT:' || fail "prev.local did not return"
"$compat_client" command "$socket_path" desktop -f next.occupied.local
wait_latest '^WMleft:O1:o2:f3:f4:LT:' || fail "occupied local cycle did not wrap"
"$compat_client" command "$socket_path" desktop -f prev.occupied.local
wait_latest '^WMleft:o1:O2:f3:f4:LT:' || fail "occupied local reverse failed"

# Non-local traversal is monitor-major and occupied traversal skips empties.
"$compat_client" command "$socket_path" desktop -f next.occupied
wait_latest '^Wmleft:o1:O2:f3:f4:LT:Mright:O1:f2:f3:f4:LT$' ||
    fail "global occupied traversal did not reach right monitor"
"$compat_client" command "$socket_path" desktop -f next
wait_latest '^Wmleft:o1:O2:f3:f4:LT:Mright:o1:F2:f3:f4:LT$' ||
    fail "global next did not use monitor-major order"
"$compat_client" command "$socket_path" desktop -f prev
wait_latest '^Wmleft:o1:O2:f3:f4:LT:Mright:O1:f2:f3:f4:LT$' ||
    fail "global prev did not return"
"$compat_client" command "$socket_path" desktop -f prev.occupied
wait_latest '^WMleft:o1:O2:f3:f4:LT:mright:O1:f2:f3:f4:LT$' ||
    fail "global occupied reverse did not reach left monitor"
"$compat_client" command "$socket_path" monitor -f right
wait_latest '^Wmleft:o1:O2:f3:f4:LT:Mright:O1:f2:f3:f4:LT$' ||
    fail "right monitor did not reselect"
wait_for "DISPLAY=$display xprop -root _NET_ACTIVE_WINDOW | grep -qi $(printf '0x%x' "$right_window")" ||
    fail "monitor focus did not resolve the active workspace's client focus"
only_occupied=$(wc -l <"$tmp_dir/reports")
"$compat_client" command "$socket_path" desktop -f next.occupied.local
sleep 0.1
[ "$(wc -l <"$tmp_dir/reports")" = "$only_occupied" ] ||
    fail "occupied local cycle changed state without an eligible alternative"
"$compat_client" command "$socket_path" monitor -f left
wait_latest '^WMleft:o1:O2:f3:f4:LT:mright:O1:f2:f3:f4:LT$' ||
    fail "left monitor did not reselect"

# Urgency and layout are projected from existing authority, with no T/G tags.
DISPLAY=$display "$urgency_bin" "$right_window" 1
wait_latest 'mright:U1:f2:f3:f4:LT$' || fail "urgent projection missing"
DISPLAY=$display xdotool key super+m
wait_latest '^WMleft:o1:O2:f3:f4:LM:' || fail "MONOCLE did not project as LM"
tail -n 1 "$tmp_dir/reports" | grep -q ':T' && fail "fabricated node state present"
tail -n 1 "$tmp_dir/reports" | grep -q ':G' && fail "fabricated node flags present"
DISPLAY=$display xdotool key super+m
wait_latest '^WMleft:o1:O2:f3:f4:LT:' || fail "FREE did not project as LT"

# Invalid and oversized input do not mutate state; connection pressure is bounded.
before_invalid=$(tail -n 1 "$tmp_dir/reports")
"$compat_client" command "$socket_path" desktop -f left:^0
"$compat_client" command "$socket_path" desktop -f left:^999999999999999999999
"$compat_client" command "$socket_path" desktop -f left:^2x
"$compat_client" command "$socket_path" desktop -f missing:^1
"$compat_client" command "$socket_path" desktop -f next.unknown
"$compat_client" oversized "$socket_path"
sleep 0.1
[ "$(tail -n 1 "$tmp_dir/reports")" = "$before_invalid" ] ||
    fail "invalid compatibility input changed authority"
"$compat_client" exhaust "$socket_path" 40 >"$tmp_dir/exhausted" ||
    fail "connection limit did not reject excess clients"
kill -0 "$wm_pid" || fail "connection pressure disturbed the WM"

# Removing the final member publishes occupied -> empty for each workspace.
kill "$left_two_pid"; wait "$left_two_pid" 2>/dev/null || true; left_two_pid=
wait_latest '^WMleft:o1:F2:f3:f4:LT:mright:U1:f2:f3:f4:LT$' ||
    fail "removing workspace-2's last client did not publish empty state"
"$compat_client" command "$socket_path" desktop -f left:^1
wait_latest '^WMleft:O1:f2:f3:f4:LT:mright:U1:f2:f3:f4:LT$' ||
    fail "workspace-1 did not reactivate"
kill "$left_one_pid"; wait "$left_one_pid" 2>/dev/null || true; left_one_pid=
wait_latest '^WMleft:F1:f2:f3:f4:LT:mright:U1:f2:f3:f4:LT$' ||
    fail "removing workspace-1's last client did not publish empty state"

# Accepted RandR name metadata is report projection, not Monitor authority.
DISPLAY=$display "$monitor_bin" rename right renamed 400 0 400 600 none hold \
    >"$tmp_dir/renamed-monitor.log" 2>&1 &
renamed_pid=$!
wait_for "DISPLAY=$display xrandr --listmonitors 2>/dev/null | grep -q 'renamed'" ||
    fail "logical monitor rename did not complete"
DISPLAY=$display "$monitor_bin" notify-root
wait_latest '^WMleft:F1:f2:f3:f4:LT:mrenamed:U1:f2:f3:f4:LT$' ||
    fail "accepted RandR monitor-name change was not published"
"$compat_client" command "$socket_path" monitor -f right
sleep 0.05
tail -n 1 "$tmp_dir/reports" | grep -q '^WMleft:' ||
    fail "stale RandR monitor name still resolved"
"$compat_client" command "$socket_path" monitor -f renamed
wait_latest '^Wmleft:F1:f2:f3:f4:LT:Mrenamed:O1:f2:f3:f4:LT$' ||
    fail "renamed RandR monitor did not resolve"
"$compat_client" command "$socket_path" monitor -f left
wait_latest '^WMleft:F1:f2:f3:f4:LT:mrenamed:O1:f2:f3:f4:LT$' ||
    fail "left monitor did not reselect after metadata change"

# Subscriber EOF/SIGPIPE affects only that connection; later commands still work.
kill "$subscriber_pid"; wait "$subscriber_pid" 2>/dev/null || true; subscriber_pid=
"$compat_client" command "$socket_path" desktop -f left:^1
sleep 0.1
kill -0 "$wm_pid" || fail "subscriber disconnect terminated the WM"
"$compat_client" subscribe "$socket_path" 1 >"$tmp_dir/final-report"
grep -q '^WMleft:F1:f2:f3:f4:LT:mrenamed:O1:f2:f3:f4:LT$' \
    "$tmp_dir/final-report" || fail "fresh subscriber did not receive current report"

kill "$right_pid_client"; wait "$right_pid_client" 2>/dev/null || true; right_pid_client=
stop_wm

# Existing path handling is conservative and compatibility failures are non-fatal.
: >"$socket_path"
start_wm tests/fixtures/config-bspwm-compat.toml "$tmp_dir/non-socket.log"
[ -f "$socket_path" ] || fail "unrelated non-socket path was removed"
grep -q 'exists and is not a Unix socket' "$tmp_dir/non-socket.log" ||
    fail "non-socket conflict diagnostic missing"
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
rm "$socket_path"

"$compat_client" stale "$socket_path"
start_wm tests/fixtures/config-bspwm-compat.toml "$tmp_dir/stale.log"
[ -S "$socket_path" ] || fail "stale socket was not recovered"
stop_wm

"$compat_client" listen "$socket_path" 10 >"$tmp_dir/listener.log" 2>&1 &
listener_pid=$!
wait_for "test -S $socket_path" || fail "conflict listener did not start"
start_wm tests/fixtures/config-bspwm-compat.toml "$tmp_dir/live-conflict.log"
kill -0 "$wm_pid" || fail "live compatibility conflict stopped the WM"
grep -q 'already has a live listener' "$tmp_dir/live-conflict.log" ||
    fail "live listener conflict diagnostic missing"
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
kill "$listener_pid"; wait "$listener_pid" 2>/dev/null || true; listener_pid=
rm -f "$socket_path"

if grep -q 'semantic invariant failed' "$tmp_dir/wm.log"; then
    sed -n '1,180p' "$tmp_dir/wm.log" >&2
    fail "compatibility transition violated semantic invariants"
fi
echo "PASS: Xvfb Polybar bspwm compatibility wire protocol and lifecycle"
