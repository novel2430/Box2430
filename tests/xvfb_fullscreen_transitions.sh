#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:146}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture_bin=${BOX2430_FIXTURE_BIN:-./build/debug/x11-test-client}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pid=

cleanup() {
    for pid in "$client_pid" "$wm_pid" "$xvfb_pid"; do
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
field() { DISPLAY=$display xwininfo -id "$1" | awk -v label="$2" '$0 ~ label {print $NF; exit}'; }
assert_geometry() {
    window=$1 expected_x=$2 expected_y=$3 expected_w=$4 expected_h=$5 expected_b=$6 label=$7
    [ "$(field "$window" 'Absolute upper-left X:')" = "$expected_x" ] || fail "$label: wrong x"
    [ "$(field "$window" 'Absolute upper-left Y:')" = "$expected_y" ] || fail "$label: wrong y"
    [ "$(field "$window" 'Width:')" = "$expected_w" ] || fail "$label: wrong width"
    [ "$(field "$window" 'Height:')" = "$expected_h" ] || fail "$label: wrong height"
    [ "$(field "$window" 'Border width:')" = "$expected_b" ] || fail "$label: wrong border"
}
has_fullscreen_state() {
    DISPLAY=$display xprop -id "$1" _NET_WM_STATE 2>/dev/null | grep -q _NET_WM_STATE_FULLSCREEN
}
assert_real() {
    window=$1 label=$2
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3; exit}')\" = 0" ||
        fail "$label: real fullscreen did not settle"
    assert_geometry "$window" 0 0 800 600 0 "$label"
    has_fullscreen_state "$window" || fail "$label: EWMH fullscreen state missing"
}
assert_normal() {
    window=$1 label=$2
    wait_for "test \"\$(DISPLAY=$display xwininfo -id $window | awk '/Border width:/ {print \$3; exit}')\" = 4" ||
        fail "$label: normal border did not settle"
    assert_geometry "$window" 60 70 220 140 4 "$label"
}
assert_fake() {
    window=$1 label=$2
    assert_normal "$window" "$label"
    has_fullscreen_state "$window" || fail "$label: fake EWMH fullscreen state missing"
}
assert_denied() {
    window=$1 label=$2
    assert_normal "$window" "$label"
    if has_fullscreen_state "$window"; then fail "$label: denied client fullscreen was exposed"; fi
}
stop_client() {
    kill "$client_pid" 2>/dev/null || true
    wait "$client_pid" 2>/dev/null || true
    client_pid=
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-geometry-state.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED | grep -q _NET_WM_STATE_FULLSCREEN" || fail "WM did not start"

for policy in fake allow deny; do
    case $policy in
        fake) title=GeometryFake ;;
        allow) title=GeometryAllow ;;
        deny) title=GeometryDeny ;;
    esac
    DISPLAY=$display "$fixture_bin" NORMAL "$title" 60 70 220 140 >"$tmp_dir/$policy.log" 2>&1 &
    client_pid=$!
    wait_for "DISPLAY=$display xdotool search --name $title >/dev/null 2>&1" || fail "$policy client missing"
    client=$(DISPLAY=$display xdotool search --name "$title" | head -n 1)
    assert_normal "$client" "$policy initial"

    # Duplicate client ADD/REMOVE is idempotent for both real and fake policy.
    DISPLAY=$display xdotool windowstate --add FULLSCREEN "$client"
    DISPLAY=$display xdotool windowstate --add FULLSCREEN "$client"
    case $policy in
        fake) assert_fake "$client" "$policy duplicate ADD" ;;
        allow) assert_real "$client" "$policy duplicate ADD" ;;
        deny) assert_denied "$client" "$policy duplicate ADD" ;;
    esac
    DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
    DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
    assert_normal "$client" "$policy duplicate REMOVE"
    if has_fullscreen_state "$client"; then fail "$policy duplicate REMOVE left EWMH state"; fi

    # client request -> user enable -> client remove -> user disable. The user
    # intent must keep real fullscreen alive after the client request is gone.
    DISPLAY=$display xdotool windowstate --add FULLSCREEN "$client"
    DISPLAY=$display xdotool key super+g
    DISPLAY=$display xdotool key super+g
    assert_real "$client" "$policy client then duplicate user enable"
    DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
    DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
    assert_real "$client" "$policy client remove while user intent remains"
    DISPLAY=$display xdotool key super+f
    assert_normal "$client" "$policy user disable after client remove"
    if has_fullscreen_state "$client"; then fail "$policy user disable left EWMH state"; fi

    # user enable -> client request -> user disable. What remains depends only
    # on the configured client policy.
    DISPLAY=$display xdotool key super+g
    assert_real "$client" "$policy user enable"
    DISPLAY=$display xdotool windowstate --add FULLSCREEN "$client"
    DISPLAY=$display xdotool windowstate --add FULLSCREEN "$client"
    DISPLAY=$display xdotool key super+f
    case $policy in
        allow) assert_real "$client" "$policy client request survives user disable" ;;
        fake) assert_fake "$client" "$policy fake request survives user disable" ;;
        deny) assert_denied "$client" "$policy denied request after user disable" ;;
    esac
    DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
    DISPLAY=$display xdotool windowstate --remove FULLSCREEN "$client"
    assert_normal "$client" "$policy final client remove"
    if has_fullscreen_state "$client"; then fail "$policy final EWMH state remained"; fi

    # Repeat the user-only enter/leave cycle to prove that no saved geometry or
    # border state is consumed by the first transition.
    DISPLAY=$display xdotool key super+g
    assert_real "$client" "$policy repeated user cycle first enter"
    DISPLAY=$display xdotool key super+f
    assert_normal "$client" "$policy repeated user cycle first leave"
    DISPLAY=$display xdotool key super+g
    assert_real "$client" "$policy repeated user cycle second enter"
    DISPLAY=$display xdotool key super+f
    assert_normal "$client" "$policy repeated user cycle second leave"
    if has_fullscreen_state "$client"; then fail "$policy repeated user cycle left EWMH state"; fi

    stop_client
done

kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb user/client fullscreen transition-state scenario"
