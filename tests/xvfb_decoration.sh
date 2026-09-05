#!/bin/sh
set -eu

export DISPLAY=${BOX2430_TEST_DISPLAY:-:239}
box2430_bin=${BOX2430_BIN:-./build/debug/box2430}
fixture=./build/debug/x11-test-client
mutator=./build/debug/x11-property-mutator
stack=./build/debug/x11-stacking-order
configure=./build/debug/x11-configure-request
tmp_dir=$(mktemp -d)
xvfb_pid= wm_pid= client_pids=
cleanup() {
    result=$?
    for pid in $client_pids $wm_pid $xvfb_pid; do kill "$pid" 2>/dev/null || true; done
    if [ "$result" -ne 0 ] || [ "${failed:-false}" = true ]; then
        echo "Decoration logs retained at $tmp_dir" >&2
    else
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT INT TERM
fail() { failed=true; echo "FAIL: $*" >&2; exit 1; }
wait_until() {
    attempts=0
    while ! "$@"; do
        attempts=$((attempts + 1))
        [ "$attempts" -lt 150 ] || return 1
        sleep 0.02
    done
}
has_wm() { xprop -root _NET_SUPPORTING_WM_CHECK | grep -q '0x'; }
named() { xdotool search --name "^$1$" 2>/dev/null | head -n 1; }
managed() { xprop -root _NET_CLIENT_LIST | grep -q "$(printf '0x%x' "$1")"; }
spawn() {
    "$fixture" "$1" "$2" "${3:-100}" "${4:-100}" 220 140 >"$tmp_dir/$2.log" 2>&1 &
    client_pids="$client_pids $!"
    spawned_pid=$!
    spawned=
    for try in $(seq 1 150); do
        spawned=$(named "$2")
        [ -z "$spawned" ] || break
        sleep 0.02
    done
    [ -n "$spawned" ] || fail "$2 missing"
    wait_until managed "$spawned" || fail "$2 unmanaged"
}
deco() { named "box2430-decoration-$(printf '0x%x' "$1")"; }
mapped() { xwininfo -id "$1" 2>/dev/null | grep -q 'Map State: IsViewable'; }
decorated() { found=$(deco "$1"); [ -n "$found" ] && mapped "$found"; }
undecorated() { ! decorated "$1"; }
geometry() {
    xwininfo -id "$1" | awk '/Absolute upper-left X:/ {x=$NF}
        /Absolute upper-left Y:/ {y=$NF} /Width:/ {w=$NF} /Height:/ {h=$NF}
        /Border width:/ {b=$NF} END {print x,y,w,h,b}'
}
geometry_is() { [ "$(geometry "$1")" = "$2" ]; }
assert_geometry() { wait_until geometry_is "$1" "$2" || fail "$3: $(geometry "$1") != $2"; }
active() { xprop -root _NET_ACTIVE_WINDOW | grep -q "$(printf '0x%x' "$1")"; }
focus() { xdotool windowactivate "$1"; wait_until active "$1" || fail "focus $1"; }
pair_below() { "$stack" "$1" "$(deco "$1")" && "$stack" "$(deco "$1")" "$2" && "$stack" "$2" "$(deco "$2")"; }
has_color() { ./build/debug/x11-window-color "$1" "$2" >/dev/null; }
no_resource() { [ -z "$(deco "$1")" ]; }
stop_wm() { kill "$wm_pid"; wait "$wm_pid"; wm_pid=; }
check_logs() {
    if grep -E 'X11 error|semantic invariant failed|AddressSanitizer|runtime error:' "$tmp_dir"/wm*.log; then
        fail "WM diagnostics"
    fi
}

Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$tmp_dir/xvfb.log" 2>&1 &
xvfb_pid=$!
wait_until xdpyinfo >/dev/null 2>&1 || fail "Xvfb did not start"
./build/debug/decoration-test --x11-types

# Master switch, including force, is off by default. Adopt the same live client
# after enabling decoration, so content geometry can be compared directly.
"$box2430_bin" -c tests/fixtures/config-core.toml >"$tmp_dir/wm-default.log" 2>&1 &
wm_pid=$!
wait_until has_wm || fail "default WM missing"
spawn NORMAL DecBaseline
a=$spawned
before=$(geometry "$a")
no_resource "$a" || fail "default created a decoration"
stop_wm
"$box2430_bin" -c tests/fixtures/config-decoration.toml >"$tmp_dir/wm-enabled.log" 2>&1 &
wm_pid=$!
wait_until decorated "$a" || fail "adopted client decoration missing"
assert_geometry "$a" "$before" "adoption preserved content"
# Reset content using the actual ConfigureRequest/synthetic ConfigureNotify path.
notice=$("$configure" "$a" xywhb 100 100 220 140 19)
notice=$("$configure" "$a" b 100 100 220 140 19)
[ "$notice" = '100 100 220 140 2 1' ] || fail "ConfigureNotify content semantics: $notice"
da=$(deco "$a")
assert_geometry "$da" '100 76 224 24 0' 'derived title strip'
xwininfo -id "$da" | grep -q 'Override Redirect State: yes' || fail "not override redirect"
root=$(xwininfo -root | awk '/Window id:/ {print $4; exit}')
parent_of() { xwininfo -id "$1" -tree | awk '/Parent window id:/ {print $4; exit}'; }
[ "$(parent_of "$a")" = "$root" ] && [ "$(parent_of "$da")" = "$root" ] || fail 'client and decoration are not root siblings'
./build/debug/x11-tray-test-client request-dock "$da"
sleep 0.03
[ "$(parent_of "$da")" = "$root" ] || fail 'tray accepted decoration as an icon'
managed "$da" && fail "decoration entered client list"
xprop -root _NET_CLIENT_LIST_STACKING | grep -q "$(printf '0x%x' "$da")" && fail "decoration entered semantic stack"
focus "$a"
wait_until has_color "$da" '#334455' || fail "focused background"
has_color "$da" '#ffffff' || fail "title text not drawn"
old_hash=$(./build/debug/x11-window-hash "$da")
"$mutator" net-name "$a" 'Changed 中文 title'
hash_changed() { [ "$(./build/debug/x11-window-hash "$da")" != "$old_hash" ]; }
wait_until hash_changed || fail "title property did not redraw"
"$mutator" net-name "$a" none
"$mutator" wm-name "$a" 'Legacy title'
old_hash=$(./build/debug/x11-window-hash "$da")
"$mutator" wm-name "$a" 'Legacy replacement'
wait_until hash_changed || fail "legacy title did not redraw"

"$mutator" motif "$a" off
wait_until undecorated "$a" || fail "Motif opt-out"
assert_geometry "$a" '100 100 220 140 2' 'Motif preserves normal content'
for hint in on none unflagged short format type; do
    "$mutator" motif "$a" "$hint"
    wait_until decorated "$a" || fail "Motif $hint should permit decoration"
    "$mutator" motif "$a" off
    wait_until undecorated "$a" || fail "runtime opt-out after $hint"
done
"$mutator" motif "$a" none
wait_until decorated "$a" || fail 'Motif deletion'

BOX2430_TEST_MOTIF=off spawn NORMAL DecAutoOptOut 400 100
auto=$spawned
undecorated "$auto" || fail 'initial Motif no-decoration ignored'
"$mutator" motif "$auto" on
wait_until decorated "$auto" || fail 'initial opt-out runtime reversal'
BOX2430_TEST_MOTIF=off spawn SPLASH DecForceSplash 400 300
forced=$spawned
wait_until decorated "$forced" || fail 'ordered force override'
"$mutator" motif "$forced" off
decorated "$forced" || fail 'Motif overrode force'
spawn NORMAL DecNoneNormal 100 300
none=$spawned
"$mutator" motif "$none" on
undecorated "$none" || fail 'Motif overrode none'
for type in DIALOG SPLASH UTILITY MENU TOOLBAR; do
    spawn "$type" "DecType$type" 400 100
    if [ "$type" = DIALOG ]; then
        wait_until decorated "$spawned" || fail 'DIALOG auto excluded'
    else
        undecorated "$spawned" || fail "$type auto decorated"
    fi
    kill "$spawned_pid"; wait_until sh -c "! xprop -root _NET_CLIENT_LIST | grep -q $(printf '0x%x' "$spawned")" || fail 'type fixture removal'
done
focus "$a"
"$mutator" type "$a" splash
wait_until undecorated "$a" || fail 'runtime type eligibility'
"$mutator" type "$a" normal
wait_until decorated "$a" || fail 'runtime type return'

# Both active and inactive MONOCLE clients lose their decoration, including force.
spawn NORMAL DecBorderless 400 100
borderless=$spawned
assert_geometry "$borderless" '400 100 220 140 0' 'border=false content'
assert_geometry "$(deco "$borderless")" '400 76 220 24 0' 'border=false decoration'
kill "$spawned_pid"
wait_until no_resource "$borderless" || fail 'borderless cleanup'
spawn NORMAL DecAllow 400 100
allow=$spawned
xdotool windowstate --add FULLSCREEN "$allow"
assert_geometry "$allow" '0 0 800 600 0' 'client-requested real fullscreen'
undecorated "$allow" || fail 'allowed client fullscreen titlebar'
xdotool windowstate --remove FULLSCREEN "$allow"
wait_until decorated "$allow" || fail 'client fullscreen return'
kill "$spawned_pid"
wait_until no_resource "$allow" || fail 'allow cleanup'
focus "$a"
xdotool windowstate --add FULLSCREEN "$a"
decorated "$a" || fail 'fake fullscreen hid decoration'
assert_geometry "$a" '100 100 220 140 2' 'fake fullscreen preserves content'
xdotool windowstate --remove FULLSCREEN "$a"
xdotool key super+m
wait_until undecorated "$a" || fail 'MONOCLE titlebar'
undecorated "$forced" || fail 'force overrode MONOCLE'
assert_geometry "$a" '0 48 800 552 0' 'MONOCLE tab geometry unchanged'
xdotool key super+j
undecorated "$auto" || fail 'inactive MONOCLE titlebar'
xdotool key super+m
wait_until decorated "$a" || fail 'FREE restore'
assert_geometry "$a" '100 100 220 140 2' 'FREE restore content'
focus "$forced"
xdotool key super+f
wait_until undecorated "$forced" || fail 'force overrode fullscreen'
assert_geometry "$forced" '0 0 800 600 0' 'raw fullscreen geometry'
xdotool key super+f
wait_until decorated "$forced" || fail 'fullscreen exit'
xdotool key super+2
wait_until undecorated "$a" || fail 'hidden workspace decoration'
mapped "$a" && fail 'hidden workspace client'
xdotool key super+1
wait_until decorated "$a" || fail 'workspace return'

focus "$a"
xdotool key super+Up
assert_geometry "$a" '0 48 796 548 2' 'maximize outer workarea'
assert_geometry "$da" '0 24 800 24 0' 'maximize title workarea'
"$mutator" motif "$a" off
assert_geometry "$a" '0 24 796 572 2' 'runtime opt-out maximized'
"$mutator" motif "$a" on
assert_geometry "$a" '0 48 796 548 2' 'runtime decoration maximized'
xdotool key super+Up
assert_geometry "$a" '100 100 220 140 2' 'unmaximize restore'
for key in super+Left super+Right; do
    xdotool key "$key"
    if [ "$key" = super+Left ]; then sx=0; else sx=400; fi
    assert_geometry "$a" "$sx 48 396 548 2" 'side snap content'
    assert_geometry "$da" "$sx 24 400 24 0" 'side snap outer'
    xdotool key super+n
    assert_geometry "$a" '100 100 220 140 2' 'unsnap content restore'
done

# A/B pairs remain adjacent across focus-only, raise, lower and special tiers.
spawn NORMAL DecStackB 140 140
b=$spawned
db=$(deco "$b")
wait_until decorated "$b" || fail 'B decoration'
wait_until pair_below "$a" "$b" || fail 'initial pair stacking'
focus "$a"
pair_below "$a" "$b" || fail 'focus implicitly raised pair'
xdotool key super+r
wait_until pair_below "$b" "$a" || fail 'raise pair'
xdotool key super+l
wait_until pair_below "$a" "$b" || fail 'lower pair'
bar=$(named box2430-bar-0)
"$stack" "$db" "$bar" || fail 'decoration above native bar'
wait_until has_color "$db" '#223344' || fail 'unfocused background'
# A marker on an unrelated titlebar must survive generic UI refresh, title
# update and A/B focus changes. Its own title invalidation must repaint it.
df=$(deco "$forced")
./build/debug/x11-window-color "$df" '#ff00ff' --paint
"$mutator" input "$forced" true
"$mutator" net-name "$b" 'DecStackB renamed'
focus "$b"
focus "$a"
has_color "$df" '#ff00ff' || fail 'unrelated UI/focus/title refresh redrew all decorations'
"$mutator" net-name "$forced" 'DecForceSplash renamed'
marker_gone() { ! has_color "$df" '#ff00ff'; }
wait_until marker_gone || fail 'own title invalidation did not redraw'
spawn NOTIFICATION DecForceNotification 10 420
notification=$spawned
no_resource "$notification" || fail 'special notification became client decoration'
"$stack" "$bar" "$notification" || fail 'notification tier'
focus "$a"
xdotool key super+r
"$stack" "$da" "$bar" || fail 'raise escaped native tier'
"$stack" "$bar" "$notification" || fail 'raise escaped notification tier'

# Titlebar uses the existing center-warp move path; assert during motion and
# after release, and inspect real input focus as well as EWMH semantic focus.
xdotool mousemove --window "$da" 30 12 mousedown 1
sleep 0.05
xdotool mousemove_relative --sync 60 40
assert_geometry "$a" '160 140 220 140 2' 'titlebar drag content'
assert_geometry "$da" '160 116 224 24 0' 'titlebar follows during drag'
active "$a" || fail 'drag semantic focus'
[ "$(xdotool getwindowfocus)" = "$a" ] || fail 'decoration took X input focus'
xdotool mouseup 1
xdotool key super+Up key super+Up
assert_geometry "$a" '160 140 220 140 2' 'drag normal geometry committed'
xdotool mousemove --window "$da" 30 12 mousedown 1
sleep 0.05
"$mutator" motif "$a" off
wait_until undecorated "$a" || fail 'Motif during drag'
xdotool mousemove_relative --sync 20 20
assert_geometry "$a" '180 160 220 140 2' 'drag survives hidden input surface'
xdotool mouseup 1
"$mutator" motif "$a" on
wait_until decorated "$a" || fail 'titlebar after hidden-surface drag'
xdotool mousemove_relative --sync 10 10
assert_geometry "$a" '180 160 220 140 2' 'release ended hidden-surface drag'
xdotool mousemove --window "$da" 30 12 mousedown 1
sleep 0.05
xdotool mousemove --sync 0 0
preview() { xwininfo -root -tree | grep -q '400x2+0+24'; }
wait_until preview || fail 'title drag corner preview'
xdotool mouseup 1
assert_geometry "$a" '0 48 396 260 2' 'title drag corner snap'
xdotool key super+n

# Withdraw/destroy and WM restart/exit leave no orphan sibling resources.
kill "$spawned_pid" # notification
focus "$b"
xdotool windowunmap "$b"
wait_until no_resource "$b" || fail 'withdrawal orphan'
xdotool windowmap "$b"
wait_until decorated "$b" || fail 'remap after withdrawal'
old_db=$(deco "$b")
xdotool key super+Shift+r
resource_gone() { ! xwininfo -id "$old_db" >/dev/null 2>&1; }
wait_until resource_gone || fail 'restart retained old decoration'
wait_until decorated "$b" || fail 'restart did not adopt owner'
focus "$b"
db=$(deco "$b")
xdotool mousemove --window "$db" 30 12 mousedown 1
sleep 0.05
xdotool windowkill "$b"
wait_until no_resource "$b" || fail 'destroy during drag orphan'
xdotool mouseup 1
focus "$a"
stop_wm
mapped "$a" || fail 'WM exit hid original client'
no_resource "$a" || fail 'WM exit orphan decoration'

# Master switch wins even over manage-time force rules, including restart
# adoption. No-native-UI stacking exercises the unanchored pair expansion too.
sed 's/enabled = true/enabled = false/' tests/fixtures/config-decoration.toml >"$tmp_dir/disabled.toml"
"$box2430_bin" -c "$tmp_dir/disabled.toml" >"$tmp_dir/wm-disabled.log" 2>&1 &
wm_pid=$!
wait_until has_wm || fail 'disabled WM missing'
wait_until managed "$forced" || fail 'disabled force adoption'
no_resource "$forced" || fail 'force bypassed global master switch'
no_resource "$a" || fail 'global master switch created resource'
stop_wm
cp tests/fixtures/config-decoration.toml "$tmp_dir/no-ui.toml"
printf '\n[appearance.bar]\nenabled = false\n[appearance.tabs]\nenabled = false\n' >>"$tmp_dir/no-ui.toml"
"$box2430_bin" -c "$tmp_dir/no-ui.toml" >"$tmp_dir/wm-no-ui.log" 2>&1 &
wm_pid=$!
wait_until decorated "$forced" || fail 'no-UI force adoption'
wait_until decorated "$a" || fail 'no-UI normal adoption'
focus "$a"
xdotool key super+r
wait_until pair_below "$forced" "$a" || fail 'no-UI raise pair'
xdotool key super+l
wait_until pair_below "$a" "$forced" || fail 'no-UI lower pair'
stop_wm
no_resource "$a" || fail 'no-UI exit orphan'

# Rule and transient classification must be independent of decoration's type
# list resolver, with the master switch both disabled and enabled.
for enabled in false true; do
    printf '[appearance.decoration]\nenabled = %s\n[placement]\nnormal = "client"\ndialog = "center"\n[[rules]]\ntitle = "DecCompat*"\nwindow_type = "normal"\nborder = false\n' "$enabled" >"$tmp_dir/types.toml"
    "$box2430_bin" -c "$tmp_dir/types.toml" >"$tmp_dir/wm-types-$enabled.log" 2>&1 &
    wm_pid=$!
    wait_until has_wm || fail 'type compatibility WM missing'
    for fallback in NORMAL DIALOG; do
        BOX2430_TEST_TYPE_FALLBACK=$fallback spawn CUSTOM "DecCompat$fallback" 100 100
        compat=$spawned
        assert_geometry "$compat" '100 100 220 140 0' 'custom fallback retains normal rule/placement'
        if [ "$enabled" = true ]; then
            wait_until decorated "$compat" || fail 'custom fallback not AUTO eligible'
        else
            no_resource "$compat" || fail 'disabled fallback decoration'
        fi
        kill "$spawned_pid"
        wait_until no_resource "$compat" || fail 'fallback teardown'
    done
    spawn UTILITY DecCompatUtility 100 100
    assert_geometry "$spawned" '100 100 220 140 0' 'UTILITY retains normal rule/placement'
    undecorated "$spawned" || fail 'UTILITY became AUTO eligible'
    kill "$spawned_pid"
    # Parent ownership remains on workspace 2 even while workspace 1 is active.
    spawn NORMAL DecCompatParent 100 100
    parent=$spawned parent_pid=$spawned_pid
    xdotool key super+shift+2
    wait_until sh -c "! xwininfo -id $parent | grep -q 'Map State: IsViewable'" || fail 'parent workspace move'
    for type in CUSTOM UTILITY; do
        BOX2430_TEST_TRANSIENT_FOR=$parent BOX2430_TEST_TYPE_FALLBACK=DIALOG spawn "$type" "DecCompatTransient$type" 100 100
        transient=$spawned
        assert_geometry "$transient" '290 242 220 140 2' 'transient retains dialog placement/rule exclusion'
        mapped "$transient" && fail 'transient lost parent workspace inheritance'
        xdotool key super+2
        wait_until mapped "$transient" || fail 'transient workspace return'
        if [ "$enabled" = true ] && [ "$type" = CUSTOM ]; then
            wait_until decorated "$transient" || fail 'transient custom+dialog not eligible'
        else
            undecorated "$transient" || fail 'transient bypassed explicit type/master switch'
        fi
        kill "$spawned_pid"
        xdotool key super+1
    done
    kill "$parent_pid"
    stop_wm
done
check_logs
echo 'PASS: Xvfb decoration lifecycle, policy, title, FREE-only, geometry, stacking and drag'
