# Box2430 Architecture

This document describes the architecture of the current Box2430 implementation. It is a guide to how the window manager is structured and how its major state transitions work; it is not a separate design contract.

## Overview

Box2430 is a small, single-threaded, non-reparenting X11 stacking window manager built directly on Xlib.

The implementation keeps most window-management behavior in one place rather than splitting it across a large framework:

| File            | Responsibility                                                                                           |
| --------------- | -------------------------------------------------------------------------------------------------------- |
| `src/main.c`    | CLI parsing, session-start detection, WM lifecycle, and restart                                          |
| `src/wm.c`      | Core state, event handling, focus, geometry, workspaces, monitors, tabs, snapping, and window management |
| `src/command.c` | Command validation and dispatch, including process spawning                                                       |
| `src/config.c`  | Built-in defaults and strict TOML configuration loading                                                  |
| `src/x11.c`     | X11 ownership, atoms, window metadata, struts, and EWMH/ICCCM-facing helpers                             |
| `src/box2430.h` | Shared data structures and public internal interfaces                                                    |

There is no frame-window hierarchy around managed clients. Box2430 manages the client windows themselves and adds only its own override-redirect helper windows, such as tab bars and snap previews.

## Core Concepts

A few X11 and window-manager terms are easy to confuse. These distinctions are used throughout the codebase.

### Manage, map, and visible

**Managing** a window means Box2430 has created a `Client` or `SpecialWindow` record for it and has taken responsibility for its WM behavior.

**Mapping** is an X11 operation that makes a window eligible to become viewable. An application normally calls `XMapWindow`; because Box2430 owns `SubstructureRedirectMask` on the root window, a new top-level client produces a `MapRequest` that the WM handles first.

In configuration names such as `focus_on_map` and `raise_on_map`, **map refers to this X11 window-mapping event**, not to a map/dictionary data structure.

Managed and mapped are not the same as currently visible. A client may remain fully managed while Box2430 unmaps it because its workspace is inactive. When that workspace becomes active, Box2430 maps it again without treating it as a new client.

At startup, an unmapped top-level window left in ICCCM `IconicState` by a
previous window manager is also eligible for discovery. `IconicState` is only
a discovery signal: after adoption the window enters the ordinary Box2430
client and workspace model.

Workspace hiding is not minimization. Box2430 does not expose a minimize state.
Hiding an inactive workspace unmaps its clients while they remain managed and
keeps their adopted `WM_STATE` as `NormalState`; `IconicState` is not used as a
workspace-visibility state.

This gives three useful questions:

```text
Is it managed?   -> Does Box2430 own client state for it?
Is it mapped?    -> Is the X window currently mapped?
Is it visible?   -> Is it on an active workspace and not otherwise hidden?
```

### Focus and raise

**Focus** controls which client receives keyboard input and is considered the active client. Focusing also updates Box2430 state such as `focused_client`, the workspace's focus stack, urgency, focused border, and `_NET_ACTIVE_WINDOW`. Focus does not reorder the workspace's stable client order.

**Raise** changes stacking order: it moves a client toward the top of the ordinary client stack. It does not by itself give that client keyboard focus.

The two operations are intentionally independent in FREE mode. For example, with the defaults:

```toml
raise_on_focus = false
focus_on_map = true
raise_on_map = true
```

when a new visible client is mapped, Box2430 normally both raises it and focuses it. Later focusing another existing client does **not** automatically raise that client because `raise_on_focus` is false.

The three options therefore mean:

* `focus_on_map`: focus a newly managed client when it is mapped onto a currently visible workspace;
* `raise_on_map`: place a newly managed client at the top of its workspace stack;
* `raise_on_focus`: whenever normal focus changes to a client, also raise it.

MONOCLE is a deliberate exception to strict focus/raise independence. MONOCLE clients overlap in the same content area, so tab or focus navigation raises the selected client to make it the visible one even when global `raise_on_focus` is disabled.

### Unmap and withdraw

Box2430 sometimes calls `XUnmapWindow` itself, most notably when hiding clients on an inactive workspace. This is a presentation operation; the client remains managed.

Applications can also unmap their own top-level windows as part of withdrawing them from WM management. `ignored_unmaps` lets Box2430 distinguish WM-generated unmaps from client-generated withdrawal so that hiding a workspace does not accidentally destroy its client bookkeeping.

### Selected monitor and focused client

`selected_monitor` and `focused_client` answer different questions.

`focused_client` is the client currently receiving WM focus. `selected_monitor` is the monitor targeted by monitor/workspace-oriented commands. Focusing a client selects that client's monitor, but Box2430 can also select a monitor explicitly even when focus does not move to a client there.

This distinction allows commands such as workspace switching to operate per monitor rather than through one global desktop state.

## Core State Model

The top-level `WM` object owns the complete runtime state:

```text
WM
├── Monitor[]
│   └── Workspace[]
│       └── Client membership and ordering
├── global Client list
├── SpecialWindow list
├── focused Client
├── selected Monitor
└── drag state
```

### Monitors and workspaces

Each `Monitor` owns its own array of workspaces:

```text
Monitor
├── geometry
├── workarea
├── workspaces[]
├── active_workspace
└── tab_bar
```

Workspaces are therefore **per-monitor**, not global virtual desktops. Monitor 1 workspace 2 and monitor 2 workspace 2 are separate `Workspace` objects.

Switching a workspace only changes the active workspace of that monitor. Other monitors keep their own active workspaces.

`selected_monitor` is the monitor targeted by workspace, mode, and focus commands when no client-specific destination is involved. Focusing a client also selects that client's monitor.

At steady state, clients on inactive workspaces are genuinely unmapped. During
activation, Box2430 first prepares all incoming geometry while it is hidden,
then makes the incoming workspace active, maps and focuses it, and only then
unmaps the outgoing clients. Both workspaces may therefore be mapped during a
short controlled overlap. Active-workspace stacking rules ignore outgoing
clients during that overlap so they cannot be raised back above the incoming
workspace.

### Clients

A normal or dialog window is represented by `Client`.

Every client belongs to exactly one workspace and also appears in the WM-wide client list. The global list is primarily ownership/discovery state; workspace-local lists determine user-visible ordering.

Important client state includes:

```text
workspace
geometry
normal_geometry
snap_state
maximized
fullscreen
user_fullscreen
client_fullscreen
urgent
accepts_input
takes_focus
```

`geometry` and `normal_geometry` should not be treated as interchangeable.

`normal_geometry` is the restore point used when leaving snap or maximize. MONOCLE and real fullscreen are presentation states: they may resize the actual X window without replacing the underlying normal workspace geometry.

## Workspace Ordering

Each workspace keeps several independent views of the same clients.

### Membership order

`workspace->clients` is a simple membership list. It is useful for operations that must visit every client in the workspace, such as rematerializing geometry.

### Stable client / tab order

`tab_head` / `tab_tail` define the workspace's canonical client order. The same order is rendered as tabs in MONOCLE mode, but it also exists and is used in FREE mode.

New clients are appended to the end. Focusing or raising a client does not reorder this list.

`focus next` / `focus prev` cycle through this order with wraparound, skipping clients that cannot receive focus. Focus history does not reorder this list.

### Stack order

`stack_head` / `stack_tail` define bottom-to-top stacking order for ordinary clients in a workspace.

Raise and lower operations modify this list. Focus and stacking are intentionally separate unless `raise_on_focus` or a specific operation explicitly raises a client.

Stable client/tab order and stack order are independent: focus cycling does not change stacking, and raise/lower operations do not change focus-cycle order.

At the root-window level, Box2430 keeps its known tiers ordered with sibling-relative stacking: ordinary clients stay below native bar/tray/tab UI, managed special windows stay above native UI, and real fullscreen clients stay above those known tiers. The native UI is not repeatedly raised to the absolute top of the root stack. This lets unrelated override-redirect overlays, such as notification windows, remain above the native UI after they map without Box2430 adopting them as managed clients.

### Focus order

`focus_head` / `focus_tail` define most-recently-focused to least-recently-focused order for clients that have actually received focus in the workspace. This is a separate interaction stack; it is not X stacking order and it does not affect `focus next` / `focus prev`.

A newly managed or moved client is not added merely because it exists in the workspace. `focus_client()` promotes a client to `focus_head` when Box2430 performs a real semantic focus transition. Removing or moving a client detaches it from this order.

## Focus Model

`wm->focused_client` is the globally focused managed client. Each workspace keeps the independent focus order described above so focus can be restored by recency without coupling focus to X stacking.

A client is focusable when it either accepts normal X input focus or supports `WM_TAKE_FOCUS`.

On focus, Box2430 may:

1. update `focused_client`;
2. select the client's monitor;
3. promote the client to the workspace's `focus_head`;
4. clear urgency;
5. call `XSetInputFocus` when appropriate;
6. send `WM_TAKE_FOCUS` when supported;
7. update `_NET_ACTIVE_WINDOW`;
8. redraw tab bars;
9. optionally raise the client.

Click focus uses passive button grabs so Box2430 can focus the window and then replay an unmatched click to the client. Sloppy focus uses `EnterNotify`.

In click mode only unfocused clients carry the catch-all first-click grab;
focused clients and sloppy-focus clients retain only explicit WM mouse-binding
grabs. `FocusIn` conflicts from clients that acquire X input focus directly are
corrected back to Box2430's semantic focused client. Sloppy crossing events are
filtered, and stacking synchronization discards stale `EnterNotify` events
created by internal restacking.

Client `_NET_ACTIVE_WINDOW` requests default to marking an unfocused client
urgent instead of allowing focus stealing. The `[focus].active_window` policy
can be set to `"focus"` to retain direct focusing for visible, focusable clients.

When a focused client disappears or leaves a workspace, Box2430 first restores
the most recently focused remaining client from the workspace focus order.
Workspace and monitor activation use the same focus-order head. If no remaining
client has focus history, Box2430 falls back to stable client order: removal
prefers the next focusable client and then a previous one, while activation
starts from the newest (`tab_tail`) end. Root focus is used only when no
focusable client exists.

## FREE and MONOCLE

Every workspace has one of two modes.

### FREE

FREE mode uses ordinary stacking-window geometry.

Clients can be moved, resized, snapped, maximized, and raised/lowered independently.

### MONOCLE

In MONOCLE mode, all clients in the active workspace are materialized into the monitor's MONOCLE content area.

The content area is:

```text
monitor workarea
minus the tab bar at the top
```

when tabs are enabled and there is enough vertical space.

The clients are not converted into tiles and are not removed from their normal workspace state. Leaving MONOCLE restores the appropriate underlying FREE presentation.

All MONOCLE clients remain managed in normal stack order. The focused/tab-selected client is raised, making stacking determine which client is visible on top.

The tab bar is one override-redirect X window per monitor and is only mapped when that monitor's active workspace is in MONOCLE mode.

## Geometry and Presentation State

A monitor has two rectangles:

* `geometry`: the physical Xinerama monitor rectangle;
* `workarea`: geometry after dock struts are removed.

Most ordinary placement, snap, and maximize operations use the workarea.

Real fullscreen uses the full monitor geometry and removes the client border.

For normal clients, `normal_geometry` is the FREE rectangle preserved before
snap/maximize, while `geometry` is the current semantic rectangle for the
FREE/snap/maximize layer. `commit_client_geometry()` updates that semantic
rectangle and presents it to X11. `present_client_geometry()` only changes the
X11 presentation and is used for temporary MONOCLE/fullscreen geometry so those
states cannot overwrite the geometry needed when they unwind.

The presentation priority in `materialize_client_geometry()` is effectively:

```text
real fullscreen
    >
MONOCLE
    >
maximized
    >
snap
    >
stored geometry
```

This ordering matters when multiple pieces of state exist at the same time.

### Snap and maximize

Snap targets are computed from the monitor workarea. Side and corner sizes come from configuration ratios.

Entering snap or maximize preserves `normal_geometry` when needed. Clearing the state restores that geometry.

EWMH maximize requests reuse this same full-workarea maximize state. Box2430 does not
introduce independent horizontal/vertical maximize modes: a request involving either
`_NET_WM_STATE_MAXIMIZED_HORZ` or `_NET_WM_STATE_MAXIMIZED_VERT` maps to the
single existing maximize state, and an active maximize is advertised with both atoms.

Interactive movement or resizing first leaves snap/maximize and returns the client to normal geometry.

### ICCCM size constraints

For ordinary FREE geometry, Box2430 applies the client's `WM_NORMAL_HINTS`
base size, minimum/maximum size, resize increments, and aspect constraints.
Parsed hints are cached on `Client`; a `WM_NORMAL_HINTS` `PropertyNotify`
invalidates that cache, and the next initial/configure/interactive resize
calculation refreshes it lazily.

Client-requested initial placement uses the final monitor's workarea for a
minimal operability check. A window whose outer frame is entirely outside an
edge is moved back to that edge, while a window that still intersects the
workarea is left at its requested position. This deliberately allows ordinary
FREE windows to remain partially off-screen or overlap screen-edge UI.

For managed FREE clients, `ConfigureRequest` commits only requested geometry
fields and applies size hints when width/height are requested. Managed border
width is WM-owned: `CWBorderWidth` does not change Box2430's border policy.
When snap, maximize, MONOCLE, or real fullscreen owns the presentation, client
geometry requests are acknowledged with the actual presented geometry without
changing the underlying semantic/restore state.

This managed-client policy applies only to ordinary `Client` objects. If a
`ConfigureRequest` does not belong to an ordinary client — including requests
from `SpecialWindow` dock/desktop/notification windows and otherwise unmanaged
windows — Box2430 passes the request's original `value_mask` through to
`XConfigureWindow`, including sibling and stacking fields. This is an intentional
compatibility boundary; do not generalize ordinary-client geometry/stacking
ownership to special windows without a concrete reason.

### Fullscreen

Box2430 distinguishes three concepts:

* `user_fullscreen`: fullscreen requested through Box2430;
* `client_fullscreen`: fullscreen requested by the application through EWMH;
* `fullscreen`: whether real borderless fullscreen is currently materialized.

Application fullscreen is controlled by the configured per-client policy:

* `allow`: honor the request as real fullscreen;
* `fake`: expose fullscreen state without giving the client real fullscreen geometry;
* `deny`: reject the client request.

User fullscreen always requests real fullscreen.

## Workareas and Special Windows

Dock, desktop, and notification windows are represented as `SpecialWindow`, not normal `Client` objects.

They do not belong to workspaces and do not participate in normal focus, client/tab ordering, or client geometry behavior.

Dock windows may provide `_NET_WM_STRUT` or `_NET_WM_STRUT_PARTIAL`. Box2430 recomputes each monitor's workarea from these struts and rematerializes affected clients.

The high-level stacking policy is:

```text
desktop windows
ordinary clients
MONOCLE tab bars
dock/notification special windows
real fullscreen clients
```

Within ordinary clients, workspace stack order is preserved.

## Session Startup and Restart

`main.c` distinguishes a fresh session start from Box2430's own `wm restart`.
Restart is carried across `exec` with a private environment marker that the new
process consumes and immediately removes. This keeps restart state out of
configuration and prevents it from leaking into later spawned applications.

On a fresh session start, `wm_init()` sets and clears the X root window once with
`appearance.background`. Box2430 does not retain wallpaper ownership or repaint
the root later. A wallpaper program can replace the background normally. A WM
restart skips this root repaint.

After initialization and startup window discovery are complete, `wm_run()`
installs its signal policy and, when requested by `-a`/`--autostart`, forks one
autostart executable before entering the event loop. The child uses the same
process-launch hygiene as binding-driven spawn: it closes the inherited X
connection, starts a new session, and restores default `SIGCHLD` behavior before
`exec`. Unlike `spawn`, autostart uses the supplied path directly and is not
rerun by `wm restart`.

## Window Management Lifecycle

Startup discovery first adopts special windows, then ordinary clients, and
finally dialogs/transients. This establishes dock workareas before initial
client placement and makes an already-managed transient parent available as
the child's default monitor/workspace affinity. Explicit rules still override
that default.

When a new window is managed, Box2430:

1. ignores override-redirect and InputOnly windows;
2. reads its type, title, class/instance, transient relationship, and attributes;
3. separates dock/desktop/notification windows into the special-window path;
4. computes initial policy from global configuration and matching rules;
5. chooses a monitor and workspace;
6. computes initial geometry and border policy;
7. inserts the client into workspace membership, stable client/tab order, and stack order;
8. selects X11 events and installs mouse grabs;
9. reads focus hints and focus-related WM protocols;
10. maps the window if its workspace is visible;
11. applies raise/focus-on-map policy;
12. applies any initial client fullscreen request.

Rules are evaluated when the window is first managed. Later property changes
refresh the cached facts Box2430 actually keeps, but cache invalidation is not
a reactive policy engine. In particular, runtime `WM_TRANSIENT_FOR` and
`_NET_WM_WINDOW_TYPE` changes update relationship/type metadata without by
themselves changing focus, workspace/monitor ownership, geometry, or stacking.
Later title/class changes likewise update metadata without rerunning the initial
rule-placement process. `WM_HINTS`, `WM_PROTOCOLS`, and `WM_NORMAL_HINTS` have
their own invalidation paths so focusability and size constraints do not remain
stuck at manage-time values. Refreshing focus capability does not itself move
semantic focus; the refreshed capability is used the next time Box2430 makes
an explicit focus decision.

Unmanaging reverses workspace/global membership, chooses a focus fallback when
necessary, and, for a still-existing window, stops client event selection,
restores the application's original border, removes passive button grabs, and
sets `WM_STATE` to `WithdrawnState` while the X server is grabbed. Destroyed
windows skip those X11 requests.

On shutdown or restart Box2430 remaps all remaining clients before relinquishing
them. At that boundary there is no longer an active/inactive workspace owner;
mapping the windows lets a successor WM discover them normally without
persisting Box2430-private workspace state. During normal operation, inactive
workspaces continue to use genuine unmapping.

## Multi-Monitor Behavior

Monitor discovery uses Xinerama. Raw screen records are normalized before they
reach Box2430 monitor state: geometries with identical x, y, width, and height
are collapsed to their first occurrence, while partially overlapping monitors
remain distinct. If Xinerama is unavailable or inactive, its query returns no
records, temporary normalization storage cannot be allocated, or the unique
topology exceeds Box2430's monitor capacity, the root screen is treated as one
monitor.

Root `ConfigureNotify` events trigger topology reconciliation.

Xinerama enumeration position is not treated as durable monitor identity.
Before changing monitor state, Box2430 matches the old logical monitors to the
new normalized rectangles. Exact rectangle matches are preferred; remaining
rectangles are paired by strongest geometric continuity (overlap first, then
nearest centers) with deterministic index tie-breaking. The resulting logical
monitor state is then placed in the new Xinerama ordering. This means a pure
enumeration reorder does not exchange monitor-local active workspaces, client
membership, or focus history merely because the array order changed.

This is deliberately **logical geometry continuity**, not physical display
identity. Xinerama does not provide connector names, CRTC identity, or EDID.
For an ambiguous sequence such as unplugging the old left display while the old
right display moves into exactly the left display's rectangle, Box2430 cannot
prove which physical display survived. It applies the same deterministic
geometry matching rule instead of pretending to know physical identity.

Topology reconciliation is local plan-then-commit work. The plan determines
continued/added/removed monitors, client migration targets, latent geometry
adjustments, selected-monitor continuity, and preferred focus. Commit first
establishes the new logical monitor/workspace ownership, then computes final
Dock workareas, clamps affected latent client geometry, rematerializes normal,
snap, maximize, MONOCLE, and fullscreen presentation, reconciles workspace
mapping, and finally restores focus/stacking/EWMH state. Helpers that assume a
steady topology are therefore not called while monitor ownership is obviously
half-updated.

New monitors receive fresh workspace state. If monitors disappear, their
clients are moved deterministically to new monitor 0 using the same workspace
index. Box2430 transfers clients rather than an entire removed Workspace object;
the surviving destination monitor keeps its own active workspace and workspace
mode.

When a client keeps the same logical monitor but that monitor's origin/size
changes, or when a client migrates from a removed monitor, both `geometry` and
`normal_geometry` are translated by the old/new monitor-origin delta and then
clamped against the final destination workarea. Signed monitor coordinates are
used directly; no `x >= 0` / `y >= 0` assumption is made. WM-owned presentation
geometry is not translated blindly: fullscreen uses the new monitor rectangle,
snap/maximize use the final workarea, and MONOCLE uses the final MONOCLE content
rectangle.

If the previously focused client survives topology reconciliation, remains on
an active workspace, and is still focusable, Box2430 keeps semantic focus on
that same client. Otherwise it falls back through the selected monitor's normal
workspace focus rules.

Moving a client between monitors can either:

* use the destination monitor's active workspace; or
* preserve the source workspace number with `--keep-workspace`.

The explicit keyboard monitor-selection command also warps the pointer to the selected monitor's center.

## Mouse Drag and Snap Preview

Interactive move and resize are tracked in `wm->drag`.

At drag start:

* the client is focused;
* snap/maximize state is cleared if necessary;
* move warps the pointer to the window center;
* resize warps the pointer to the lower-right corner.

During move, the pointer position determines the candidate monitor and edge/corner snap target. The top edge away from the corners represents maximize.

Snap preview is drawn with four override-redirect windows forming an outline inside the target outer rectangle. The preview itself does not change client state.

On release, the client may first move to another monitor's active workspace and then receive the selected snap/maximize state.

## Command Path

Bindings do not call arbitrary WM internals directly.

Configuration parses a binding into command arguments and validates them against a command context:

```text
keyboard binding
mouse binding
tab-bar binding
```

At runtime the relevant X event produces a `CommandContext`, then `command_run()` dispatches the command.

Context-specific commands such as `mouse move-window` and `tab close` are rejected outside their valid input path.

`spawn`, `spawn-shell`, and session autostart share one child-process launch primitive. `spawn` searches `PATH` for the supplied argv, `spawn-shell` executes `/bin/sh -c <command>`, and autostart executes its supplied path directly. The child closes the inherited X connection file descriptor, starts a new session, and resets `SIGCHLD` to the default disposition before `exec`; the WM itself ignores `SIGCHLD` with `SA_NOCLDWAIT`.

## Event Loop

Box2430 is single-threaded.

After initialization it drains pending X events and then blocks in `poll()` on the X connection file descriptor:

```text
while running:
    process all pending X events
    poll(X connection)
```

`SIGINT` and `SIGTERM` only request termination; normal state mutation stays in the main event loop.

Important event families include:

* `MapRequest`: manage new windows;
* `ConfigureRequest`: accept normal client geometry requests unless a WM presentation state owns geometry;
* `DestroyNotify` / `UnmapNotify`: unmanage clients;
* key/button/motion events: bindings, focus, and drag;
* `EnterNotify`: sloppy focus;
* `PropertyNotify`: size-hint invalidation, focus/protocol capability refresh,
  title/class/transient/type metadata updates, and dock struts;
* `ClientMessage`: EWMH activation, close, fullscreen, and maximize requests;
* root `ConfigureNotify`: monitor reconciliation;
* `Expose`: tab-bar redraw.

`ignored_unmaps` distinguishes unmaps intentionally generated by Box2430, such as hiding an inactive workspace, from a client withdrawing itself.

## X11 Boundary

`x11.c` contains the small compatibility boundary for ICCCM/EWMH-facing operations.

The current implementation supports the parts needed by Box2430 rather than attempting to implement every desktop-manager convention. Notable state includes:

* `WM_STATE`
* `WM_DELETE_WINDOW`
* `WM_TAKE_FOCUS`
* `_NET_ACTIVE_WINDOW`
* `_NET_CLIENT_LIST`
* `_NET_CLIENT_LIST_STACKING`
* `_NET_SUPPORTING_WM_CHECK` with a `Box2430` identity window
* `_NET_WM_STATE_FULLSCREEN`
* `_NET_WM_STATE_MAXIMIZED_HORZ` / `_NET_WM_STATE_MAXIMIZED_VERT`
* `_NET_CLOSE_WINDOW`
* `_NET_WM_WINDOW_TYPE`
* `_NET_WM_STRUT` / `_NET_WM_STRUT_PARTIAL`
* `_NET_WORKAREA`

Because Box2430's workspaces are per-monitor rather than one global desktop sequence, its internal workspace model should not be assumed to map directly onto conventional EWMH desktop numbering.

WM ownership is acquired through `SubstructureRedirectMask`. A `BadAccess` during this step means another WM already owns the display.

During normal operation, ordinary `BadWindow` errors are ignored because races with disappearing X clients are expected in a window manager. Other X11 errors are logged. Internal bookkeeping correctness should be protected by clear invariants and regression tests rather than by turning expected X11 lifetime races into a large error-scoping framework.

## Architectural Invariants

A few distinctions are structural rather than incidental implementation details:

* semantic client state and temporary X presentation are not the same thing;
* monitor geometry and workarea are distinct;
* workspaces are per-monitor;
* workspace membership, stable client/tab order, and stack order are distinct structures;
* focus and stacking are independent except where a mode such as MONOCLE explicitly couples them;
* WM-generated unmaps must not be confused with client withdrawal.

Changes that intentionally alter one of these properties should be treated as architectural changes rather than local refactors.

For commands and configuration, see `docs/REFERENCE.md`. For engineering principles, see `docs/IMPLEMENTATION_STYLE.md`. For build, testing, and debugging guidance, see `DEVELOPMENT.md`.
