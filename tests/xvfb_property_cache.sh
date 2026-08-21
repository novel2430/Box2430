#!/bin/sh
set -eu

display=${BOX2430_TEST_DISPLAY:-:177}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
focus_bin=${BOX2430_FOCUS_BIN:-./build/debug/x11-focus-client}
mutator_bin=${BOX2430_PROPERTY_MUTATOR_BIN:-./build/debug/x11-property-mutator}
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= take_pid= none_pid= parent_pid= child_pid=

cleanup() {
    for pid in "$child_pid" "$parent_pid" "$none_pid" "$take_pid" "$wm_pid" "$xvfb_pid"; do
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
map_state() {
    DISPLAY=$display xwininfo -id "$1" | awk -F: '/Map State:/ {gsub(/^ +/, "", $2); print $2; exit}'
}
geometry() {
    DISPLAY=$display xwininfo -id "$1" |
        awk '/Absolute upper-left X:/ {x=$NF} /Absolute upper-left Y:/ {y=$NF} /Width:/ {w=$NF} /Height:/ {h=$NF} END {print x, y, w, h}'
}

Xvfb "$display" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_for "DISPLAY=$display xdpyinfo >/dev/null 2>&1" || fail "Xvfb did not start"
DISPLAY=$display "$box2430_bin" -c tests/fixtures/config-focus-compat.toml >"$tmp_dir/wm.log" 2>&1 &
wm_pid=$!
wait_for "DISPLAY=$display xprop -root _NET_SUPPORTED >/dev/null 2>&1" || fail "WM did not start"

# Establish one globally-active client and one client that initially rejects
# both ICCCM focus mechanisms.
DISPLAY=$display "$focus_bin" take PropertyTake "$tmp_dir/take.marker" >"$tmp_dir/take.log" 2>&1 &
take_pid=$!
wait_for "test -f $tmp_dir/take.marker" || fail "take-focus baseline did not start"
take=$(DISPLAY=$display xdotool search --name PropertyTake | head -n 1)
DISPLAY=$display "$focus_bin" none PropertyNone "$tmp_dir/none.marker" >"$tmp_dir/none.log" 2>&1 &
none_pid=$!
wait_for "DISPLAY=$display xdotool search --name PropertyNone >/dev/null 2>&1" || fail "no-focus baseline did not start"
none=$(DISPLAY=$display xdotool search --name PropertyNone | head -n 1)
wait_active "$take" || fail "non-focusable baseline client stole focus on map"

# WM_HINTS is cached. Runtime mutation must refresh accepts_input, but changing
# the property alone must never move semantic focus.
DISPLAY=$display "$mutator_bin" input "$none" true || fail "could not enable InputHint"
wait_active "$take" || fail "WM_HINTS mutation stole focus"
DISPLAY=$display xdotool key super+j
wait_active "$none" || fail "runtime InputHint=True was not refreshed"

DISPLAY=$display "$mutator_bin" input "$none" false || fail "could not disable InputHint"
wait_active "$none" || fail "WM_HINTS disable changed semantic focus reactively"
DISPLAY=$display xdotool key super+j
wait_active "$take" || fail "could not restore take-focus baseline"
DISPLAY=$display xdotool key super+j
wait_active "$take" || fail "runtime InputHint=False was not refreshed"

# WM_PROTOCOLS has an independent invalidation path. Adding WM_TAKE_FOCUS must
# make the client focusable only when the WM later chooses it; the property
# mutation itself is metadata/capability refresh, not a focus request.
rm -f "$tmp_dir/none.marker"
DISPLAY=$display "$mutator_bin" take-focus "$none" true || fail "could not add WM_TAKE_FOCUS"
wait_active "$take" || fail "WM_PROTOCOLS mutation stole focus"
DISPLAY=$display xdotool key super+j
wait_active "$none" || fail "runtime WM_TAKE_FOCUS add was not refreshed"
wait_for "test -f $tmp_dir/none.marker" || fail "WM_TAKE_FOCUS was not delivered after runtime add"

DISPLAY=$display "$mutator_bin" take-focus "$none" false || fail "could not remove WM_TAKE_FOCUS"
wait_active "$none" || fail "WM_PROTOCOLS removal changed semantic focus reactively"
DISPLAY=$display xdotool key super+j
wait_active "$take" || fail "could not restore focus after protocol removal"
DISPLAY=$display xdotool key super+j
wait_active "$take" || fail "runtime WM_TAKE_FOCUS removal was not refreshed"

# Metadata relationships/types may change after manage, but Round 1 deliberately
# does not turn those property changes into a reactive layout or ownership engine.
DISPLAY=$display xterm -title PropertyParent -geometry 24x6+80+260 >"$tmp_dir/parent.log" 2>&1 &
parent_pid=$!
wait_for "DISPLAY=$display xdotool search --name PropertyParent >/dev/null 2>&1" || fail "parent client missing"
parent=$(DISPLAY=$display xdotool search --name PropertyParent | head -n 1)
DISPLAY=$display xterm -title PropertyChild -geometry 24x6+420+260 >"$tmp_dir/child.log" 2>&1 &
child_pid=$!
wait_for "DISPLAY=$display xdotool search --name PropertyChild >/dev/null 2>&1" || fail "child client missing"
child=$(DISPLAY=$display xdotool search --name PropertyChild | head -n 1)
wait_active "$child" || fail "child did not start focused"

DISPLAY=$display xdotool key super+shift+2
wait_for "test \"\$(DISPLAY=$display xwininfo -id $child | awk -F: '/Map State:/ {gsub(/^ +/, \"\", \$2); print \$2; exit}')\" = \"IsUnMapped\"" ||
    fail "child did not move to hidden workspace"
wait_active "$parent" || fail "moving child did not restore parent focus"
before_geometry=$(geometry "$child")

DISPLAY=$display "$mutator_bin" transient "$child" "$parent" || fail "could not set WM_TRANSIENT_FOR"
wait_active "$parent" || fail "WM_TRANSIENT_FOR mutation stole focus"
[ "$(map_state "$child")" = "IsUnMapped" ] || fail "WM_TRANSIENT_FOR mutation moved workspace/visibility"
[ "$(geometry "$child")" = "$before_geometry" ] || fail "WM_TRANSIENT_FOR mutation changed geometry"

DISPLAY=$display "$mutator_bin" type "$child" dialog || fail "could not set dialog type"
wait_active "$parent" || fail "dialog type mutation stole focus"
[ "$(map_state "$child")" = "IsUnMapped" ] || fail "dialog type mutation changed workspace/visibility"
[ "$(geometry "$child")" = "$before_geometry" ] || fail "dialog type mutation changed geometry"

DISPLAY=$display "$mutator_bin" type "$child" dock || fail "could not set dock type"
wait_active "$parent" || fail "dock type mutation stole focus"
[ "$(map_state "$child")" = "IsUnMapped" ] || fail "dock type mutation reclassified the managed client"
[ "$(geometry "$child")" = "$before_geometry" ] || fail "dock type mutation changed geometry"

# Deleting WM_TRANSIENT_FOR also needs a real invalidation path; it must remain
# side-effect free just like setting it.
DISPLAY=$display "$mutator_bin" transient "$child" none || fail "could not clear WM_TRANSIENT_FOR"
wait_active "$parent" || fail "clearing WM_TRANSIENT_FOR changed focus"
[ "$(map_state "$child")" = "IsUnMapped" ] || fail "clearing WM_TRANSIENT_FOR changed visibility"

kill "$child_pid" 2>/dev/null || true; child_pid=
kill "$parent_pid" 2>/dev/null || true; parent_pid=
kill "$none_pid" 2>/dev/null || true; none_pid=
kill "$take_pid" 2>/dev/null || true; take_pid=
kill "$wm_pid"; wait "$wm_pid"; wm_pid=
if grep -q "box2430: X11 error" "$tmp_dir/wm.log"; then
    sed -n '1,200p' "$tmp_dir/wm.log" >&2
    fail "unexpected X11 error"
fi
echo "PASS: Xvfb runtime property-cache/non-reactive metadata scenario"
