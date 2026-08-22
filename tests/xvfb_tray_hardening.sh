#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:194}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
tray_bin=${BOX2430_TRAY_BIN:-./build/debug/x11-tray-test-client}
lifecycle_bin=${BOX2430_LIFECYCLE_BIN:-./build/debug/x11-lifecycle-client}
test_client_bin=${BOX2430_TEST_CLIENT_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= watch_pid= client_pid= special_pid= icon_pid=

cleanup() {
    for pid in "$icon_pid" "$special_pid" "$client_pid" "$watch_pid" "$wm_pid" "$xvfb_pid"; do
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
        if [ "$attempts" -ge 300 ]; then return 1; fi
        sleep 0.02
    done
}
parent_of() { DISPLAY=$display xwininfo -id "$1" -tree | awk '/Parent window id:/ {print $4; exit}'; }
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
window_named() { DISPLAY=$display xwininfo -root -tree | awk -v name="$1" 'index($0, "\"" name "\"") {print $1; exit}'; }
stop_wm() {
    kill "$wm_pid"
    wait "$wm_pid" 2>/dev/null || true
    wm_pid=
}
start_wm() {
    log=$1
    DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-tray.toml >"$tmp_dir/$log" 2>&1 &
    wm_pid=$!
    wait_for "DISPLAY=$display $tray_bin owner >/dev/null 2>&1" || fail "tray selection was not acquired"
}
start_icon() {
    output=$1 name=$2 min_width=${3:-0}
    DISPLAY=$display "$tray_bin" icon "$name" 32 32 64 32 "$min_width" \
        >"$tmp_dir/$output" 2>"$tmp_dir/$output.err" &
    icon_pid=$!
    wait_for "test -s $tmp_dir/$output" || fail "$name did not dock"
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp -noreset >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"

# Observe MANAGER so Phase 6.5 can prove it carries a real server timestamp.
DISPLAY=$display "$tray_bin" watch >"$tmp_dir/manager.out" 2>"$tmp_dir/manager.err" &
watch_pid=$!
start_wm hardening-wm.log
wait_for "test -s $tmp_dir/manager.out" || fail "tray MANAGER announcement missing"
wait "$watch_pid" || fail "tray MANAGER watcher failed"
watch_pid=
set -- $(cat "$tmp_dir/manager.out")
manager_owner=$1
manager_timestamp=$2
owner=$(DISPLAY=$display "$tray_bin" owner)
[ "$manager_owner" = "$owner" ] || fail "MANAGER owner mismatch"
[ "$manager_timestamp" -gt 0 ] || fail "MANAGER used CurrentTime"
DISPLAY=$display "$tray_bin" visual >/dev/null || fail "_NET_SYSTEM_TRAY_VISUAL missing or wrong"

root=$(DISPLAY=$display xwininfo -root | awk '/Window id:/ {print $4; exit}')
host=$(window_named box2430-tray)
bar=$(window_named box2430-bar-0)
tab=$(window_named box2430-tabbar-0)
[ -n "$host" ] && [ -n "$bar" ] && [ -n "$tab" ] || fail "tray/UI internal windows missing"

# REQUEST_DOCK only transfers an external, otherwise-unowned window into tray
# ownership. Internal WM windows and protocol windows must remain untouched.
for target in "$root" "$owner" "$host" "$bar" "$tab"; do
    DISPLAY=$display "$tray_bin" request-dock "$target" || fail "could not send ownership attack"
    sleep 0.03
    kill -0 "$wm_pid" || fail "ownership attack stopped the WM"
    [ "$(DISPLAY=$display "$tray_bin" owner)" = "$owner" ] || fail "ownership attack disturbed tray selection"
done
[ "$(parent_of "$owner")" = "$root" ] || fail "tray owner was reparented"
[ "$(parent_of "$host")" = "$root" ] || fail "tray host was reparented"
[ "$(parent_of "$bar")" = "$root" ] || fail "native bar was reparented"
[ "$(parent_of "$tab")" = "$root" ] || fail "tab bar was reparented"

# A normal managed Client must never become TrayIcon ownership.
DISPLAY=$display "$lifecycle_bin" ordinary TrayOwnershipClient >"$tmp_dir/client.out" 2>"$tmp_dir/client.err" &
client_pid=$!
wait_for "test -s $tmp_dir/client.out" || fail "managed client did not start"
client=$(cat "$tmp_dir/client.out")
wait_for "DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi $client" || fail "managed client missing from client list"
DISPLAY=$display "$tray_bin" request-dock "$client"
sleep 0.05
[ "$(parent_of "$client")" = "$root" ] || fail "managed Client was reparented into tray"
DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -qi "$client" || fail "managed Client ownership was corrupted"

# The same invariant applies to special windows owned by the WM special list.
DISPLAY=$display "$test_client_bin" DOCK TrayOwnershipDock 30 30 120 20 >"$tmp_dir/special.out" 2>"$tmp_dir/special.err" &
special_pid=$!
wait_for "test -n \"\$(window_named TrayOwnershipDock)\"" || fail "special Dock did not appear"
special=$(window_named TrayOwnershipDock)
wait_for "DISPLAY=$display xprop -id $special WM_STATE 2>/dev/null | grep -q 'window state: Normal'" || fail "special Dock was not managed"
DISPLAY=$display "$tray_bin" request-dock "$special"
sleep 0.05
[ "$(parent_of "$special")" = "$root" ] || fail "special Dock was reparented into tray"

# An absurd client min-width may influence normalization but cannot escape the
# physical 800px bar. Two 2px edge spacings leave at most 796px for one icon.
start_icon huge.out TrayHugeHint 1000000000
set -- $(cat "$tmp_dir/huge.out"); huge=$1 huge_host=$2
wait_for "test \"\$(field $huge 'Width:')\" = 796" || fail "huge WM_NORMAL_HINT escaped per-icon clamp"
wait_for "test \"\$(field $huge_host 'Width:')\" = 800" || fail "tray widget escaped physical bar width"
# Re-docking an already-owned icon is idempotently rejected.
DISPLAY=$display "$tray_bin" request-dock "$huge"
sleep 0.03
[ "$(parent_of "$huge")" = "$huge_host" ] || fail "duplicate dock disturbed existing TrayIcon"
kill "$icon_pid"; wait "$icon_pid" 2>/dev/null || true; icon_pid=
wait_for "DISPLAY=$display xwininfo -id $huge_host | grep -q 'Map State: IsUnMapped'" || fail "tray did not collapse after huge icon exit"

# Synthetic signed dimensions exercise the int->unsigned boundary directly.
start_icon geometry.out TrayGeometry 0
set -- $(cat "$tmp_dir/geometry.out"); geometry_icon=$1 geometry_host=$2
DISPLAY=$display "$tray_bin" synthetic-configure "$geometry_host" "$geometry_icon" -99 -7
wait_for "test \"\$(field $geometry_icon 'Width:')\" = 24" || fail "negative synthetic geometry escaped normalization"
[ "$(field "$geometry_icon" 'Height:')" = 24 ] || fail "negative synthetic height escaped slot clamp"
DISPLAY=$display "$tray_bin" synthetic-configure "$geometry_host" "$geometry_icon" 2147483647 1
wait_for "test \"\$(field $geometry_icon 'Width:')\" = 796" || fail "huge synthetic geometry escaped clamp"
wait_for "test \"\$(field $geometry_host 'Width:')\" = 800" || fail "huge synthetic geometry overflowed tray layout"
kill "$icon_pid"; wait "$icon_pid" 2>/dev/null || true; icon_pid=
wait_for "DISPLAY=$display xwininfo -id $geometry_host | grep -q 'Map State: IsUnMapped'" || fail "tray did not collapse after geometry icon exit"

# Deterministic lifecycle storm: requests are deliberately followed quickly by
# resize/property/reparent/destroy activity to exercise stale-event ordering.
[ "$(DISPLAY=$display "$tray_bin" storm 64)" = 64 ] || fail "tray lifecycle storm helper failed"
wait_for "DISPLAY=$display xwininfo -id $host | grep -q 'Map State: IsUnMapped'" || fail "tray retained stale width after lifecycle storm"
kill -0 "$wm_pid" || fail "lifecycle storm stopped the WM"

# Unexpected owner destruction must deactivate tray, cleanly unembed a live
# icon, remove the host, and leave the ordinary WM running.
start_icon owner-loss.out TrayOwnerLoss 0
set -- $(cat "$tmp_dir/owner-loss.out"); owner_icon=$1 owner_host=$2
DISPLAY=$display "$tray_bin" destroy "$owner"
wait_for "grep -q 'system tray owner destroyed; tray disabled' $tmp_dir/hardening-wm.log" || fail "owner destruction was not detected"
kill -0 "$wm_pid" || fail "owner destruction stopped the WM"
if DISPLAY=$display "$tray_bin" owner >/dev/null 2>&1; then fail "selection survived owner destruction"; fi
wait_for "test \"\$(parent_of $owner_icon)\" = '$root'" || fail "owner destruction did not unembed icon"
if DISPLAY=$display xwininfo -id "$owner_host" >/dev/null 2>&1; then fail "tray host survived owner destruction"; fi
kill "$icon_pid" 2>/dev/null || true; wait "$icon_pid" 2>/dev/null || true; icon_pid=
stop_wm

# Host destruction is different: its embedded children may already be gone at
# X-server level, so hardening must clear C bookkeeping without touching stale
# icon XIDs, release selection, and keep the WM alive.
start_wm host-loss-wm.log
owner=$(DISPLAY=$display "$tray_bin" owner)
start_icon host-loss.out TrayHostLoss 0
set -- $(cat "$tmp_dir/host-loss.out"); host_icon=$1 host=$2
DISPLAY=$display "$tray_bin" destroy "$host"
wait_for "grep -q 'system tray host destroyed; tray disabled' $tmp_dir/host-loss-wm.log" || fail "host destruction was not detected"
kill -0 "$wm_pid" || fail "host destruction stopped the WM"
if DISPLAY=$display "$tray_bin" owner >/dev/null 2>&1; then fail "selection survived host destruction"; fi
if DISPLAY=$display xwininfo -id "$owner" >/dev/null 2>&1; then fail "tray owner survived host destruction"; fi
if DISPLAY=$display xwininfo -id "$host" >/dev/null 2>&1; then fail "tray host survived destruction"; fi
kill "$icon_pid" 2>/dev/null || true; wait "$icon_pid" 2>/dev/null || true; icon_pid=
stop_wm

kill "$special_pid" 2>/dev/null || true; wait "$special_pid" 2>/dev/null || true; special_pid=
kill "$client_pid" 2>/dev/null || true; wait "$client_pid" 2>/dev/null || true; client_pid=

echo "PASS: Xvfb XEmbed tray hardening/ownership/geometry/lifecycle scenario"
