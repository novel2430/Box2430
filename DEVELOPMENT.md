# Box2430 Development

This document describes how to build, test, debug, and verify the current Box2430 codebase.

For implementation semantics, see `docs/ARCHITECTURE.md`. For long-lived engineering principles, see `docs/IMPLEMENTATION_STYLE.md`.

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

Useful X11 test tools include:

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

Useful debugging tools include `gdb`, `strace`, and Valgrind.

## Build profiles

Debug build:

```sh
make
```

The binary is:

```text
build/debug/box2430
```

Release build:

```sh
make release
```

The binary is:

```text
build/release/box2430
```

AddressSanitizer + UndefinedBehaviorSanitizer build:

```sh
make sanitize
```

This profile uses Clang by default and produces:

```text
build/sanitize/box2430
```

Build the X11 fixture/helper programs used by the integration tests with:

```sh
make test-tools
```

Clean all profiles with:

```sh
make clean
```

A staged installation can be checked without modifying the host system:

```sh
make release
make DESTDIR="$PWD/build/stage" install
```

## Environment preflight

On a new development machine, verify the environment before investigating WM failures.

At minimum, confirm:

1. `pkg-config --exists x11 xinerama xft` succeeds;
2. `make` succeeds;
3. `make test-tools` succeeds;
4. an Xvfb display can start and accept `xdpyinfo` connections;
5. `make sanitize` builds and the sanitizer runtime actually executes;
6. Xephyr can connect to the host display if visual or multi-monitor testing is needed.

An environment failure changes what can be verified; it does not imply a Box2430 semantic failure.

## Testing philosophy

Test observable WM behavior through the same path used by a real user whenever practical.

For example, a keybinding bug should normally be exercised as:

```text
X keyboard event
    -> binding lookup
    -> command dispatch
    -> WM state/action
    -> observable X11 result
```

Do not replace this with a direct call to the final command handler unless the lower-level test is only supplemental.

Examples:

* Send keybinds with `xdotool` instead of calling command functions directly.
* Spawn tests should cover binding dispatch, `fork`/`exec`, and the resulting managed window together.
* Focus-cycle tests should exercise real keybindings and verify that ordinary focus changes do not reorder the stable client cycle.
* Geometry tests should inspect the resulting X window geometry, not only internal state.

When fixing a reproducible bug, prefer adding the smallest regression scenario that exercises the real failing path.

## Xvfb integration suite

Most deterministic behavior is tested in a headless X server.

Build first:

```sh
make
make test-tools
```

Run the full suite:

```sh
make test
```

`make test` first checks pure monitor-topology normalization and then runs the
Xvfb integration scenarios. `tests/run_xvfb.sh` runs only those Xvfb scenarios
directly after the binaries and test tools have been built.

The suite currently covers areas including:

* WM ownership and startup discovery;
* manage/unmanage and workspace visibility;
* commands and FREE/MONOCLE transitions;
* strict configuration parsing and binding validation;
* rules and fullscreen policies;
* docks, struts, workareas, and special windows;
* semantic geometry, snap, maximize, MONOCLE/fullscreen nesting, and
  ConfigureRequest geometry ownership/idempotence;
* interactive mouse move/resize and snap preview;
* NumLock-insensitive bindings;
* duplicate-KeySym/all-KeyCode grabs and runtime keyboard-map rebuilds;
* ICCCM size hints and focus protocol;
* runtime ICCCM/EWMH property-cache refresh without reactive focus/layout side effects;
* stable client-order focus cycling, focus-stack restoration, and urgency;
* MONOCLE tab-bar behavior;
* restart behavior, including cold-start-only background/autostart semantics;
* direct `spawn`, shell-backed `spawn-shell`, and SIGCHLD inheritance behavior.

Individual scenarios can be run directly, for example:

```sh
tests/xvfb_mouse.sh
tests/xvfb_spawn.sh
```

The scripts use `BOX2430_TEST_DISPLAY` when an explicit unused display number is needed:

```sh
BOX2430_TEST_DISPLAY=:150 tests/xvfb_spawn.sh
```

A different WM binary can be supplied with `BOX2430_BIN`:

```sh
BOX2430_BIN=./build/sanitize/box2430 tests/run_xvfb.sh
```

This is the normal way to run the integration suite against the sanitizer build.

## Xephyr tests

Use Xephyr when nested visual behavior, Xinerama layout, or topology changes matter.

Available scenarios include:

```sh
tests/xephyr_visual.sh
tests/xephyr_multimon.sh
tests/xephyr_topology.sh
```

They cover behavior such as:

* FREE overlap and border presentation;
* snap preview, snap, maximize, and fullscreen;
* per-monitor workspaces and monitor movement;
* cross-monitor drag;
* Xinerama monitor selection;
* topology reconciliation;
* MONOCLE tab rendering, including non-ASCII titles.

Some Xephyr scenarios capture evidence under:

```text
build/evidence/
```

They require access to a working host X display. If Xephyr cannot connect to the host display, use Xvfb for non-visual verification and treat the Xephyr-specific result as not verified in that environment.

## X server restrictions in sandboxes

Xvfb and Xephyr should be run outside restrictive filesystem/process sandboxes when the sandbox prevents X server socket creation.

Known symptoms include:

```text
Xvfb did not start
Cannot establish any listening sockets
Owner of /tmp/.X11-unix should be set to root
```

When these errors occur in a sandbox, do not repeatedly change display numbers or modify Box2430 code. Re-run the same test outside the sandbox with an unused display number.

Compilation, static inspection, and tests that do not start an X server may still be run inside the sandbox.

## Sanitizers

`make sanitize` enables AddressSanitizer and UndefinedBehaviorSanitizer.

For substantial behavior changes, a useful verification sequence is:

```sh
make clean
make sanitize
make test-tools
BOX2430_BIN=./build/sanitize/box2430 tests/run_xvfb.sh
```

Run relevant Xephyr scenarios with the sanitizer binary as well when the change affects multi-monitor or visual behavior.

If GCC's sanitizer runtime is unavailable on a machine, using Clang is preferred over weakening the checks.

LeakSanitizer-clean process teardown is not treated as a universal requirement because external X11/font libraries may retain process-lifetime allocations.

## Valgrind and the Xft/fontconfig baseline

A minimal Xft-only program, without Box2430 code, has been observed to reproduce the same process-exit Valgrind allocation seen when starting Box2430:

```text
320 bytes total
256 bytes direct + 64 bytes indirect
allocation path: libfontconfig -> libexpat
```

Treat this specific matching result as external library/process-teardown behavior. Do not add Box2430 cleanup workarounds or change product behavior merely to suppress it.

This does **not** make arbitrary Valgrind findings acceptable. Investigate any:

* invalid read or write;
* use of uninitialized data;
* allocation whose stack includes Box2430 code;
* leak with a different size or allocation path;
* leak that grows repeatedly during normal WM operations.

When uncertain, compare the finding with a minimal Xft program in the same environment.

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

For event-order or lifetime bugs, preserve the real asynchronous X11 sequence before changing implementation details. A disappearing client may legitimately produce an ordinary `BadWindow` race; distinguish that from incorrect Box2430 bookkeeping or an unexpected non-`BadWindow` X error.

When a test fails, inspect the WM/X-server logs produced by the scenario before reducing the test to an internal helper call.

## Adding or changing tests

Choose the narrowest existing scenario that matches the behavior being changed.

Prefer:

* extending an existing `xvfb_*.sh` scenario for deterministic X11 behavior;
* adding a new Xvfb scenario when the behavior forms a distinct regression boundary;
* using Xephyr only when visual output, topology, or nested multi-monitor behavior is essential;
* adding a small fixture client under `tests/` when an exact ICCCM/EWMH behavior cannot be produced reliably with `xterm` or `xdotool`.

Test fixture configuration belongs under:

```text
tests/fixtures/
```

A regression test should fail for the original bug and pass because of the intended fix, not because of unrelated timing or environment assumptions.

## Runtime economy checks

A lightweight runtime baseline can be measured with:

```sh
make release
tests/measure_economy.sh
```

The script reports idle RSS, RSS with a configurable number of xterms, and idle CPU usage under Xvfb.

Use it to notice major regressions, not as a fixed release gate tied to historical machine-specific numbers.

The client count can be changed with:

```sh
BOX2430_CLIENT_COUNT=20 tests/measure_economy.sh
```

## Real-session smoke testing

Xvfb and Xephyr provide most automated coverage, but changes involving real input devices, panels, drivers, X.Org/XLibre differences, or physical multi-monitor behavior may still deserve a real-session smoke test.

Do this from a disposable X session or spare VT. Do not replace a user's active window manager from an automated test or coding-agent session.

A convenient dedicated startup file is:

```sh
printf '%s\n' 'xterm &' 'exec /path/to/box2430 -c /path/to/config.toml' > ~/.xinitrc-box2430
```

Then start it manually on an unused display using the host system's normal X.Org or XLibre `xinit`/`startx` procedure.

Before testing, configure an explicit `wm quit` binding so the session can be exited cleanly.

For a broad smoke test, check the behaviors touched by the change, plus basic startup, window management, focus, workspace switching, FREE/MONOCLE, move/resize, and clean WM exit.

## Verification reporting

Only report a test as passing if it was actually executed successfully in the current environment.

Distinguish clearly between:

* **PASS** — executed and succeeded;
* **FAIL** — executed and failed;
* **UNVERIFIED** — could not be executed because of an environment limitation;
* **USER HANDOFF** — intentionally requires a real user-controlled X session.

Do not convert an unavailable X server, missing Xephyr host display, or sandbox socket restriction into a claim about Box2430 correctness.
