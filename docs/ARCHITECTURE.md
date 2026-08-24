# Box2430 Architecture

This document describes the architecture of the current Box2430 implementation.
It explains the runtime state model and the major invariants behind the code; it
is not a separate frozen specification.

## Overview

Box2430 is a small, single-threaded, non-reparenting X11 stacking window manager
built directly on Xlib. Ordinary managed application windows remain their own
X11 top-level windows; Box2430 does not wrap them in frame windows.

The native UI is part of the same runtime rather than a separate panel process.
Bars, MONOCLE tab bars, snap-preview windows, and the tray owner/host are
Box2430-owned override-redirect windows. XEmbed tray icons are the notable
reparenting exception: tray protocol requires icons to be embedded into the tray
host.

The main source layout is:

| File | Responsibility |
| --- | --- |
| `src/main.c` | CLI parsing, fresh-session/restart distinction, WM lifecycle, and re-exec |
| `src/wm.c` | Main state machine: events, clients, workspaces, focus, stacking, geometry, monitor reconciliation, dragging, ICCCM/EWMH reactions |
| `src/ui.c` / `src/ui.h` | Borders, native bars, widget layout/drawing, status/clock text, MONOCLE tabs, snap preview |
| `src/tray.c` / `src/tray.h` | XEmbed system-tray selection, owner/host windows, icon lifecycle and geometry |
| `src/bspwm_compat.c` / `src/bspwm_compat.h` | Optional Polybar `internal/bspwm` wire adapter, Unix socket lifecycle, bounded clients, command parsing, and report projection |
| `src/monitor.c` | Pure geometry-first logical-monitor continuity matching and metadata comparison |
| `src/monitor_randr.c` | RandR 1.5 version preflight and owned logical-monitor/output snapshot capture |
| `src/command.c` | Command validation/dispatch and child-process launch paths |
| `src/config.c` | Built-in defaults and strict atomic TOML configuration loading |
| `src/x11.c` | WM ownership, atoms, window metadata, struts, client lists, workarea, and ICCCM/EWMH helpers |
| `src/box2430.h` | Shared structures, enums, constants, and internal interfaces |

The implementation deliberately stays close to one X11 event loop and explicit C
state transitions. See `docs/IMPLEMENTATION_STYLE.md` for the long-lived
engineering rationale.

## Transition, Authority, and Projection (TAP)

Box2430 organizes state-heavy paths using three responsibility categories:

```text
input / X observation
    -> current policy and semantic transition
    -> coherent in-memory authority
    -> ordered X11 / ICCCM / EWMH / native-UI projection
```

**Transition** means the semantic intent or protocol observation being handled,
not a required `Transaction` object. **Authority** is the coherent in-memory
state Box2430 accepts as true after that intent. **Projection** is the ordered
X11/ICCCM/EWMH/native-UI work used to make that authority observable.

This is a control-flow discipline, not a generic transaction engine, reducer, or
separate render pass. Protocol ordering remains next to the transition when it
is part of observable behavior, as it is for workspace switching and
WM-generated unmaps. Authority and projection may therefore be deliberately
interleaved inside one transition when X11 ordering is itself user-visible.

Different input paths should converge on the same semantic transition before
they mutate authoritative state. A keyboard monitor command, an exposed-root
click, and a workspace-label click must not each grow independent "repair the
focus" logic merely because they originate from different X events. Source-
specific behavior such as pointer warping belongs to projection around the
shared transition.

The main interaction transitions are monitor activation, workspace activation,
and client activation:

* **monitor activation** changes the selected monitor, preserves that monitor's
  active workspace, and resolves semantic client focus from that workspace;
* **workspace activation** selects the workspace's monitor, changes that
  monitor's active workspace when necessary, and resolves semantic client focus
  from the newly active workspace;
* **client activation** chooses a specific focusable client, aligns selected
  monitor authority with that client's monitor, updates workspace focus history,
  and clears urgency. It does not implicitly activate a hidden workspace.

Resolving focus may legitimately produce no client. `selected_monitor` remains
authoritative even when its active workspace has no focusable target, in which
case `focused_client == NULL` and X input focus is projected to the root.

X input-focus projection is a separate operation: `FocusIn` compatibility can
re-project the already chosen semantic focus without becoming another semantic
focus path.

Client movement has a smaller ownership-only boundary that changes the sole
workspace owner and all membership/stable/stack/focus links without performing
focus, mapping, geometry, EWMH, or UI work. The surrounding move or topology
transition retains responsibility for geometry and ordered projection.

The authoritative root is represented explicitly by `WMModel`. It owns the
monitor/workspace graph, global managed-client and special-window lists, selected
monitor, and semantic focused client. `WM` remains the process/runtime
coordinator around that model: configuration, X11 connection state, native-UI
resources, tray state, and transient drag state stay outside the model root.

`WMModel` is an authority boundary, not a claim that every entity below it is
backend-independent. Existing `Monitor` and `Client` objects still carry X11/UI
attachments and cached protocol metadata. Separating those attachments is not
required for the current TAP model. Pure model helpers take `WMModel *` (or
`const WMModel *`) where practical, while transitions that coordinate policy and
ordered projection continue to take the full `WM *`.

Debug builds check the in-memory model authority after startup and completed X
event handling. These checks cover monitor/workspace ownership, global and
workspace-local client ownership, list coherence, interaction coherence, and
snap/maximize exclusion. They do not query X or assert transient mappedness.

## Core X11 concepts

### Manage, map, and visible

**Managing** a window means Box2430 has adopted it into a `Client` or
`SpecialWindow` record and is responsible for the relevant WM behavior.

**Mapping** is an X11 operation. Because Box2430 owns `SubstructureRedirectMask`
on the root, an ordinary top-level application's map request is normally seen by
the WM as `MapRequest` before the window becomes visible.

A managed client does not have to remain mapped. Clients on inactive workspaces
stay managed while Box2430 unmaps them. Returning to that workspace remaps the
same clients without re-running the management pipeline.

At startup, Box2430 can also adopt an unmapped top-level window left in ICCCM
`IconicState` by a previous WM. `IconicState` is only a startup-discovery signal;
Box2430 does not implement a minimize workflow and does not use `IconicState` to
represent hidden workspaces.

Useful distinctions are therefore:

```text
managed  -> Box2430 owns runtime state for the window
mapped   -> the X window is mapped
visible  -> it belongs to an active workspace and its presentation is mapped
```

### WM-generated unmap vs. client withdrawal

Workspace hiding uses real `XUnmapWindow` calls. Applications may also unmap a
top-level window to withdraw it from WM management.

`Client.ignored_unmaps` distinguishes unmaps generated by Box2430 from genuine
withdrawal. A WM-generated unmap must not accidentally destroy client state.
When a live client is actually unmanaged/withdrawn, Box2430 restores its original
border width, releases passive button grabs, stops selecting client events, and
sets `WM_STATE` to `WithdrawnState`.

### Focus and raise

Focus and stacking are separate semantics.

**Focus** changes the client that receives keyboard focus and updates:

* `wm->model.focused_client`;
* the workspace focus-history list;
* urgency state;
* focused/unfocused/urgent border presentation;
* passive click-focus grabs;
* `_NET_ACTIVE_WINDOW`;
* the selected monitor when focus moves to a client on another monitor.

**Raise** changes the workspace's bottom-to-top stack order. Raising does not by
itself assign keyboard focus.

With the defaults, a newly mapped visible client is both focused and raised
because `focus_on_map = true` and `raise_on_map = true`, but later focusing an
existing client does not raise it because `raise_on_focus = false`.

MONOCLE intentionally couples focus selection with raising: all MONOCLE clients
occupy the same content rectangle, so the focused tab must also become the top
ordinary client to be visible.

### Selected monitor and focused client

`selected_monitor` is not just an alias for the focused client's monitor.

It is the authoritative interaction context used by monitor/workspace/mode
commands. `focused_client` is the single semantic client focus for the X11 seat.
The two are related by an invariant, not by identity: when `focused_client` is
non-NULL it belongs to the selected monitor's active workspace, but a selected
monitor may legitimately have no focused client.

* client-specific focus normally selects that client's monitor;
* monitor/workspace/mode commands use `selected_monitor` as their target;
* `monitor next|prev` can select another monitor and then focus that monitor's
  active-workspace fallback client;
* clicking exposed root background selects the monitor under the pointer without
  warping it, then applies the same active-workspace focus fallback;
* the selected monitor also determines which monitor exposes the single XEmbed
  tray.

This separation is what makes workspaces per-monitor rather than a single global
desktop index.

Common user inputs therefore normalize as follows:

| Input | Semantic transition | Authority result | Source-specific projection |
| --- | --- | --- | --- |
| `monitor next` / `monitor prev` | activate monitor | select target monitor; keep its active workspace; resolve that workspace's focus target | warp pointer to the target monitor center |
| click a workspace label | activate workspace | select that label's monitor; activate the clicked workspace if needed; resolve its focus target | keep pointer at the clicked UI location; map/unmap when the workspace changes |
| click exposed root background | activate monitor | select the monitor under the pointer; keep its active workspace; resolve that workspace's focus target | keep pointer at the click location |

If the resolved workspace has no focusable client, the authority result is still
well-defined: the monitor stays selected, `focused_client` becomes `NULL`, and
projection puts X input focus on the root. These are not three separate focus
implementations; they are different inputs into the monitor/workspace activation
transitions.

## Core state model

The top-level `WM` object is the running-process coordinator. Its `WMModel`
member makes the authoritative modeled desktop state explicit:

```text
WM
├── WMModel                     <- authoritative modeled desktop root
│   ├── Monitor[]
│   │   ├── geometry / workarea / bar_geometry
│   │   ├── Workspace[]
│   │   ├── active_workspace
│   │   ├── native bar window + Xft draw state
│   │   └── MONOCLE tab-bar window + Xft draw state
│   ├── global Client list
│   ├── SpecialWindow list
│   ├── selected Monitor
│   └── focused Client
├── Config                      <- policy / style
├── X11 connection + atoms     <- runtime / projection mechanism
├── accepted RandR snapshot    <- owned platform observation metadata
├── Tray
├── BspwmCompat                <- optional bounded interoperability runtime
├── native UI resources / cached status + clock text
└── interactive drag / snap-preview state
```

The `WMModel` boundary deliberately stops at the authoritative root. `Monitor`
and `Client` are not split into separate semantic/runtime objects, so this is not
a backend-neutral model rewrite. The boundary exists to make model ownership and
mutation visible in code without obscuring the direct C control flow.

### Polybar bspwm-module compatibility

The optional `BspwmCompat` runtime is an interoperability boundary outside
`WMModel`:

```text
Polybar internal/bspwm
    -> bspwm_compat command adapter
    -> existing monitor/workspace Transition
    -> WMModel Authority
    -> bspwm report Projection
    -> Polybar subscriber
```

It owns only Unix listener/client state, bounded wire buffers, and the last
serialized full report. It does not cache a second semantic desktop model.
Incoming monitor/workspace commands resolve targets against current authority
and the accepted RandR snapshot, then call `workspace_activate()`. Monitor focus
activates the target monitor's already-active workspace, deliberately avoiding
the pointer-warping keyboard `monitor_select()` projection.

Reports enumerate semantic monitors/workspaces in their existing order.
Workspace indices are projected as numeric `1..N` names; occupied and urgent
state comes directly from workspace membership and client urgency; FREE/MONOCLE
is projected as bspwm-compatible `LT`/`LM`. Logical-monitor names are read by
corresponding index from the accepted RandR observation and never copied into
`Monitor` authority.

Report serialization/comparison happens at coherent main-loop checkpoints after
X event batches and compatibility-command dispatch. Individual transitions do
not publish reports. A changed report is broadcast as a complete snapshot;
bounded nonblocking output coalesces later unsent snapshots without blocking the
X event loop. This is intentionally the Polybar-required bspwm subset, not a
general IPC or `bspc` control plane.

### Monitors and workspaces

Every monitor owns its own workspace array:

```text
Monitor 0                     Monitor 1
├── workspace 1              ├── workspace 1
├── workspace 2              ├── workspace 2
└── ...                      └── ...
```

The same workspace number on two monitors represents two different `Workspace`
objects. Activating workspace 2 on one monitor does not change the active
workspace of another monitor.

Each workspace stores:

* `mode`: FREE or MONOCLE;
* membership list;
* stable tab/client order;
* stack order;
* focus-history order.

These lists intentionally model different semantics and are not interchangeable.

### Clients

Every ordinary/dialog client belongs to exactly one workspace and also appears
in the WM-wide ownership list.

Important semantic client state includes:

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
border_enabled
fullscreen_policy
cached title / class / instance / type / transient relationship
cached ICCCM size hints
```

`geometry` and `normal_geometry` have different roles. `normal_geometry` is the
restore point preserved across snap/maximize. MONOCLE and real fullscreen are
temporary presentation states and must not overwrite that restore state.

## Workspace ordering

A workspace keeps several views of the same clients.

### Membership order

`workspace->clients` is a simple membership chain used when every client in the
workspace must be visited, for example during rematerialization.

### Stable client/tab order

`tab_head` / `tab_tail` are the canonical stable client order.

New clients are appended. Normal focus changes, urgency, raise/lower, and focus
history do not reorder this list.

The same order is used by:

* `focus next` / `focus prev` in both FREE and MONOCLE;
* MONOCLE tab drawing;
* keyboard `tab focus N`.

### Stack order

`stack_head` / `stack_tail` are bottom-to-top ordinary-client stack order.

`client_raise()` and `client_lower()` modify this list. Focus only modifies it
when an operation explicitly raises, such as MONOCLE tab selection or global
`raise_on_focus`.

### Focus-history order

`focus_head` / `focus_tail` are most-recently-focused to least-recently-focused
history. This exists to restore useful focus after a client disappears, a
workspace is entered, or topology changes.

It is independent from both stable client order and X stacking order.

## Focus model

A client is considered focusable if ICCCM metadata says it accepts direct input
or supports `WM_TAKE_FOCUS`.

When Box2430 gives focus:

1. update internal semantic focus;
2. update border/button-grab presentation for old/new clients;
3. select the client's monitor;
4. move the client to the head of workspace focus history;
5. clear its urgency;
6. use `XSetInputFocus` when `InputHint` permits;
7. send `WM_TAKE_FOCUS` when supported;
8. update `_NET_ACTIVE_WINDOW` and native UI;
9. optionally raise when `raise_on_focus` is enabled.

A `FocusIn` compatibility path reasserts the WM's chosen focus if an ordinary
client steals X focus behind Box2430's semantic state. It does not treat every
raw focus event as permission for the client to redefine WM focus policy.

Runtime `WM_HINTS` and `WM_PROTOCOLS` observations refresh focus capabilities
without reactively choosing a different semantic focused client. A later focus
transition uses the refreshed capability. This preserves the distinction
between metadata observation and interaction intent.

`_NET_ACTIVE_WINDOW` requests are controlled by `focus.active_window`:

* `urgent` marks a requesting non-focused client urgent;
* `focus` focuses the requesting client only when it is already on its monitor's
  active workspace.

This avoids implicitly switching workspaces in response to client activation
requests.

## FREE and MONOCLE

### FREE

FREE is normal stacking-window behavior:

* clients keep their committed semantic geometry;
* overlap is allowed;
* ordinary focus and raise are independent;
* move/resize, snap, and maximize are available;
* the FREE border style is used.

### MONOCLE

MONOCLE keeps the same workspace membership and stable client order, but changes
presentation:

* every client is presented in the monitor's MONOCLE content rectangle;
* focus/tab selection raises the selected client;
* the MONOCLE border style is used;
* an optional tab bar is created for the visible MONOCLE workspace when it has
  clients;
* the tab bar can independently occupy the top or bottom edge of the monitor
  workarea.

MONOCLE does not replace `Client.geometry`/`normal_geometry` with the temporary
full-content rectangle. Leaving MONOCLE rematerializes the client's underlying
FREE semantic state.

## Geometry model

Box2430 separates raw monitor geometry, monitor workarea, semantic client
geometry, and temporary presentation geometry.

### Monitor geometry and workarea

`Monitor.geometry` is the accepted RandR logical-monitor rectangle.

`Monitor.workarea` is computed in this order:

```text
raw monitor geometry
    -> subtract matching external Dock struts
    -> reserve native bar edge when enabled
    -> final monitor workarea
```

The MONOCLE tab bar does **not** permanently change `Monitor.workarea`. Instead,
MONOCLE derives a content rectangle from the workarea by temporarily removing
the configured tab edge when the tab bar materializes.

This separation matters because:

* normal placement uses workarea;
* snap/maximize use workarea;
* MONOCLE uses workarea minus its tab strip;
* real fullscreen uses raw monitor geometry.

`_NET_WORKAREA` exposes the selected monitor's Box2430 workarea as one rectangle;
it is not a global per-desktop model.

### Semantic geometry vs. presentation

`commit_client_geometry()` changes semantic geometry and presents it.
`present_client_geometry()` changes only the X window presentation.

Temporary presentations such as MONOCLE and real fullscreen use presentation
without rewriting restore state.

`client_geometry_is_wm_owned()` is true while MONOCLE, fullscreen, maximize, or
snap owns the visible geometry. Client `ConfigureRequest` geometry cannot simply
overwrite a WM-owned presentation state.

### Normal size hints

Box2430 caches and applies ICCCM `WM_NORMAL_HINTS`, including:

* base/minimum/maximum size;
* resize increments;
* aspect constraints.

Hints are invalidated on `WM_NORMAL_HINTS` property change and re-read when
needed. Geometry is then clamped/fit against relevant workarea constraints.

### Snap and maximize

Snap state is semantic state separate from raw geometry. Side/corner targets are
computed from the destination monitor workarea and configured ratios.

Maximize is represented independently by `client->maximized` and is reflected as
both EWMH maximized atoms. It uses the monitor workarea.

Leaving snap/maximize restores `normal_geometry` rather than treating the target
rectangle as the permanent normal size.

### Fullscreen

User fullscreen and client-requested fullscreen are tracked separately:

```text
user_fullscreen
client_fullscreen
fullscreen_policy
    -> should this client receive real fullscreen?
    -> fullscreen presentation flag
```

User `fullscreen` commands can request real fullscreen directly. Client EWMH
fullscreen requests pass through the configured/per-rule policy:

* `allow`: may become real fullscreen;
* `fake`: record/report the client fullscreen request without screen-covering
  presentation;
* `deny`: do not retain client fullscreen state.

Real fullscreen uses the raw monitor rectangle and zero border. Exiting it
rematerializes the appropriate underlying MONOCLE/maximize/snap/normal state.

## Native UI

### Borders

Borders are drawn directly on ordinary client windows with X11 border width and
colors. FREE and MONOCLE have independent width/color configuration. A rule can
disable WM borders for an individual client.

Changing workspace mode rematerializes border width as part of client
presentation.

### Native bar

When enabled, Box2430 creates one override-redirect bar window per monitor. The
bar is laid out from configured left/center/right widget groups.

Supported widgets are:

```text
workspaces  mode  title  status  clock  tray
```

The layout records widget rectangles and per-workspace rectangles in `Monitor`.
Those same rectangles are used for drawing and workspace-label hit testing.

Widget styles are resolved from bar base style -> widget style -> visual-state
style. The workspace and mode widgets have state-specific styling; title can
select title/class/instance metadata.

The bar occupies real workarea space even though its X window is
override-redirect: Box2430 subtracts its configured edge explicitly rather than
publishing a Dock strut for its own bar.

### Status

Status is external text, not an internal command runner. `ui_status_refresh()`
reads root `_NET_WM_NAME` when it is valid UTF-8 and falls back to `WM_NAME`.
Root property changes refresh the bar.

### Clock and event-loop timeout

Clock text is generated from the configured `strftime()` format. If the bar is
enabled, the clock widget is configured, and at least one bar has visible
geometry, the main event loop uses an approximately one-second poll timeout.
Otherwise it can block indefinitely waiting for X activity.

This keeps periodic work conditional on an actually visible clock.

### MONOCLE tab bar

Each monitor owns a possible tab-bar window, but it maps only when:

* tabs are enabled;
* the monitor's active workspace is MONOCLE;
* the workspace contains at least one client.

Tab geometry comes from the monitor workarea, not the native bar rectangle. Its
edge is configured independently from the native bar edge.

Tab hit testing follows stable `tab_head -> tab_tail` order. The tab-bar binding
context supplies the clicked client to command dispatch.

### Snap preview

Snap preview uses four Box2430-owned override-redirect windows that draw an
outline for a candidate snap/maximize target. Preview is temporary UI state and
does not commit the client snap/maximize state until the drag finishes.

## System tray

The tray is an XEmbed system-tray manager implemented in `tray.c`.

It is enabled only when:

1. the native bar is enabled; and
2. the `tray` widget is present in one configured bar group.

Box2430 attempts to own `_NET_SYSTEM_TRAY_S<screen>`. The tray has two internal
X windows:

* an **owner** window that owns the selection and carries tray properties;
* a **host** window into which XEmbed icons are reparented.

If another tray manager already owns the selection, Box2430 logs the condition,
keeps running, and leaves its tray inactive. Selection loss at runtime similarly
disables the tray rather than terminating the WM.

Docked icons are tracked separately from ordinary clients and special windows.
Their requested geometry is normalized to the current tray slot/bar constraints
before embedding. XEmbed mapped state controls whether an icon is actually
mapped inside the host.

There is one X11 tray selection per screen, not one tray manager per monitor.
Box2430 therefore allocates the tray widget only on `selected_monitor`; selecting
another monitor moves/reallocates the tray host into that monitor's native bar.

## Stacking model

Box2430 has explicit workspace stack order for ordinary clients and also enforces
a root-level ordering between categories it owns.

The intended known tiers are approximately:

```text
bottom
    Desktop special windows
    ordinary active-workspace clients
    Box2430 native UI (bars, tray host, MONOCLE tabs)
    managed non-Desktop special windows (Dock / managed Notification)
    real fullscreen clients
 top among Box2430-managed tiers
```

This is not implemented by blindly raising every Box2430 UI window to the
absolute root top. Native UI and higher tiers are ordered relative to known
siblings. That distinction is important for unrelated override-redirect overlays
such as dunst: Box2430 should not repeatedly jump its bar over an external
notification merely because the bar redraws or restacking is enforced.

At startup, discovery also detects an already-visible override-redirect
Notification window as a one-shot ceiling so newly created native UI does not
immediately appear above it.

Only active-workspace ordinary clients participate in the ordinary visible
stack. Workspace switching briefly overlaps incoming/outgoing mapped windows,
but stacking enforcement follows the newly active workspace so outgoing clients
are not raised back over the incoming workspace.

## Special windows and workareas

`Dock`, `Desktop`, and managed `Notification` window types are represented as
`SpecialWindow`, not `Client`.

* Desktop is deliberately kept at the bottom.
* Dock may publish `_NET_WM_STRUT_PARTIAL` or legacy `_NET_WM_STRUT`; those
  struts alter monitor workareas.
* managed non-Desktop special windows occupy the special-window stacking tier.

Override-redirect windows are generally outside normal management. This is why
external notification overlays can coexist without becoming Box2430 clients.

Dock strut property changes recompute all workareas and rematerialize client/native
UI geometry.

## Window management lifecycle

### Startup

Initialization follows this broad order:

```text
load defaults + optional config
    -> open X display
    -> acquire WM ownership
    -> paint fresh-session fallback background (fresh session only)
    -> initialize atoms/EWMH identity
    -> discover monitors/workspaces
    -> compute initial workareas
    -> initialize native UI resources/windows
    -> initialize tray selection/host when configured
    -> draw/update bars
    -> grab configured keys
    -> discover existing top-level windows
    -> synchronize X and check semantic invariants
    -> initialize optional bspwm compatibility socket
```

Autostart runs after `wm_init()` returns successfully and startup discovery is
complete, so an autostarted Polybar can connect to an already-created socket and
receive a post-discovery report. Compatibility initialization failure is logged
but does not fail WM startup. Teardown closes compatibility clients/listener and
removes its owned socket before the X connection and accepted RandR snapshot are
destroyed; compatibility descriptors are not preserved across WM restart.

### Existing-window discovery

Startup scans root children in multiple passes:

1. special windows;
2. ordinary non-transient clients;
3. transient/dialog clients.

The ordering lets managed transient parents exist before their children are
assigned default monitor/workspace policy.

A viewable ordinary window is eligible for adoption. An unmapped ordinary
window can also be adopted when its existing ICCCM `WM_STATE` is `IconicState`.
Override-redirect/InputOnly windows are not adopted as normal clients.

### Manage pipeline

For a normal/dialog client, the main management path is:

```text
read X attributes/type
    -> read title/class/instance/transient metadata
    -> choose default policy
       (transient parent monitor/workspace when available,
        otherwise selected monitor/active workspace)
    -> apply matching rules in declaration order
    -> capture original border + initial size hints/geometry
    -> link client into global/workspace orders
    -> select client events + passive buttons
    -> materialize geometry/border
    -> set adopted WM_STATE = NormalState
    -> read focus/urgency protocol metadata
    -> map/hide according to active workspace
    -> apply raise_on_map / focus_on_map
    -> process initial client fullscreen request through policy
    -> refresh UI and EWMH client lists
```

Rules run after transient-derived defaults, so an explicit matching monitor or
workspace rule can override the transient default.

Title/class/type/transient properties are cached and refreshed when properties
change, but metadata refresh is not a general reactive rule engine: changing a
title or class later does not rerun initial placement/rules and migrate the
client automatically.

### Unmanage and shutdown

Unmanage first resolves semantic focus while the client still has enough
workspace-order context for a fallback, then removes it from all workspace/global
structures.

If the X window still exists (withdrawal rather than destruction), Box2430
restores client-owned X state as described earlier.

At full WM shutdown/restart boundary, Box2430 remaps remaining managed clients
before relinquishing ownership. A successor WM should be able to discover all
live top-level clients without understanding Box2430's private inactive-workspace
state.

## Workspace transitions

Activating another workspace on one monitor is ordered to avoid visible stale
geometry/background flashes:

1. choose the incoming workspace focus target;
2. rematerialize incoming client geometry while still hidden;
3. switch `active_workspace`;
4. map/raise incoming clients;
5. focus the chosen incoming target;
6. unmap outgoing clients with `ignored_unmaps` bookkeeping;
7. update native bar and enforce final stacking.

The incoming workspace is therefore prepared before it becomes the visible
steady state.

## Multi-monitor model

Monitor discovery uses the active RandR 1.5 Monitor API. Every active
`XRRMonitorInfo` is one observed Box logical monitor, including when two logical
monitors have identical rectangles. A logical monitor may report zero, one, or
many associated physical outputs; those output IDs and connector names are
retained truthfully rather than converted into additional Box monitors.

The runtime `WM` owns a deep-copied RandR snapshot next to `WMModel`. Each
observation retains geometry, logical-monitor Atom/name, `primary`, `automatic`,
and the complete output ID/name list. No pointers returned by libXrandr survive
the query, and the snapshot is replaced only after a complete observation is
accepted. Snapshot index and `Monitor.index` then describe the same current
observed position. Debug invariants enforce equal snapshot/model counts and
equal geometry at every index, including for the synthetic root observation.

This metadata is platform observation, not semantic Authority. Connector names,
logical-monitor names, `primary`, and `automatic` do not own workspaces, select
monitors, reorder the monitor array, or define client continuity.

### Query validity and synthetic root

RandR 1.5 or newer is checked before WM startup continues. Query failure,
invalid geometry, incomplete metadata capture, allocation failure, or a monitor
count above `BOX2430_MAX_MONITORS` rejects the new observation. Startup fails
clearly; at runtime the previous model and accepted snapshot remain untouched.

A successful query returning zero active logical monitors is the sole fallback
case. Box2430 records one explicitly synthetic root-sized observation with no
logical-monitor/output identity. It does not invent connector metadata or use a
root rectangle to hide another query failure.

### Logical matching

Root `ConfigureNotify` remains the runtime topology trigger. RandR enumeration
index is not considered stable identity.

`match_monitor_observations()` first pairs exact old/new rectangles. Remaining
pairs are chosen greedily by:

1. greater overlap area;
2. then smaller center distance;
3. only after equal geometry scores, matching output sets or logical-monitor
   identity;
4. then deterministic old/new index tie breaking.

Output association is compared as a set, not by returned array order. This
preserves monitor-local state across enumeration reorder while ensuring RandR
identity never defeats better spatial continuity. When both geometry and
metadata are indistinguishable, array indices remain the deterministic fallback.

### Observation policy

RandR is the query backend, not the hotplug policy. Box2430 queries at startup
and from its existing root `ConfigureNotify` reconciliation path. It does not
select RandR output/CRTC/screen-change events, poll topology, or introduce a
detached-monitor state. A physical connection change that produces no existing
accepted trigger therefore leaves `WMModel` and the last accepted snapshot
untouched.

### Reconciliation

Topology changes are planned before commit. The plan determines:

* old/new logical monitor matching;
* added/removed monitor state;
* client migration targets;
* selected-monitor continuity;
* preferred focused client;
* geometry translation/rematerialization needs.

An accepted query is classified before projection. An identical observation is
discarded. If monitor count, order/continuity, and geometry are unchanged but
RandR metadata differs, only the owned snapshot is replaced; workspace/client
Authority and all X11/UI projection remain untouched. A semantic topology change
continues through the staged monitor-array transition below.

New monitors receive fresh workspace arrays. Clients from removed monitors move
deterministically to a surviving target while preserving workspace index where
possible; the surviving monitor keeps its own workspace modes and active
workspace rather than inheriting a removed `Workspace` object wholesale.

When a continuing logical monitor changes origin/size, latent FREE geometry is
translated by the monitor-origin delta and clamped to the final destination
workarea. WM-owned presentation states are recomputed semantically instead of
blindly translated:

* fullscreen -> new raw monitor rectangle;
* snap/maximize -> new workarea;
* MONOCLE -> new MONOCLE content rectangle.

If the previous focused client still survives, remains visible, and is focusable,
reconciliation prefers to keep semantic focus on that client. Otherwise normal
workspace focus fallback applies.

## Moving clients between monitors/workspaces

A client always has one workspace owner. Moving it therefore updates all
workspace-local ordering structures rather than leaving shared membership.

Cross-monitor moves can target:

* the destination monitor's active workspace; or
* the same workspace index with `--keep-workspace`.

Geometry is translated/clamped when monitor ownership changes. With `--follow`,
focus/selection follows the client; without it, source/destination workspaces
resolve their own visible/focus state.

The ownership-only reassignment is shared with topology migration. Normal moves
still preserve their established transition protocol: focused-source fallback
is chosen before unlinking, same-monitor follow may keep the client mapped for
incoming-first workspace activation, and follow activation ends by focusing and
raising the moved client.

Workspace-bar `workspace move-window` uses the same lower-level move path and can
therefore move a focused client to a workspace label on another monitor.

## Interactive move/resize

Interactive client manipulation lives in `wm->drag`.

At drag start, Box2430 focuses the client and clears incompatible snap/maximize
state when needed. Move/resize then tracks root pointer motion.

During move:

* pointer location determines candidate monitor;
* configured edge/corner zones determine snap candidates;
* the top edge away from corners represents maximize preview;
* snap preview is UI-only until release.

On release, a client may first move to another monitor's active workspace and
then commit the chosen snap/maximize target.

## Command and binding path

Bindings do not directly call arbitrary WM internals.

Configuration parses a command into argv, validates it for its input context, and
stores that command in a typed binding table:

```text
keyboard        -> COMMAND_CONTEXT_KEYBOARD
client mouse    -> COMMAND_CONTEXT_MOUSE
tab bar         -> COMMAND_CONTEXT_TABBAR
workspace bar   -> COMMAND_CONTEXT_WORKSPACEBAR
```

At runtime, the relevant X event creates a `CommandContext` containing only the
objects meaningful for that event (clicked client, monitor/workspace label,
coordinates, timestamp), then `command_run()` dispatches the command.

This is why commands such as `mouse resize-window`, `tab close`, and `workspace
activate` cannot be copied into arbitrary binding contexts.

`spawn`, `spawn-shell`, and session autostart share the same child-launch
primitive:

* `spawn` uses `execvp()` with direct argv;
* `spawn-shell` uses `/bin/sh -c`;
* autostart uses `execv()` on the supplied path.

The child closes the inherited X connection fd, calls `setsid()`, and restores
`SIGCHLD` to default before `exec`. The WM process itself ignores `SIGCHLD` with
`SA_NOCLDWAIT` to avoid child zombies.

## Event loop

Box2430 is single-threaded. Normal state mutation happens in the main loop.

Conceptually:

```text
while running:
    drain all pending X events
    refresh clock when relevant
    poll(X connection, timeout)
```

The timeout is indefinite unless a visible configured clock requires periodic
refresh, in which case it is about one second.

Tray event handling runs before the ordinary WM event switch so XEmbed/host/icon
events can be consumed by the tray subsystem.

Important ordinary event families include:

* `MapRequest` -> manage/reconcile mapping;
* `ConfigureRequest` -> client geometry/stack request handling under WM ownership rules;
* root `ConfigureNotify` -> monitor topology reconciliation;
* `DestroyNotify` / `UnmapNotify` -> unmanage/withdrawal;
* `KeyPress` -> keyboard binding dispatch;
* `MappingNotify` -> rebuild keyboard mapping/grabs and client button grabs;
* `ButtonPress` -> bar/tab/client mouse contexts;
* `MotionNotify` / `ButtonRelease` -> interactive drag;
* `EnterNotify` -> sloppy focus;
* `FocusIn` -> semantic-focus compatibility enforcement;
* `PropertyNotify` -> status, hints, protocol capability, metadata, and Dock strut refresh;
* `ClientMessage` -> EWMH activation, close, fullscreen, and maximize requests;
* `Expose` and related internal-window events -> native UI redraw/maintenance.

`SIGINT`/`SIGTERM` request loop termination rather than performing WM state
mutation inside the signal handler.

## X11 compatibility boundary

`x11.c` implements the ICCCM/EWMH subset Box2430 currently needs. The goal is
practical compatibility, not a full desktop-environment model.

Notable supported state/protocols include:

* WM ownership through `SubstructureRedirectMask`;
* `WM_STATE` adoption/withdrawal;
* `WM_DELETE_WINDOW`;
* `WM_TAKE_FOCUS` / `WM_HINTS` input + urgency;
* `WM_NORMAL_HINTS`;
* `_NET_SUPPORTING_WM_CHECK` and `_NET_SUPPORTED`;
* `_NET_ACTIVE_WINDOW`;
* `_NET_CLIENT_LIST` / `_NET_CLIENT_LIST_STACKING`;
* `_NET_CLOSE_WINDOW`;
* `_NET_WM_NAME` and legacy title/status fallbacks;
* `_NET_WM_WINDOW_TYPE` for normal/dialog/dock/desktop/notification;
* `_NET_WM_STATE_FULLSCREEN`;
* `_NET_WM_STATE_MAXIMIZED_HORZ` / `_NET_WM_STATE_MAXIMIZED_VERT`;
* `_NET_WM_STRUT` / `_NET_WM_STRUT_PARTIAL`;
* `_NET_WORKAREA`.

Box2430's workspace model is intentionally per-monitor, so it should not be
silently translated into a conventional single global `_NET_CURRENT_DESKTOP` /
`_NET_WM_DESKTOP` model.

WM ownership failure is detected as `BadAccess`. During normal runtime,
`BadWindow` is ignored because disappearing-client races are routine in X11;
other X11 errors are logged. Internal bookkeeping bugs should be caught through
state invariants and regression tests rather than by serializing every X request.

## Architectural invariants

The following distinctions are structural and should not be collapsed as an
incidental refactor:

* ordinary clients are non-reparented; tray icons are protocol-specific embedded children;
* workspaces are per-monitor, not one global desktop list;
* selected monitor and focused client are distinct state;
* RandR logical/output identity is accepted platform metadata, not semantic
  workspace/client continuity authority;
* topology query source and topology reaction policy are distinct; RandR event
  subscription is not part of the passive reconciliation model;
* semantic client focus decisions and X input-focus reassertion are distinct;
* monitor geometry and workarea are distinct;
* native bar reservation and MONOCLE tab reservation occur at different geometry layers;
* semantic client geometry and temporary X presentation are distinct;
* `normal_geometry` is a restore point, not the current fullscreen/MONOCLE rectangle;
* membership order, stable tab/client order, stack order, and focus-history order are distinct;
* focus and stacking are independent except where explicit policy/mode couples them;
* WM-generated unmaps and client withdrawal are distinct;
* metadata refresh is not automatic rule reapplication;
* the single XEmbed tray follows the selected monitor rather than creating one tray manager per monitor;
* Box2430 native UI should be ordered within known WM tiers, not blindly raised above unrelated override-redirect overlays.

A change that intentionally alters one of these properties is architectural and
should update this document and the relevant regression coverage.

For user-facing commands/configuration see `docs/REFERENCE.md`. For stable
engineering principles see `docs/IMPLEMENTATION_STYLE.md`. For build/testing and
verification see `DEVELOPMENT.md`.
