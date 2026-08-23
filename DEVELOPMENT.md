# Box2430 Development

This document describes how to build, test, debug, and verify the current
Box2430 repository.

For runtime semantics see `docs/ARCHITECTURE.md`. For commands/configuration see
`docs/REFERENCE.md`. For long-lived engineering principles see
`docs/IMPLEMENTATION_STYLE.md`.

## Required build environment

* C11 compiler: GCC or Clang
* GNU Make
* `pkg-config`
* X11 development libraries:
  * `x11`
  * `xinerama`
  * `xft`

Verify the required libraries with:

```sh
pkg-config --exists x11 xinerama xft
```

Useful X11 test/debug tools include:

* Xvfb
* Xephyr
* `xdpyinfo`
* `xprop`
* `xwininfo`
* `xdotool`
* `xterm`
* `xrandr`
* `xwd`
* ImageMagick (`convert`)

Useful general debugging tools include `gdb`, `strace`, and Valgrind.

## Build profiles

Debug build:

```sh
make
```

Binary:

```text
build/debug/box2430
```

Release build:

```sh
make release
```

Binary:

```text
build/release/box2430
```

AddressSanitizer + UndefinedBehaviorSanitizer build:

```sh
make sanitize
```

The sanitizer profile uses Clang by default and produces:

```text
build/sanitize/box2430
```

Build all X11 fixture/helper programs and focused local tests with:

```sh
make test-tools
```

Clean all build profiles:

```sh
make clean
```

A staged install can be checked without modifying the host system:

```sh
make release
make DESTDIR="$PWD/build/stage" install
```

## Environment preflight

On a new development machine, verify the environment before treating a failure
as a WM bug.

At minimum:

1. `pkg-config --exists x11 xinerama xft` succeeds;
2. `make` succeeds;
3. `make test-tools` succeeds;
4. an Xvfb display can start and accept `xdpyinfo` connections;
5. `make sanitize` builds and the sanitizer runtime can execute;
6. Xephyr can connect to the host X display when nested visual/topology testing
   is needed.

Missing libraries, sandbox socket restrictions, or an unavailable host display
change what can be verified; they do not by themselves imply a Box2430 semantic
failure.

## Testing strategy

Prefer observable behavior through the real path used by the WM.

For example, a keybinding regression is ideally exercised as:

```text
X KeyPress
    -> key lookup / modifiers
    -> stored binding
    -> command context + dispatch
    -> WM state transition
    -> observable X11 result
```

Likewise:

* use `xdotool`/X events for binding behavior rather than calling a command
  helper directly;
* when multiple inputs converge on one semantic transition, verify both the
  shared authoritative result and any intentional source-specific projection
  difference; for example, keyboard monitor navigation and pointer-driven
  monitor/workspace selection should agree on selected monitor/focus while only
  the explicit monitor command warps the pointer;
* spawn tests should cover binding dispatch, `fork`/`exec`, and the resulting
  window/process behavior together;
* focus-cycle tests should verify that ordinary focus/raise operations do not
  reorder stable client order;
* geometry tests should inspect actual X window geometry;
* bar/tab/workspace interactions should send real pointer events to the native
  UI windows;
* tray tests should exercise the XEmbed selection, docking, reparenting, mapped
  state, geometry, and lifecycle rather than only tray helper functions.

Use the cheapest layer that genuinely verifies the behavior:

```text
pure/local logic         -> focused C test / build check
headless X11 behavior    -> Xvfb
visual/topology behavior -> Xephyr
real hardware/session    -> user-controlled X session
```

When fixing a reproducible bug, prefer the smallest regression scenario that
fails on the buggy behavior and passes for the intended reason.

### Debug semantic invariant checks

The debug profile checks Box2430's in-memory semantic invariants after startup
and completed X event handling. The checker validates monitor/workspace/client
ownership, workspace-local order coherence, selected-monitor/focused-client
coherence, and snap/maximize exclusion without querying X.

The full Xvfb suite therefore exercises these checks through real management,
focus, workspace, movement, lifecycle, and protocol paths. A failure reported as
`semantic invariant failed` indicates internal authoritative-state corruption;
diagnose the transition that preceded it rather than weakening the checker.

Release builds use `NDEBUG` and omit the checker. Sanitizer builds retain it.

## `make test`

The normal deterministic suite is:

```sh
make test
```

The Makefile performs:

```text
build/debug/monitor-geometry-test
build/debug/ui-label-test
tests/run_xvfb.sh
```

So `make test` contains two focused local tests followed by the Xvfb integration
suite.

### Monitor geometry test

`tests/monitor_geometry_test.c` checks the pure monitor-topology helpers in
`src/monitor.c`, including:

* duplicate rectangle normalization;
* fallback behavior;
* negative coordinates;
* exact matching across Xinerama enumeration reorder;
* insertion/removal;
* resolution/origin changes;
* overlap/center-distance continuity;
* deterministic ambiguous geometry matching.

This test verifies the geometry matching primitive. Xephyr topology scenarios
verify that the same model is integrated correctly with live X11 monitor/client
state.

### UI label test

`tests/ui_label_test.c` exercises pure/native-UI helper behavior without needing
a live WM session. Coverage includes label formatting/style resolution,
workspace/mode/title state selection, clock visibility, and related UI helper
logic used by the bar and tabs.

## Xvfb integration suite

After building the WM and test tools, the headless scenarios can also be run
directly:

```sh
tests/run_xvfb.sh
```

The current runner executes 36 scenarios:

```text
xvfb_bootstrap.sh
xvfb_core_commands.sh
xvfb_workspace_transition.sh
xvfb_config.sh
xvfb_v2_config.sh
xvfb_border_modes.sh
xvfb_rules.sh
xvfb_special_windows.sh
xvfb_native_bar.sh
xvfb_override_redirect_notification.sh
xvfb_bar_widgets.sh
xvfb_workspacebar.sh
xvfb_bar_pressure.sh
xvfb_status_clock.sh
xvfb_tray.sh
xvfb_tray_hardening.sh
xvfb_semantic_geometry.sh
xvfb_configure_request.sh
xvfb_fullscreen_transitions.sh
xvfb_maximize_ewmh.sh
xvfb_mouse.sh
xvfb_numlock.sh
xvfb_keymap.sh
xvfb_normal_hints.sh
xvfb_focus_cycle.sh
xvfb_tabbar.sh
xvfb_tabbar_empty.sh
xvfb_lifecycle.sh
xvfb_visibility_withdrawal.sh
xvfb_restart.sh
xvfb_focus_urgency.sh
xvfb_focus_history.sh
xvfb_focus_protocol.sh
xvfb_property_cache.sh
xvfb_focus_compat.sh
xvfb_spawn.sh
```

Some test/fixture names still contain `v2` because they were introduced during
that development phase. The name is historical; the tested native-UI
configuration is part of the current repository behavior.

The Xvfb suite currently covers areas including:

* WM ownership and startup discovery, including adopted `IconicState` windows;
* manage/unmanage and original-border restoration;
* inactive-workspace unmapping vs. genuine client withdrawal;
* workspace transition mapping/focus/paint ordering;
* core command dispatch and command-context validation;
* strict atomic TOML parsing and invalid-config fallback;
* native UI configuration validation, state-style validation, and old-config rejection;
* independent FREE/MONOCLE border presentation;
* ordered rules and client fullscreen policies;
* Docks, struts, workareas, Desktop/Notification special windows;
* native bar top/bottom workarea reservation;
* bar widget layout/state, narrow-layout pressure, and workspace-label hit testing;
* root status and internal clock refresh;
* external override-redirect notification stacking relative to native UI;
* XEmbed tray selection, docking, lifecycle, geometry normalization, and hardening;
* semantic geometry, normal restore state, snap, maximize, MONOCLE, and fullscreen nesting;
* ConfigureRequest ownership/idempotence;
* EWMH maximize and fullscreen transitions;
* interactive mouse move/resize and snap preview;
* NumLock/CapsLock-insensitive passive grabs;
* duplicate KeySym/all-KeyCode grabs and runtime `MappingNotify` rebuilds;
* ICCCM size hints;
* stable client/tab focus cycling;
* MONOCLE tab interactions and empty-tab behavior;
* urgency and workspace focus-history restoration;
* `WM_TAKE_FOCUS`, InputHint, and FocusIn compatibility;
* runtime metadata/property-cache refresh without implicit re-placement;
* restart boundaries, including cold-start-only background/autostart behavior;
* direct `spawn`, `spawn-shell`, and SIGCHLD inheritance/reset behavior.

Run one scenario directly when iterating on a focused area:

```sh
tests/xvfb_tray.sh
tests/xvfb_workspacebar.sh
tests/xvfb_focus_compat.sh
```

The scripts use `BOX2430_TEST_DISPLAY` when an explicit unused display number is
needed:

```sh
BOX2430_TEST_DISPLAY=:150 tests/xvfb_spawn.sh
```

A different WM binary can be supplied with `BOX2430_BIN`:

```sh
BOX2430_BIN=./build/sanitize/box2430 tests/run_xvfb.sh
```

This is the normal way to run the same integration suite against another build
profile.

## Xephyr scenarios

Xephyr is used when nested visual behavior, multi-monitor rendering, or live
Xinerama topology changes matter.

Current scenarios are:

```text
tests/xephyr_visual.sh
tests/xephyr_multimon.sh
tests/xephyr_topology.sh
tests/xephyr_bar_widgets.sh
tests/xephyr_native_bar_topology.sh
tests/xephyr_tray.sh
tests/xephyr_tray_topology.sh
```

They cover behavior such as:

* FREE overlap and border presentation;
* snap preview, snap, maximize, and fullscreen visuals;
* MONOCLE tab rendering, including non-ASCII titles;
* per-monitor workspaces and client movement;
* cross-monitor drag;
* Xinerama monitor selection and logical monitor continuity;
* keyboard, workspace-bar, and exposed-root monitor-selection behavior,
  including their intended pointer-warp differences;
* topology reconciliation across resolution/origin changes;
* preservation/rematerialization of latent FREE geometry, snap, maximize,
  MONOCLE, and fullscreen state;
* native bar/widgets on multi-monitor layouts;
* native bar placement across topology changes;
* tray rendering/embedding in a nested server;
* tray relocation/reallocation as selected monitor/topology changes.

Some Xephyr scenarios capture visual evidence under:

```text
build/evidence/
```

Xephyr requires access to a working host X display. If the nested server cannot
connect to that host display, use Xvfb for non-visual verification and report the
Xephyr result as unverified rather than failed product behavior.

## X server restrictions in sandboxes

Xvfb and Xephyr may fail inside restrictive filesystem/process sandboxes that
prevent X server socket creation.

Typical symptoms include:

```text
Xvfb did not start
Cannot establish any listening sockets
Owner of /tmp/.X11-unix should be set to root
```

When these appear because of the execution environment, do not repeatedly
change display numbers or modify Box2430 code to compensate. Re-run the same
test outside the restrictive sandbox with an unused display.

Compilation, static inspection, and tests that do not launch an X server may
still be useful inside the sandbox.

## Sanitizers

`make sanitize` enables AddressSanitizer and UndefinedBehaviorSanitizer.

A useful substantial-change sequence is:

```sh
make clean
make sanitize
make test-tools
BOX2430_BIN=./build/sanitize/box2430 tests/run_xvfb.sh
```

Run relevant Xephyr scenarios with the sanitizer binary as well when the change
affects monitor topology, native UI, tray, or visual geometry.

If GCC's sanitizer runtime is unavailable on a machine, using Clang is preferred
over weakening checks.

LeakSanitizer-clean process teardown is not treated as a universal requirement
because external X11/font libraries can retain process-lifetime allocations.

## Valgrind and Xft/fontconfig baseline

Xft/fontconfig may retain process-lifetime allocations at exit. When a Valgrind
finding appears to originate entirely in those libraries, compare it with a
minimal Xft-only reproducer in the same environment before treating it as a
Box2430 leak.

Do not add product cleanup workarounds solely to suppress an identical external
library teardown baseline.

This does **not** make arbitrary findings acceptable. Investigate any:

* invalid read or write;
* use of uninitialized data;
* allocation whose stack includes Box2430 code;
* leak whose allocation path differs from the external-library baseline;
* leak that grows repeatedly during normal WM operations.

## Debugging X11 behavior

Useful inspection commands include:

```sh
xprop -root
xprop -id <window-id>
xwininfo -root -tree
xwininfo -id <window-id>
xdotool search --name '<title>'
xdotool getwindowgeometry --shell <window-id>
```

For native UI/tray work, window names are also useful when inspecting the root
tree. Box2430 names monitor bars and its tray owner/host, which can help separate
internal windows from managed application clients.

For event-order or lifetime bugs, preserve the real asynchronous X11 sequence
before changing implementation details. A disappearing client may legitimately
produce an ordinary `BadWindow`; distinguish that from incorrect internal
bookkeeping or unexpected non-`BadWindow` X errors.

When an integration scenario fails, inspect the WM/X-server logs produced by the
scenario before reducing the test to an internal helper.

## Adding or changing tests

Choose the narrowest existing scenario that matches the behavior being changed.

Prefer:

* extending an existing `xvfb_*.sh` scenario for deterministic X11 behavior;
* adding a new Xvfb scenario when the behavior creates a distinct regression boundary;
* using Xephyr only when visual output, live topology, or nested multi-monitor
  behavior is essential;
* extending `monitor_geometry_test.c` or `ui_label_test.c` when the behavior is
  genuinely pure/local;
* adding a small fixture client under `tests/` when an exact ICCCM/EWMH/XEmbed
  behavior cannot be produced reliably with `xterm` or `xdotool`.

Fixture configuration belongs under:

```text
tests/fixtures/
```

A regression test should fail for the original bug and pass because of the
intended fix, not because of unrelated sleep timing or environmental assumptions.

Avoid writing a test that merely duplicates implementation logic and then proves
the duplicate agrees with itself.

## Runtime economy checks

A lightweight runtime baseline can be measured with:

```sh
make release
tests/measure_economy.sh
```

The script reports idle RSS, RSS with a configurable number of xterms, and idle
CPU usage under Xvfb.

Use it to notice large regressions, not as a fixed release gate tied to one
historical machine.

Change the client count with:

```sh
BOX2430_CLIENT_COUNT=20 tests/measure_economy.sh
```

## Real-session smoke testing

Xvfb/Xephyr cover most automated behavior, but changes involving real input
devices, notification daemons, wallpaper tools, panels, drivers, X.Org/XLibre
differences, physical multi-monitor changes, or real tray applications may still
deserve a real-session smoke test.

Use a disposable X session or spare VT. Automated tooling/coding agents must not
replace the user's active WM unless explicitly authorized for that exact action.

A convenient dedicated startup file is:

```sh
printf '%s\n' \
  'xterm &' \
  'exec /path/to/box2430 -c /path/to/config.toml' \
  > ~/.xinitrc-box2430
```

Start it manually on an unused display using the host system's normal X.Org or
XLibre `xinit`/`startx` procedure.

Before testing, configure an explicit `wm quit` binding so the session can be
exited cleanly.

For a broad smoke test, check at least:

* startup and existing-window adoption;
* ordinary focus/raise/lower;
* workspace switching per monitor;
* FREE/MONOCLE and tab interaction;
* native bar rendering/status/clock;
* tray with a real XEmbed application when relevant;
* move/resize/snap/maximize/fullscreen;
* notification stacking if native UI stacking changed;
* physical monitor unplug/replug only when topology behavior is in scope;
* clean WM exit/restart.

## Verification reporting

Only report a test as passing when it was actually executed successfully in the
current environment.

Use clear categories:

* **PASS** — executed and succeeded;
* **FAIL** — executed and failed;
* **UNVERIFIED** — could not execute because of an environment limitation;
* **USER HANDOFF** — intentionally requires a real user-controlled X session.

Do not convert a missing development library, unavailable X server, missing
Xephyr host display, or sandbox socket restriction into a statement about
Box2430 correctness.
