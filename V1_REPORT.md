# Microbox V1 Delivery Report

Date: 2026-08-20

## Result

`MICROBOX V1 IMPLEMENTATION COMPLETE`

Microbox is a non-reparenting, Xlib-based stacking window manager implementing
the frozen V1 product, state, architecture, interaction, command, configuration,
and implementation-economy contracts. All verification executable in this
environment passes. Real X.Org and XLibre session checks remain intentional
user handoffs.

## Repository Structure

- `src/main.c`: process arguments, lifecycle, and restart exec
- `src/wm.c`: core WM state, event loop, focus/orders, geometry, input, rules,
  special windows, tab bar, and monitor reconciliation
- `src/command.c`: the argv-like Command Registry and context validation
- `src/config.c`: safe defaults and strict, whole-config-atomic TOML loading
- `src/x11.c`: thin ICCCM/EWMH and X11 property boundary
- `src/microbox.h`: transparent shared core types and internal boundaries
- `vendor/tomlc17/`: isolated selected TOML vendor
- `tests/`: X11 fixture clients, Xvfb integration scenarios, Xephyr scenarios,
  fixtures, and repeatable economy measurement
- `config.example.toml`: validated full V1 example configuration

## Builds Actually Executed

| Command | Status |
|---|---|
| `make` | PASS |
| `make release` | PASS |
| `make sanitize` | PASS |
| `make test-tools` | PASS |
| staged `make install` with a temporary `DESTDIR` | PASS |

The developer and release builds use GCC. The sanitizer build uses Clang with
AddressSanitizer and UndefinedBehaviorSanitizer.

## Scenarios Actually Executed

The final sanitizer-backed `tests/run_xvfb.sh` run passed every scenario:

- startup discovery, WM ownership exclusion, manage/unmanage, click focus, and
  workspace visibility/restore
- command/state transitions, FREE/MONOCLE geometry, stacking, snap, maximize,
  user fullscreen, fake client fullscreen, moves, unbind, and WM_DELETE_WINDOW
- strict schema, whole-config fallback, invalid command/context rejection,
  binding inheritance, configured effects, and `config.example.toml`
- ordered one-shot rules, hidden destinations, normal/dialog placement, border,
  and allow/fake/deny fullscreen policies
- Desktop/Dock/Notification behavior, struts/workarea, and fixed stacking tiers
- explicit mouse move/resize, snap preview/commit, restore, and MONOCLE no-op
- held-modifier MRU snapshot cycle
- Xft MONOCLE tab bar rendering and mouse/wheel interaction
- process restart and startup config reload
- click/sloppy focus, root retention, urgency border/tab propagation
- absent versus established workspace focus history
- ICCCM `WM_TAKE_FOCUS` and `WM_HINTS` input behavior

Sanitizer-backed Xephyr runs also passed:

- `tests/xephyr_visual.sh`: FREE overlap/borders, mouse snap outline, committed
  snap, maximize, and fullscreen
- `tests/xephyr_topology.sh`: topology reconciliation and active/inactive/urgent
  Xft tabs, including CJK fallback rendering
- `tests/xephyr_multimon.sh`: Xinerama monitor selection, independent local
  workspaces, move flags, physical geometry translation, and cross-monitor drag

Visual evidence is under `build/evidence/`, including
`xephyr-montage.png`, `xephyr-topology.png`, and `xephyr-multimon.png`.

## Acceptance Matrix

| Area | Status | Evidence |
|---|---|---|
| Build: developer | PASS | `make` |
| Build: release | PASS | `make release` |
| Build: sanitize | PASS | `make sanitize` |
| WM startup / ownership | PASS | `xvfb_bootstrap.sh` |
| Manage / unmanage | PASS | `xvfb_bootstrap.sh` |
| Per-monitor workspaces | PASS | `xephyr_multimon.sh` |
| selected_monitor | PASS | `xephyr_multimon.sh` |
| FREE mode | PASS | `xvfb_core_commands.sh`, `xephyr_visual.sh` |
| Click focus | PASS | `xvfb_bootstrap.sh` |
| Sloppy focus | PASS | `xvfb_focus_urgency.sh` |
| Workspace focus restore | PASS | `xvfb_bootstrap.sh`, `xvfb_focus_history.sh` |
| Tab order | PASS | `xvfb_core_commands.sh`, `xvfb_tabbar.sh` |
| MRU snapshot cycle | PASS | `xvfb_mru.sh` |
| Stacking order | PASS | `xvfb_core_commands.sh`, `xvfb_special_windows.sh` |
| Move workspace | PASS | `xvfb_core_commands.sh` |
| Move monitor | PASS | `xephyr_multimon.sh` |
| `--follow` | PASS | `xephyr_multimon.sh` |
| `--keep-workspace` | PASS | `xephyr_multimon.sh` |
| Mouse move | PASS | `xvfb_mouse.sh` |
| Mouse resize | PASS | `xvfb_mouse.sh` |
| Cross-monitor drag | PASS | `xephyr_multimon.sh` |
| Keyboard snap | PASS | `xvfb_core_commands.sh` |
| Mouse snap preview/commit | PASS | `xvfb_mouse.sh`, `xephyr_visual.sh` |
| Maximize / restore | PASS | `xvfb_core_commands.sh`, `xephyr_visual.sh` |
| MONOCLE | PASS | `xvfb_core_commands.sh`, `xvfb_tabbar.sh` |
| MONOCLE geometry no-op | PASS | `xvfb_core_commands.sh`, `xvfb_mouse.sh` |
| Tab Bar interaction | PASS | `xvfb_tabbar.sh` |
| User fullscreen | PASS | `xvfb_core_commands.sh`, `xephyr_visual.sh` |
| Client fullscreen allow | PASS | `xvfb_rules.sh` |
| Client fullscreen fake | PASS | `xvfb_core_commands.sh` |
| Client fullscreen deny | PASS | `xvfb_config.sh`, `xvfb_rules.sh` |
| Window rules | PASS | `xvfb_rules.sh` |
| Hidden rule destination | PASS | `xvfb_rules.sh` |
| Config strict schema | PASS | `xvfb_config.sh` |
| Config atomic fallback | PASS | `xvfb_config.sh` |
| Binding override / unbind | PASS | `xvfb_config.sh`, `xvfb_core_commands.sh` |
| Command context validation | PASS | `xvfb_config.sh` |
| ICCCM practical subset | PASS | bootstrap/core/rules/focus protocol scenarios |
| EWMH practical subset | PASS | bootstrap/core/rules/special-window scenarios |
| Urgency | PASS | `xvfb_focus_urgency.sh`, `xephyr_topology.sh` |
| Dock / strut / workarea | PASS | `xvfb_special_windows.sh` |
| Minimal stacking precedence | PASS | `xvfb_special_windows.sh` |
| Monitor topology reconciliation | PASS | `xephyr_topology.sh` |
| Xvfb integration suite | PASS | final sanitizer-backed `tests/run_xvfb.sh` |
| Xephyr visual scenarios | PASS | all three Xephyr scripts and screenshots |
| ASan / UBSan | PASS | full Xvfb and all Xephyr scenarios |
| Owned LOC baseline | PASS | 3,888 physical lines in `src/*.c` + `src/*.h` |
| Build-time baseline | PASS | clean profile 1.85 s; one-file rebuild 0.17 s |
| RSS / idle CPU baseline | PASS | `tests/measure_economy.sh` |
| Binary-size baseline | PASS | stripped release 118,800 bytes |
| X.Org real-session smoke test | USER HANDOFF | procedure below |
| XLibre real-session smoke test | USER HANDOFF | procedure below |

## Implementation Economy Baseline

Measurement environment:

- architecture: x86_64 Linux
- compiler: GCC 14.2.1 for the measured `-O2 -DNDEBUG` release profile
- direct linked API surface: libc, libX11, libXinerama, libXft
- X server: Xvfb, 800x600x24
- clients: zero for idle measurements; 10 xterms for loaded RSS
- RSS source: `/proc/<pid>/status` `VmRSS`
- CPU method: process user+system ticks over a two-second idle interval
- LOC method: physical lines in Microbox-owned `src/*.c` and `src/*.h`

Results:

| Metric | Baseline |
|---|---:|
| Microbox-owned production LOC | 3,888 lines |
| Clean release-profile build | 1.85 s |
| One-file compile + relink | 0.17 s |
| Idle RSS | 10,584 KiB |
| RSS with 10 xterms | 10,588 KiB |
| Idle CPU | 0.000% |
| Stripped release binary | 118,800 bytes |

The stripped artifact used for measurement is
`build/evidence/microbox.stripped`; the normal release binary remains unstripped.

## Known Limitations and UNVERIFIED Items

There are no known unresolved V1 correctness failures and no environment-caused
`UNVERIFIED` rows.

Intentional V1/non-goal limitations:

- Per-monitor local workspaces are not projected as misleading global EWMH
  desktops.
- Microbox does not configure outputs or persist physical monitor identity.
- It does not include Post-V1 IPC, bars, compositing, minimize, scratchpads,
  session restore, or a general stacking-layer framework.
- Tab glyph availability follows fonts installed on the target system; the Xft
  renderer uses configured primary fonts plus common CJK fallback patterns.

Real X.Org and XLibre checks are `USER HANDOFF`, not `UNVERIFIED`, because the
frozen safety contract intentionally assigns real-session takeover to the user.

## Real-Session Smoke-Test Handoff

Do this from a spare text VT after saving work and ending any graphical session
that would conflict with the test display. Do not replace a live desktop WM.

1. Build and stage the release:

   ```sh
   cd /home/novel2430/src/microbox
   make release
   mkdir -p "$HOME/.local/bin" "$HOME/.config/microbox"
   install -m 0755 build/release/microbox "$HOME/.local/bin/microbox"
   install -m 0644 config.example.toml "$HOME/.config/microbox/config.toml"
   ```

2. For a convenient clean exit, add this line under `[bindings.keys]` in the
   copied config:

   ```toml
   "Super+Shift+Escape" = "wm quit"
   ```

3. Create a dedicated test startup file without replacing the normal
   `~/.xinitrc`:

   ```sh
   printf '%s\n' 'xterm &' 'exec "$HOME/.local/bin/microbox" -c "$HOME/.config/microbox/config.toml"' > "$HOME/.xinitrc-microbox"
   chmod 0700 "$HOME/.xinitrc-microbox"
   ```

4. X.Org: from the spare VT, use the distro's normal `startx`/`xinit` command
   with that dedicated startup file and an unused display, for example:

   ```sh
   startx "$HOME/.xinitrc-microbox" -- :1
   ```

5. XLibre: select the distro's XLibre server entrypoint with `xinit`, using the
   same startup file and another unused display. The exact server executable
   path is distribution-specific; confirm it with the package's installed-file
   list, then run:

   ```sh
   microbox_xlibre_server=/absolute/path/to/the/XLibre-server-binary
   xinit "$HOME/.xinitrc-microbox" -- "$microbox_xlibre_server" :2
   ```

6. On each server verify: initial xterm management and focused border; open a
   second terminal; click and sloppy focus as configured; workspace switch and
   restore; FREE overlap; MONOCLE and tab input; Alt-Tab snapshot cycling;
   keyboard and mouse snap; maximize/restore; fullscreen/restore; monitor
   navigation and cross-monitor move/drag if multiple Xinerama heads are
   present; Dock/workarea behavior if a panel is available; then exit with
   `Super+Shift+Escape`.

No Post-V1 TODO is required for this V1 delivery.
