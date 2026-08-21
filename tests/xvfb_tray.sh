#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:182}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tray_bin=${BOX2430_TRAY_BIN:-./build/debug/x11-tray-test-client}
stacking_bin=${BOX2430_STACKING_BIN:-./build/debug/x11-stacking-order}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= watch_pid= restart_pid= icon1_pid= icon2_pid= icon3_pid= blocker_pid=

cleanup() {
    for pid in "$icon3_pid" "$icon2_pid" "$icon1_pid" "$restart_pid" "$blocker_pid" "$watch_pid" "$wm_pid" "$xvfb_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; fi
    done
    rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }
wait_for() {
    attempts=0
    while ! eval "$1"; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
parent_of() { DISPLAY=$display xwininfo -id "$1" -tree | awk '/Parent window id:/ {print $4; exit}'; }
start_icon() {
    output=$1 name=$2 width=$3 height=$4 resize_width=$5 resize_height=$6
    DISPLAY=$display "$tray_bin" icon "$name" "$width" "$height" "$resize_width" "$resize_height" \
        >"$tmp_dir/$output" 2>"$tmp_dir/$output.err" &
    started_pid=$!
    wait_for "test -s $tmp_dir/$output" || fail "$name did not dock"
}
stop_wm() {
    kill "$wm_pid"
    wait "$wm_pid" 2>/dev/null || true
    wm_pid=
}
stop_wm_clean() {
    kill "$wm_pid"
    wait "$wm_pid" || fail "WM did not shut down cleanly"
    wm_pid=
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp -noreset >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

# Watch before WM startup so the ICCCM MANAGER announcement is observable.
DISPLAY=$display "$tray_bin" watch >"$tmp_dir/manager.out" 2>"$tmp_dir/manager.err" &
watch_pid=$!
# A mapped normal root child carrying _XEMBED_INFO models an icon surviving a
# WM restart.  It must never be adopted into Box2430's ordinary Client list
# while it waits for the new MANAGER announcement and re-docks.
DISPLAY=$display "$tray_bin" restart-icon TrayRestartIcon 32 32 >"$tmp_dir/restart.out" 2>"$tmp_dir/restart.err" &
restart_pid=$!
wait_for "test -s $tmp_dir/restart.out" || fail "restart-style tray icon did not start"
restart_icon=$(head -n 1 "$tmp_dir/restart.out")
sleep 0.05
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tray.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "test -s $tmp_dir/manager.out" || fail "tray MANAGER announcement missing"
wait "$watch_pid" || fail "tray MANAGER watcher failed"
watch_pid=
manager_owner=$(cat "$tmp_dir/manager.out")
selection_owner=$(DISPLAY=$display "$tray_bin" owner) || fail "tray selection not owned"
[ "$manager_owner" = "$selection_owner" ] || fail "MANAGER owner does not match tray selection owner"
DISPLAY=$display xprop -id "$selection_owner" _NET_SYSTEM_TRAY_ORIENTATION | grep -q '= 0' ||
    fail "tray orientation hint is not horizontal"

bar=$(DISPLAY=$display xwininfo -root -tree | awk '/"box2430-bar-0"/ {print $1; exit}')
[ -n "$bar" ] || fail "native bar missing"

wait_for "test \"\$(wc -l < $tmp_dir/restart.out)\" -ge 2" || fail "restart-style tray icon did not re-dock"
set -- $(tail -n 1 "$tmp_dir/restart.out"); redocked_icon=$1 restart_host=$2
[ "$redocked_icon" = "$restart_icon" ] || fail "restart-style icon changed window identity"
if DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi "$restart_icon"; then
    fail "restart-style tray icon entered ordinary Client lifecycle"
fi
[ "$(parent_of "$restart_icon")" = "$restart_host" ] || fail "restart-style icon was not embedded"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $restart_host | awk '/Width:/ {print \$2; exit}')\" = 28" || fail "restart-style tray width incorrect"
DISPLAY=$display "$stacking_bin" "$bar" "$restart_host" || fail "restart-style tray host is not above native bar"
kill "$restart_pid"; wait "$restart_pid" 2>/dev/null || true; restart_pid=
wait_for "DISPLAY=$display xwininfo -id $restart_host | grep -q 'Map State: IsUnMapped'" || fail "restart-style icon removal did not collapse tray"

# One square icon is normalized to the 24px bar height.  The visible host is a
# root-level sibling with 2px leading/trailing spacing: 2 + 24 + 2 = 28.
start_icon icon1.out TrayIconOne 32 32 64 32
icon1_pid=$started_pid
set -- $(cat "$tmp_dir/icon1.out"); icon1=$1 host=$2
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Width:/ {print \$2; exit}')\" = 28" ||
    fail "single-icon tray width incorrect"
[ "$(field "$icon1" 'Width:')" = 24 ] || fail "square icon width was not normalized"
[ "$(field "$icon1" 'Height:')" = 24 ] || fail "square icon height was not normalized"
[ "$(parent_of "$icon1")" = "$host" ] || fail "icon was not reparented into tray host"
[ "$(field "$host" 'Absolute upper-left X:')" = 772 ] || fail "tray host did not occupy right allocation"
DISPLAY=$display "$stacking_bin" "$bar" "$host" || fail "tray host is not above native bar"

# A second 48x24 icon preserves its aspect/width.  Dock order remains stable.
start_icon icon2.out TrayIconTwo 48 24 96 24
icon2_pid=$started_pid
set -- $(cat "$tmp_dir/icon2.out"); icon2=$1 host2=$2
[ "$host2" = "$host" ] || fail "tray host was recreated while docking"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Width:/ {print \$2; exit}')\" = 78" ||
    fail "multiple-icon tray width incorrect"
[ "$(field "$icon2" 'Width:')" = 48 ] || fail "wide icon width was not preserved"
[ "$(field "$icon2" 'Height:')" = 24 ] || fail "wide icon height was not normalized"

# _XEMBED_INFO controls visibility and therefore host width; hidden icons stay
# embedded but consume no allocation.
kill -USR1 "$icon1_pid"
wait_for "DISPLAY=$display xwininfo -id $icon1 | grep -q 'Map State: IsUnMapped'" || fail "XEMBED_MAPPED clear did not hide icon"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Width:/ {print \$2; exit}')\" = 52" || fail "hidden icon still consumed tray width"
kill -USR1 "$icon1_pid"
wait_for "DISPLAY=$display xwininfo -id $icon1 | grep -q 'Map State: IsViewable'" || fail "XEMBED_MAPPED set did not restore icon"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Width:/ {print \$2; exit}')\" = 78" || fail "restored icon did not restore tray width"

# Client-requested geometry is intercepted by tray-specific logic, not the
# ordinary ConfigureRequest path.  96x24 remains 96x24 and expands only tray.
kill -USR2 "$icon2_pid"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $icon2 | awk '/Width:/ {print \$2; exit}')\" = 96" || fail "tray resize request was not normalized/accepted"
[ "$(field "$icon2" 'Height:')" = 24 ] || fail "tray resize request escaped bar height"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Width:/ {print \$2; exit}')\" = 126" || fail "tray host did not follow icon resize"

# Reparenting out ends XEmbed ownership without disturbing the remaining icon.
kill -HUP "$icon1_pid"
wait_for "test \"\$(parent_of $icon1)\" != '$host'" || fail "client reparent did not end embedding"
wait_for "test \"\$(DISPLAY=$display xwininfo -id $host | awk '/Width:/ {print \$2; exit}')\" = 100" || fail "reparented icon remained in tray bookkeeping"

# Destroying the last embedded icon collapses the widget and unmaps the host.
kill "$icon2_pid"; wait "$icon2_pid" 2>/dev/null || true; icon2_pid=
wait_for "DISPLAY=$display xwininfo -id $host | grep -q 'Map State: IsUnMapped'" || fail "empty tray host did not unmap"

# Clean shutdown releases selection and reparents a live icon to root rather
# than destroying the foreign icon window.
start_icon icon3.out TrayShutdownIcon 32 32 64 32
icon3_pid=$started_pid
set -- $(cat "$tmp_dir/icon3.out"); icon3=$1 shutdown_host=$2
stop_wm_clean
wait_for "test \"\$(parent_of $icon3)\" = \"\$(DISPLAY=$display xwininfo -root | awk '/Window id:/ {print \$4; exit}')\"" || fail "WM shutdown did not reparent tray icon to root"
DISPLAY=$display xwininfo -id "$icon3" | grep -q 'Map State: IsViewable' || fail "WM shutdown left visible tray icon unmapped"
if DISPLAY=$display "$tray_bin" owner >/dev/null 2>&1; then fail "WM shutdown did not release tray selection"; fi
kill "$icon3_pid" 2>/dev/null || true; wait "$icon3_pid" 2>/dev/null || true; icon3_pid=

# Existing tray ownership is optional functionality: Box2430 must not steal it
# or fail WM startup.
DISPLAY=$display "$tray_bin" hold-selection >"$tmp_dir/blocker.out" 2>"$tmp_dir/blocker.err" &
blocker_pid=$!
wait_for "test -s $tmp_dir/blocker.out" || fail "selection blocker did not start"
blocker=$(cat "$tmp_dir/blocker.out")
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tray.toml >"$tmp_dir/conflict-wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM failed to start with tray conflict"
[ "$(DISPLAY=$display "$tray_bin" owner)" = "$blocker" ] || fail "Box2430 stole existing tray selection"
wait_for "grep -q 'already owned; tray disabled' $tmp_dir/conflict-wm.log" || fail "tray conflict diagnostic missing"
stop_wm
kill "$blocker_pid"; wait "$blocker_pid" 2>/dev/null || true; blocker_pid=

# Unexpected selection loss deactivates only tray state.  The WM keeps running,
# icons are cleanly unembedded, and Box2430 does not fight to reacquire it.
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tray.toml >"$tmp_dir/loss-wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display $tray_bin owner >/dev/null 2>&1" || fail "tray selection not reacquired for loss test"
start_icon loss-icon.out TrayLossIcon 32 32 64 32
icon1_pid=$started_pid
set -- $(cat "$tmp_dir/loss-icon.out"); loss_icon=$1 loss_host=$2
DISPLAY=$display "$tray_bin" hold-selection >"$tmp_dir/taker.out" 2>"$tmp_dir/taker.err" &
blocker_pid=$!
wait_for "test -s $tmp_dir/taker.out" || fail "selection taker did not start"
taker=$(cat "$tmp_dir/taker.out")
wait_for "grep -q 'selection lost; tray disabled' $tmp_dir/loss-wm.log" || fail "selection loss was not handled"
kill -0 "$wm_pid" || fail "tray selection loss stopped the WM"
[ "$(DISPLAY=$display "$tray_bin" owner)" = "$taker" ] || fail "Box2430 reacquired tray selection after loss"
wait_for "test \"\$(parent_of $loss_icon)\" = \"\$(DISPLAY=$display xwininfo -root | awk '/Window id:/ {print \$4; exit}')\"" || fail "selection loss did not unembed existing icon"
if DISPLAY=$display xwininfo -id "$loss_host" >/dev/null 2>&1; then fail "tray host survived selection loss"; fi

kill "$icon1_pid" 2>/dev/null || true; wait "$icon1_pid" 2>/dev/null || true; icon1_pid=
stop_wm
kill "$blocker_pid"; wait "$blocker_pid" 2>/dev/null || true; blocker_pid=

if grep -q 'box2430: X11 error' "$tmp_dir/wm.log" "$tmp_dir/conflict-wm.log" "$tmp_dir/loss-wm.log"; then
    cat "$tmp_dir/wm.log" "$tmp_dir/conflict-wm.log" "$tmp_dir/loss-wm.log" >&2
    fail "unexpected X11 error"
fi

echo "PASS: Xvfb XEmbed system-tray selection/lifecycle/geometry scenario"
