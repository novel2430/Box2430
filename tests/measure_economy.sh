#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:139}
box2430_bin=${BOX2430_BIN:-./build/release/box2430}
client_count=${BOX2430_CLIENT_COUNT:-10}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pids=

cleanup() {
    for pid in $client_pids; do kill "$pid" 2>/dev/null || true; done
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
        if [ "$attempts" -ge 250 ]; then return 1; fi
        sleep 0.02
    done
}
rss_kib() { awk '/^VmRSS:/ {print $2}' "/proc/$1/status"; }
cpu_ticks() { awk '{print $14 + $15}' "/proc/$1/stat"; }

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-core.toml \
    >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED 2>/dev/null | grep -q _NET_ACTIVE_WINDOW" ||
    fail "WM did not start"

sleep 0.5
idle_rss=$(rss_kib "$wm_pid")
ticks_before=$(cpu_ticks "$wm_pid")
sleep 2
ticks_after=$(cpu_ticks "$wm_pid")
clock_ticks=$(getconf CLK_TCK)
idle_cpu=$(awk -v before="$ticks_before" -v after="$ticks_after" \
    -v hz="$clock_ticks" 'BEGIN { printf "%.3f", (after-before) * 100 / (hz * 2) }')

i=1
while [ "$i" -le "$client_count" ]; do
    DISPLAY=$display xterm -title "EconomyClient$i" >"$tmp_dir/client-$i.log" 2>&1 &
    client_pids="$client_pids $!"
    i=$((i + 1))
done
wait_for "test \"\$(DISPLAY=$display xprop -root _NET_CLIENT_LIST | grep -o '0x[0-9a-fA-F]*' | wc -l)\" -eq $client_count" ||
    fail "clients were not all managed"
sleep 0.5
clients_rss=$(rss_kib "$wm_pid")

echo "x_server=Xvfb screen=800x600x24"
echo "client_type=xterm client_count=$client_count"
echo "idle_rss_kib=$idle_rss"
echo "rss_${client_count}_clients_kib=$clients_rss"
echo "idle_cpu_percent=$idle_cpu"

for pid in $client_pids; do kill "$pid" 2>/dev/null || true; done
client_pids=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
echo "PASS: economy runtime baseline"
