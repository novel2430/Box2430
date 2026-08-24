# Goal: Optional Polybar Support via a Narrow bspwm Compatibility Layer

## Mission

Implement end-to-end Polybar workspace integration for Box2430 by providing the
small subset of the bspwm IPC wire protocol that Polybar's built-in
`internal/bspwm` module requires.

The feature is **not** a general Box2430 IPC system and is **not** an attempt to
make `bspc` work against Box2430. It is a deliberately narrow interoperability
adapter so users can run Polybar today without patching Polybar or waiting for a
Box2430-specific Polybar module.

Do not stop after producing a design. Inspect the current repository, implement
the feature, add regression coverage, update the affected documentation, run the
relevant tests, and review the final diff for scope and architectural fit.

The working tree contains `ref/bspwm`. Use it as a protocol/reference
implementation where useful, but preserve Box2430's current model and coding
style. In particular, do not import bspwm's architecture, selector system, or
control plane merely because the wire format comes from bspwm.

---

## Product Contract

### User-facing purpose

A user should be able to enable one Box2430 option and then configure Polybar
with its existing bspwm module:

```toml
[bspwm_compat]
enabled = true
```

and, for example:

```ini
[module/workspaces]
type = internal/bspwm
pin-workspaces = true
enable-click = true
enable-scroll = true

label-focused = %name%
label-occupied = %name%
label-urgent = %name%!
label-empty = %name%

label-monocle = MONOCLE
label-tiled = FREE
```

No Polybar patch and no custom Polybar module should be required.

The native Box2430 bar remains independent. Enabling `bspwm_compat` must not
automatically enable or disable the native bar. A user may run both, or may set
`appearance.bar.enabled = false` and use Polybar only.

### Default

The feature is **disabled by default**.

When disabled:

- no bspwm-compatible socket is created;
- no compatibility client/subscriber state is allocated or polled;
- no bspwm reports are generated;
- no additional external control surface exists.

### Scope statement

Document the feature as **Polybar bspwm-module compatibility**, not as general
"bspwm IPC compatibility".

The permanent user-facing documentation should make the following boundary
clear:

> Box2430 implements the subset of the bspwm IPC protocol required by Polybar's
> `internal/bspwm` module. It does not provide general `bspc` compatibility.

### Explicit non-goals

Do **not** implement any of the following as part of this task:

- a Box2430-native IPC protocol;
- `bspc` compatibility as a general goal;
- bspwm `query`, `config`, `rule`, `node`, `wm`, or arbitrary selector APIs;
- IPC-based configuration of Box2430;
- workspace names in the Box2430 semantic model;
- bspwm's tree/tiled semantics;
- bspwm root windows or `wm-restack = bspwm` behavior;
- a generic IPC framework;
- a generic Unix socket/server abstraction layer;
- a thread for IPC handling;
- fd inheritance/restart-preservation machinery for subscribers;
- reactive RandR hotplug policy beyond Box2430's existing topology behavior;
- new external dependencies merely to implement this compatibility layer.

If Polybar restacking is needed, documentation may recommend Polybar's EWMH
restack path where appropriate; do not manufacture bspwm-specific root windows.

---

## Architectural Contract

Read the relevant sections of:

- `AGENTS.md`
- `docs/ARCHITECTURE.md`
- `docs/IMPLEMENTATION_STYLE.md`
- `docs/REFERENCE.md`
- `DEVELOPMENT.md`

before substantial implementation work.

This feature must preserve Box2430's current Transition / Authority / Projection
(TAP) discipline.

Conceptually:

```text
                    WMModel
                 semantic Authority
                  /            \
                 /              \
       X11/native UI         bspwm report
          Projection          Projection
                                  |
                               Polybar
                                  |
                         bspwm wire commands
                                  |
                         semantic Transition
                                  |
                               WMModel
```

### Authority boundary

`WMModel` remains the authoritative desktop-state root. The compatibility layer
must not create a second desktop model or cache semantic truth independently.

Do not add bspwm-specific semantic fields to `Monitor`, `Workspace`, `Client`, or
`WMModel` merely to serialize the protocol.

In particular:

- XRandR monitor names remain accepted observation metadata in
  `wm->monitor_snapshot`, not semantic monitor identity stored in `Monitor`;
- workspaces remain identified semantically by their existing per-monitor
  zero-based `index` and are exposed to Polybar as decimal `index + 1` names;
- FREE and MONOCLE remain Box2430 modes; the bspwm report only projects them into
  compatible presentation flags.

### Transition boundary

Incoming compatibility commands are an additional **input source**, not an
additional state-mutating implementation.

They must resolve their target and converge on existing Box2430 semantic
transitions.

Most importantly:

- desktop/workspace activation must use `workspace_activate()`;
- selecting another monitor from IPC must not use the pointer-warping
  `monitor_select()` path;
- do not directly assign `wm->model.selected_monitor`,
  `monitor->active_workspace`, or `wm->model.focused_client` from
  `bspwm_compat.c`.

For Polybar's `monitor -f NAME`, the intended semantic result is selection of
that monitor while keeping its current active workspace and resolving focus,
without pointer warping. The existing public workspace transition can express
this by activating the target monitor's already-active workspace:

```c
workspace_activate(wm, target_monitor, target_monitor->active_workspace);
```

If the current implementation changes while this task is being executed, use
the current shared semantic transition that has the same meaning rather than
copying old bookkeeping into the compatibility layer.

### Runtime boundary

The compatibility implementation should be localized primarily in:

```text
src/bspwm_compat.c
src/bspwm_compat.h
```

Use an opaque runtime object owned by `WM`, for example conceptually:

```c
typedef struct BspwmCompat BspwmCompat;

/* in WM */
BspwmCompat *bspwm_compat;
```

and a small config object or equivalent:

```c
typedef struct BspwmCompatConfig {
    bool enabled;
} BspwmCompatConfig;
```

The exact API is implementation-dependent, but the intended ownership is:

```text
WM
├── WMModel
├── X11/UI runtime
├── Tray
└── BspwmCompat *
    ├── listener fd
    ├── socket-path ownership
    ├── bounded connection/subscriber state
    ├── bounded input/output buffering
    ├── bspwm command parsing
    └── report serialization/publication state
```

Do not create `ipc.c`, `unix_server.c`, `socket_server.c`, an event callback
framework, or another generic subsystem unless the current repository already
contains a concrete reusable abstraction that genuinely fits. For the current
scope, the Unix socket implementation belongs inside `bspwm_compat`.

---

## Local Reference Material

Use `ref/bspwm` selectively. At minimum inspect the current equivalents of:

- `ref/bspwm/src/bspwm.c`
  - socket path construction;
  - Unix listener lifecycle;
  - socket integration with the main event loop;
  - CLOEXEC/restart considerations.
- `ref/bspwm/src/messages.c`
  - NUL-separated argv wire format;
  - `subscribe` behavior;
  - desktop/monitor focus command shape.
- `ref/bspwm/src/subscribe.c`
  - `report` line format;
  - monitor/workspace/layout tags;
  - initial report on subscription.

Use `grep` within `ref/bspwm` to follow selector semantics only where needed to
understand the Polybar command subset. Do not copy or generalize the full bspwm
selector parser.

The compatibility contract below is intentionally self-contained; local bspwm
is a reference for details, not a reason to broaden scope.

---

## Exact Wire Protocol Required for Polybar

Polybar sends each command as a sequence of ASCII argv elements, each terminated
by `\0`. There is no textual newline command framing.

The long-lived subscription command is:

```text
subscribe\0report\0
```

A successful report subscriber remains connected and receives newline-terminated
full report snapshots.

Polybar's relevant short-lived command connections send forms equivalent to:

```text
desktop\0-f\0MONITOR:^N\0
monitor\0-f\0MONITOR\0
desktop\0-f\0next.local\0
desktop\0-f\0prev.local\0
desktop\0-f\0next.occupied.local\0
desktop\0-f\0prev.occupied.local\0
```

When `pin-workspaces = false`, Polybar may omit `.local`:

```text
desktop\0-f\0next\0
desktop\0-f\0prev\0
desktop\0-f\0next.occupied\0
desktop\0-f\0prev.occupied\0
```

The compatibility layer must implement these forms and no broader command
language is required.

### Supported command grammar

Support exactly the semantic families below:

```text
subscribe report

monitor -f <monitor-name>

desktop -f <monitor-name>:^<1-based-workspace-index>

desktop -f next[.occupied][.local]
desktop -f prev[.occupied][.local]
```

Long aliases, arbitrary selector modifiers, nested bspwm selectors, and other
bspwm domains are out of scope unless they are discovered to be required by the
current Polybar `internal/bspwm` implementation during verification.

### Input framing and partial reads

Do not assume one `send()` equals one `recv()` merely because bspwm's historical
implementation is permissive here.

The implementation must tolerate a supported command arriving across multiple
reads. Keep the parser narrow and bounded:

- accumulate only up to a fixed small maximum command size;
- split only on received NUL terminators;
- enforce a small maximum argv count;
- once a supported grammar has the complete expected argv count, process it;
- reject/close malformed, oversized, or unsupported command connections without
  mutating WM state;
- do not allocate unbounded input based on peer-controlled data.

A subscriber sends only its initial `subscribe report` command. After the
connection becomes a subscriber, no additional command stream needs to be
supported on that fd for this task.

### Command responses

Polybar does not require a success payload for the focus commands. It sends the
request and closes the short-lived connection.

It is acceptable to close successful command connections without a textual
response. Unsupported/malformed requests may be closed with or without a small
error response; do not spend scope on reproducing bspwm's complete error text.

The long-lived `subscribe report` connection must remain open and immediately
receive the current report.

---

## Report Projection Contract

A report is one complete ASCII snapshot line beginning with `W` and ending with
`\n`.

Serialize monitors in Box2430's current semantic/accepted-snapshot order and
workspaces in ascending workspace index.

### Monitor tags

For monitor `i`, obtain the display name from the accepted RandR observation at
the same index:

```text
wm->monitor_snapshot.monitors[i].name_string
```

Do not copy this name into semantic `Monitor` authority.

Tag the selected monitor with uppercase `M` and every other monitor with
lowercase `m`:

```text
M<name>
m<name>
```

The existing invariant that accepted monitor snapshot count/order corresponds to
`WMModel` should remain the basis for this projection.

### Workspace names

Expose each workspace name as decimal:

```text
workspace->index + 1
```

Examples: `1`, `2`, ..., `9`, `10`, etc.

Do not add configurable or semantic workspace names in this task.

### Workspace state tags

For a workspace, determine:

- **active**: it is `monitor->active_workspace`;
- **urgent**: at least one client in that workspace currently has Box2430's
  urgency flag set;
- **occupied**: the workspace contains at least one managed client;
- otherwise it is empty.

Use bspwm-compatible tags:

| State | Active | Inactive |
| --- | --- | --- |
| empty | `F` | `f` |
| occupied | `O` | `o` |
| urgent | `U` | `u` |

Urgent takes precedence over occupied/empty, matching the report semantics that
Polybar expects. Uppercase/lowercase expresses whether the workspace is active
on that monitor.

### Workspace mode mapping

After each monitor's workspace tags, expose the mode of that monitor's active
workspace:

```text
WORKSPACE_MONOCLE -> LM
WORKSPACE_FREE    -> LT
```

This is an intentional **presentation compatibility mapping**. It does not mean
that Box2430 FREE semantically becomes bspwm tiled mode.

Do not emit fabricated `T...` node-state tags or `G...` node flags in the first
implementation. Polybar can parse them, but Box2430 does not need to invent
those bspwm concepts.

### Example

Conceptually, a two-monitor report may look like:

```text
WMeDP-1:O1:f2:f3:LT:mHDMI-1:F1:o2:f3:LM\n
```

The exact monitor names and workspace count come from the current runtime.

### Full snapshots, not deltas

Every published update is a complete report line. Do not invent a delta/event
protocol.

Keep the last serialized report in compatibility runtime state so publication
can be change-driven:

```text
serialize current WMModel + accepted RandR names
        |
        v
compare with last report
        |
   changed?
     /   \
   no     yes
           |
           v
    queue/broadcast full report
```

A newly connected subscriber must receive the current report immediately even if
nothing has changed since the previous publication.

---

## Command-to-Transition Semantics

### `desktop -f MONITOR:^N`

1. Resolve `MONITOR` by exact comparison against accepted
   `monitor_snapshot.monitors[i].name_string`.
2. Resolve semantic monitor `&wm->model.monitors[i]` using the same index.
3. Parse `N` as a strict one-based workspace ordinal.
4. Reject `N == 0`, overflow, non-numeric suffixes, and values greater than the
   configured workspace count.
5. Call the existing workspace activation transition for that monitor/workspace.

Do not special-case focus or selected-monitor repair in the compatibility layer.

### `monitor -f MONITOR`

Resolve the monitor by accepted RandR name and activate the target monitor's
currently active workspace through the shared semantic transition.

**Do not call `monitor_select()`**, because that path intentionally warps the
pointer for keyboard monitor selection. Polybar scrolling must never make the
mouse pointer jump to the monitor center.

### `desktop -f next.local` / `prev.local`

Cycle through workspaces on the currently selected monitor with wraparound,
then call the shared workspace activation transition.

### `.occupied`

When `.occupied` is present, skip workspaces with no managed clients.

If no eligible target exists, make no semantic change. Do not create a new
notion of "occupied" separate from workspace membership.

### Without `.local`

Polybar may use `next`, `prev`, `next.occupied`, or `prev.occupied` when
`pin-workspaces = false`.

Implement a deterministic global cycle over Box2430's existing monitor/workspace
order. Use monitor-major order:

```text
monitor 0 workspace 0..N-1,
monitor 1 workspace 0..N-1,
...
```

with wraparound. Start from the selected monitor's active workspace. When the
target belongs to another monitor, normal workspace activation should select
that monitor as part of the same semantic transition.

Do not create global workspace identity or EWMH desktop semantics to implement
this traversal; it is only a compatibility selector traversal.

---

## Socket Path and Lifecycle

### Path selection

Match the path Polybar expects:

1. if `BSPWM_SOCKET` exists and is non-empty, use it exactly;
2. otherwise derive the conventional bspwm path for the active X display:

```text
/tmp/bspwm<host>_<display>_<screen>-socket
```

For the common local display `:0`, this is effectively the same path that bspwm
and Polybar derive with an empty host component.

Do not add a Box2430-specific socket-path configuration option in this task.
`BSPWM_SOCKET` is already the interoperability override understood by Polybar.

Do not add libxcb solely to call `xcb_parse_display()`. Box2430 already has an
Xlib display. Implement only the small display-string parsing required to derive
the conventional path, and keep it local to `bspwm_compat`. Test the ordinary
forms used by X11 sessions, including at least local `:N`, `:N.S`, and a named
host form if practical.

Reject paths that cannot fit in `sockaddr_un.sun_path` with a clear compatibility
error; do not truncate silently.

### Existing path / stale socket handling

Do not blindly unlink an arbitrary existing path.

When the chosen path already exists, distinguish a live listener from a stale
socket as safely and simply as practical. The intended behavior is:

- if another live socket owner accepts a connection, log a clear error and
  disable `bspwm_compat` for this Box2430 session;
- if the path is an obviously stale Unix socket (for example connection attempt
  reports refusal/no listener), remove it and bind;
- if the existing path is not a Unix socket or its ownership is ambiguous, do
  not delete it; log an error and disable the compatibility feature.

This optional feature must never destroy an unrelated filesystem entry just to
start the WM.

### Failure semantics

If `[bspwm_compat].enabled = true` but the compatibility runtime cannot be
created (path conflict, permissions, resource failure, etc.):

- print a clear `box2430:` error identifying the compatibility failure;
- leave `wm->bspwm_compat == NULL` (or equivalent disabled runtime state);
- continue running the window manager normally.

Do not silently fail, but do not make this optional Polybar feature fatal to the
entire WM session.

### Initialization order

Create the compatibility runtime only after Box2430 has established coherent
startup authority:

```text
open/acquire X
-> initialize monitors/workareas/UI/tray
-> discover existing windows
-> XSync
-> WM invariant check
-> create bspwm_compat if enabled
-> enter wm_run()
-> run session autostart
```

The important user-visible property is that an autostarted Polybar can connect
after Box2430 has already created the socket, and its first report reflects the
post-startup-scan desktop state.

### Destruction order

Destroy the compatibility runtime and close clients/listener during WM teardown
before the X connection and accepted monitor snapshot become invalid. Remove the
socket path only if this runtime successfully created/owns that socket.

Do not attempt to preserve listener/subscriber fds across Box2430 restart in this
version.

Polybar reconnect behavior should be observed during smoke testing and reported,
but restart-preserved IPC is not an acceptance requirement for this task.

---

## File Descriptor and I/O Requirements

Box2430 remains single-threaded.

### Nonblocking and CLOEXEC

The listener and every accepted client fd must be:

- nonblocking;
- close-on-exec.

Use the existing POSIX/C style of the project. It is fine to use `fcntl()` after
`socket()`/`accept()` rather than introducing GNU-specific APIs solely for
convenience.

CLOEXEC matters because Box2430 spawns arbitrary child applications and
session-autostart processes. Compatibility sockets must not leak into those
exec'd processes.

### SIGPIPE safety

A Polybar process may disappear between readiness checks and writes. A dead
subscriber must not terminate Box2430 through SIGPIPE.

Handle socket writes in a way that makes broken peers an ordinary connection
cleanup condition. Do not globally install a SIGPIPE behavior that accidentally
creates inherited child-process semantics unless the chosen handler is known to
reset appropriately across `exec`.

### Bounded clients

Use a small fixed maximum number of compatibility connections/subscribers rather
than an unbounded server data structure. A value around the project's existing
bounded style (for example 32) is ample for Polybar and keeps memory/poll state
predictable.

The exact constant is up to the implementation, but it must be:

- fixed/bounded;
- not user-configurable in this task;
- sufficient for multiple monitor bars plus short-lived click/scroll commands.

If the limit is reached, reject/close new connections without disturbing the WM.

### Nonblocking writes and backpressure

Never block the WM waiting for a subscriber to read.

A report may normally fit in one local socket write, but correctness must not
rely on that. Handle short writes and `EAGAIN` without unbounded buffering.

Preferred behavior is a small bounded pending-output mechanism. Since reports
are complete snapshots, later unsent updates may be coalesced to the newest
snapshot as long as the byte stream remains valid (never splice a new report
into the middle of a partially written report).

It is also acceptable to disconnect a persistently unusable/slow subscriber
rather than grow memory or block the main loop.

Poll for `POLLOUT` only while a connection actually has pending output.

Handle `POLLHUP`, `POLLERR`, EOF, and ordinary local-socket races by removing the
connection cleanly.

---

## Main Event Loop Integration

Do not hide the existence of compatibility fds behind a generic reactor
framework. `wm_run()` already is Box2430's process-level reactor and may know
that additional fds need polling.

The intended shape is approximately:

```text
while running:
    drain pending X events
    -> each X event completes its normal Box transition/projection

    after coherent X-event processing:
        publish bspwm report if changed

    tick native clock if needed

    build pollfd array:
        X connection
        optional bspwm listener
        optional bspwm clients (POLLIN and POLLOUT only as needed)

    poll(using existing clock-derived timeout)

    dispatch ready bspwm compatibility fds
        -> parse supported request
        -> call shared semantic transition
        -> invariant checkpoint in wm.c
        -> publish report if changed
```

This is illustrative, not a requirement to use these exact function names.

The important invariants are:

1. the compatibility layer does not run a second thread/event loop;
2. socket details remain inside `bspwm_compat`;
3. `wm.c` may coordinate the pollfd set and post-dispatch WM invariant check;
4. reports are generated only from coherent post-transition authority;
5. publication is not manually embedded into every individual WM transition.

### Publish checkpoint

Do **not** add calls such as:

```c
bspwm_compat_publish(...);
```

inside every one of:

- `workspace_activate()`;
- monitor activation;
- client manage/unmanage;
- urgency changes;
- workspace mode changes;
- topology reconciliation;
- focus paths.

That approach is path-fragile and will eventually miss a state-changing route.

Instead, serialize/compare at a coherent main-loop checkpoint after completed X
input processing and after compatibility-command dispatch. The serializer is
cheap and the cached-report comparison decides whether subscribers actually need
an update.

This also naturally catches report-visible changes that come from accepted
RandR metadata (for example a logical monitor name change) without inventing a
new semantic event type.

Preserve the current clock timeout behavior. Adding compatibility fds must not
break native-clock ticking or cause busy polling when no fd/output is active.

---

## Configuration Work

Add the top-level table:

```toml
[bspwm_compat]
enabled = false
```

Requirements:

- default is false in `config_set_defaults()`;
- the strict config parser recognizes only `enabled` under this table;
- unknown keys continue to invalidate the whole config according to current
  Box2430 behavior;
- add the table to `config.example.toml` with `enabled = false`;
- add valid/invalid config regression coverage consistent with the current
  strict-atomic config tests.

Do not add socket path, max clients, protocol mode, workspace names, or other
speculative knobs.

---

## Suggested Implementation Sequence

Keep the repository buildable between phases. Adjust exact boundaries if the
current code makes another small ordering more natural, but do not broaden the
feature.

### Phase 0 — Reconnaissance and contract check

Before editing:

1. read the relevant current Box2430 docs listed above;
2. inspect `WM`, `WMModel`, `workspace_activate()`, `monitor_select()`,
   `wm_init()`, `wm_run()`, `wm_destroy()`, config parsing, RandR snapshot
   ownership, and current Xvfb test conventions;
3. inspect the local bspwm files listed under **Local Reference Material**;
4. confirm the code still matches the assumptions in this GOAL;
5. if a small implementation detail has drifted, adapt to the current code while
   preserving this contract rather than rewriting unrelated code.

Do not spend the phase designing a generic IPC subsystem.

### Phase 1 — Config and report projection core

Implement:

- `BspwmCompatConfig` or equivalent with default `enabled = false`;
- strict `[bspwm_compat]` parsing;
- the compatibility module skeleton;
- monitor-name lookup through accepted RandR snapshot order;
- a bounded/full report serializer implementing the exact mapping above.

Add the cheapest useful focused verification for report format and config before
proceeding.

At the end of this phase, report generation should be testable independently of
live Polybar even if socket subscriptions are not wired yet.

### Phase 2 — Socket runtime and subscription

Implement inside `bspwm_compat`:

- path resolution (`BSPWM_SOCKET` first, conventional bspwm path otherwise);
- safe listener creation/ownership/cleanup;
- nonblocking + CLOEXEC fds;
- bounded connection slots;
- incremental bounded NUL-argv input parsing;
- `subscribe report` recognition;
- immediate initial report;
- long-lived subscriber output handling;
- clean disconnect/error/backpressure behavior.

Integrate the listener/clients into `wm_run()`'s existing `poll()` loop without
threads or a generic reactor layer.

Initialize the runtime after startup discovery/invariant check and before
session autostart.

### Phase 3 — Polybar command transitions

Implement exactly the supported command grammar:

- `desktop -f MONITOR:^N`;
- `monitor -f MONITOR` without pointer warp;
- local next/prev;
- local occupied next/prev;
- non-local/global next/prev;
- non-local/global occupied next/prev.

Converge all resulting desktop changes on the existing workspace semantic
transition.

After a compatibility command batch/dispatch, run Box2430's debug invariant
checkpoint from the coordinator (`wm.c`) rather than duplicating model checks in
`bspwm_compat`.

### Phase 4 — Change-driven publication

Wire coherent report publication into the main loop:

- after completed X-event processing;
- after compatibility-command dispatch.

Use last-report comparison so irrelevant X events do not generate subscriber
traffic.

Verify that all report-visible existing transitions are captured without adding
per-transition publish hooks:

- selected monitor changes;
- active workspace changes;
- client manage/unmanage affecting occupied state;
- urgency changes;
- FREE/MONOCLE mode changes;
- accepted monitor topology/name changes where existing Box topology logic
  already reacts.

### Phase 5 — Hardening and regression coverage

Add/extend tests for the acceptance matrix below.

Keep tests self-contained. Do not make Polybar a mandatory build/test dependency.
Use a small compatibility test client/helper where needed to send actual
NUL-separated packets and read subscriber reports.

Use existing Xvfb/RandR helpers rather than inventing a parallel test harness.

### Phase 6 — Documentation and real integration smoke

Update permanent docs to describe the implemented current behavior, not this
historical phase plan.

If `polybar` is installed in the development environment, perform a real smoke
test with `type = internal/bspwm`. Do not install a new system dependency solely
for the automated suite. If Polybar is unavailable, provide a short manual smoke
procedure and clearly report that real Polybar execution was not verified in the
environment.

Review the final diff and remove accidental generic abstractions or bspwm surface
area not required by the contract.

---

## Required Regression / Acceptance Matrix

The exact test filenames may follow current repository conventions. Prefer a
small focused helper plus one or more Xvfb integration scenarios.

### A. Config/default behavior

1. With no `[bspwm_compat]` table, the feature is disabled.
2. `[bspwm_compat] enabled = false` creates no socket.
3. `[bspwm_compat] enabled = true` starts the compatibility listener when the
   path is available.
4. Unknown keys or invalid value types under `[bspwm_compat]` preserve current
   strict atomic-config failure behavior.

For tests, setting `BSPWM_SOCKET` to a temporary path is encouraged so tests do
not touch a developer's real/default bspwm socket path.

### B. Subscription/report

5. `subscribe\0report\0` immediately returns one newline-terminated full report
   and keeps the connection open.
6. The report begins with `W`.
7. Single-monitor selected state uses `M` and correct numeric workspace names.
8. Multi-monitor report uses accepted RandR monitor names and exactly one
   selected-monitor `M`; other monitors use `m`.
9. Active empty/occupied/urgent produce `F`/`O`/`U`; inactive equivalents produce
   lowercase tags.
10. FREE produces `LT`; MONOCLE produces `LM`.
11. The report omits fabricated `T`/`G` bspwm node semantics.

### C. Change publication

12. Creating/managing the first client on a workspace changes empty -> occupied
    and produces a new report.
13. Removing the last client changes occupied -> empty and produces a new report.
14. Changing urgency produces the correct urgent report state.
15. Activating another workspace produces the correct active uppercase/lowercase
    state change.
16. Changing FREE/MONOCLE produces the corresponding `LT`/`LM` report.
17. Irrelevant X activity that leaves the serialized report unchanged does not
    enqueue duplicate report snapshots indefinitely.

### D. Click command semantics

18. `desktop -f <monitor>:^N` activates exactly that per-monitor workspace.
19. The command also selects the target monitor through normal workspace
    activation when needed.
20. Invalid monitor names or workspace ordinals do not mutate WM authority.

### E. Scroll command semantics

21. `next.local` and `prev.local` wrap within the selected monitor.
22. `.occupied.local` skips empty workspaces and does nothing safely when no
    eligible alternative exists.
23. Non-local `next`/`prev` traverse deterministic monitor-major order with
    wraparound.
24. Non-local `.occupied` traversal skips empty workspaces across monitors.

### F. Monitor focus command / pointer behavior

25. `monitor -f <name>` selects that monitor and keeps its active workspace.
26. It resolves semantic focus according to existing monitor/workspace
    activation rules.
27. It **does not warp the pointer**. Add an automated assertion if practical;
    otherwise make this an explicit real-X smoke check and keep the implementation
    obviously routed away from `monitor_select()`.

### G. Transport hardening

28. A supported command split across multiple writes/reads is parsed correctly.
29. Oversized/malformed input is rejected without crash or WM state mutation.
30. Subscriber EOF/HUP removes only that connection.
31. Writing to a disconnected subscriber cannot terminate Box2430.
32. A slow/non-reading subscriber cannot block the WM or cause unbounded memory
    growth.
33. Connection-limit exhaustion rejects excess connections without disturbing
    existing WM behavior.
34. Spawned/autostart child processes do not inherit the compatibility listener
    or client fds across `exec` (CLOEXEC behavior).

### H. Socket ownership/failure

35. `BSPWM_SOCKET` override is honored.
36. The conventional default path matches Polybar/bspwm for normal X display
    strings.
37. A live conflicting socket causes a clear compatibility error but Box2430
    still runs.
38. An unrelated non-socket path is not unlinked.
39. A stale owned/unserved Unix socket can be recovered safely.
40. Normal WM teardown closes compatibility fds and removes only the socket path
    created by this runtime.

### I. Existing regression suite

41. `make` succeeds with normal warnings policy.
42. The relevant focused tests pass.
43. `make test` passes, including all pre-existing scenarios.
44. Run release/sanitizer or other existing verification when practical for the
    new socket code; report exactly what was run.

### J. Real Polybar smoke (when available)

45. With `bspwm_compat.enabled = true`, Polybar's unmodified
    `type = internal/bspwm` module starts and displays workspaces.
46. Clicking a workspace changes Box2430's workspace.
47. Scrolling changes workspaces according to Polybar settings.
48. Occupied/urgent/mode labels update.
49. On multiple monitors, `pin-workspaces = true` shows the workspaces associated
    with the matching RandR monitor name.
50. Observe Box2430 restart behavior and note whether the installed Polybar
    reconnects cleanly. Do not add restart fd inheritance solely to satisfy this
    observation in this task.

---

## Documentation Requirements

Update documentation only after implementation behavior is known and tested.

### `README.md`

Add a concise user-facing capability note that Polybar is supported through its
built-in bspwm module when the optional compatibility layer is enabled.

Do not advertise general bspwm/bspc compatibility.

### `docs/REFERENCE.md`

Document:

- `[bspwm_compat] enabled = false` default;
- how to enable it;
- `BSPWM_SOCKET` override behavior;
- a minimal Polybar `[module/...] type = internal/bspwm` example;
- numeric workspace names (`1..N`);
- FREE -> Polybar `label-tiled`, MONOCLE -> `label-monocle` mapping;
- click/scroll support;
- multi-monitor name matching through RandR;
- the non-goal of general `bspc` compatibility;
- that `wm-restack = bspwm` is not part of this compatibility contract, and that
  users should use an appropriate non-bspwm-specific restack mode such as EWMH
  when needed.

### `docs/ARCHITECTURE.md`

Add a compact architectural description:

```text
Polybar internal/bspwm
    -> bspwm_compat command adapter
    -> existing monitor/workspace Transition
    -> WMModel Authority
    -> bspwm report Projection
    -> Polybar subscriber
```

Record that `BspwmCompat` is runtime/interoperability state outside `WMModel`,
monitor names come from accepted RandR observation metadata, and report
publication happens at coherent main-loop checkpoints rather than inside every
transition.

### `DEVELOPMENT.md`

Only update if the implementation adds a useful new test helper, focused test
command, or manual Polybar smoke procedure that future developers should know.

Do not alter `docs/IMPLEMENTATION_STYLE.md` unless implementation work reveals a
truly new long-lived engineering principle; this feature should fit the existing
principles already documented there.

---

## Expected Diff Character

A good implementation should look structurally like a local interoperability
feature, not an IPC rewrite.

Expected major changes:

```text
+ src/bspwm_compat.c
+ src/bspwm_compat.h
~ src/box2430.h           small config/runtime ownership additions
~ src/config.c            strict optional config parsing
~ src/wm.c                init/destroy + poll integration + publish checkpoints
~ Makefile                source/test helper integration
~ config.example.toml
~ README.md
~ docs/REFERENCE.md
~ docs/ARCHITECTURE.md
~ tests/...
```

`src/bspwm_compat.c` should contain most of the new complexity.

Treat the following as warning signs that scope/design has drifted:

- hundreds of bspwm-specific lines spread throughout `wm.c`;
- large changes to `command.c`;
- bspwm fields added to `WMModel`, `Monitor`, `Workspace`, or `Client`;
- a new generalized IPC/socket/event framework;
- a new dependency such as libxcb solely for compatibility plumbing;
- per-transition report hooks scattered across WM logic;
- direct Authority mutation in `bspwm_compat.c`;
- reimplementation of bspwm's selector/query/config/rule system;
- workspace naming introduced because Polybar supports names;
- pointer warping caused by Polybar monitor/scroll actions.

If the clean implementation requires a very small existing helper to become
non-static or gain a narrow semantic wrapper, that is acceptable when it makes
multiple input sources converge on the same Transition. Prefer that over copying
state mutation into the compatibility module. Keep the API semantic, not
bspwm-specific.

---

## Decisions Already Made — Do Not Reopen Without Concrete Evidence

These are deliberate product/architecture decisions for this task:

1. The module/file/product term is **`bspwm_compat`**, emphasizing a narrow
   compatibility contract rather than a general IPC API.
2. The feature is optional and disabled by default.
3. The purpose is to make Polybar's existing `internal/bspwm` module usable with
   Box2430; arbitrary existing bspwm Polybar configurations are not promised to
   work unchanged.
4. `WORKSPACE_FREE` is projected as bspwm `LT`; `WORKSPACE_MONOCLE` as `LM`.
5. Compatibility initialization failure is non-fatal to the WM but must be loud.
6. First version does not preserve subscriber fds across Box2430 restart.
7. First version keeps workspace names numeric `1..N`; do not add semantic
   workspace names.
8. Monitor names remain RandR observation metadata, not semantic identity.
9. Unix socket mechanics stay inside `bspwm_compat`; do not preemptively extract a
   generic socket layer.
10. IPC commands reuse existing Box2430 semantic transitions and never mutate
    `WMModel` directly.
11. `monitor -f` from Polybar must not warp the pointer.
12. Publication uses complete report snapshots and coherent checkpoint comparison,
    not scattered manual emit calls.

If current code or real current Polybar behavior demonstrates that one of these
is impossible or materially incorrect, stop broadening the implementation,
document the concrete conflict, and make the smallest necessary adjustment.
Do not reinterpret convenience as evidence that the contract should expand.

---

## Completion Checklist

Before declaring the task complete:

- [ ] `bspwm_compat` is disabled by default and fully absent at runtime when disabled.
- [ ] Enabling it creates the expected Polybar-visible socket.
- [ ] `subscribe report` works as a long-lived subscription with immediate state.
- [ ] Report monitor/workspace/urgent/mode projection matches the contract.
- [ ] Polybar workspace click commands work.
- [ ] Polybar local and occupied scroll commands work.
- [ ] Non-local scroll forms used with `pin-workspaces = false` work.
- [ ] Polybar monitor selection does not warp the pointer.
- [ ] No compatibility command directly mutates `WMModel` Authority.
- [ ] No report publishing was scattered through individual WM transitions.
- [ ] Socket/client I/O is nonblocking, bounded, CLOEXEC, and SIGPIPE-safe.
- [ ] Stale/live socket path handling cannot delete an unrelated file.
- [ ] Compatibility failure does not prevent Box2430 from running.
- [ ] Existing `make test` regression suite still passes.
- [ ] New regression coverage exercises the actual Unix socket wire format.
- [ ] User-facing and architecture documentation is synchronized with behavior.
- [ ] Real Polybar smoke test was run if Polybar was already available; otherwise
      the missing environment verification is explicitly reported.
- [ ] Final diff contains no unrelated refactor or speculative IPC features.

In the final implementation report, summarize:

1. files/architecture changed;
2. exact compatibility protocol implemented;
3. tests actually executed and results;
4. whether real Polybar was exercised;
5. any known limitation, especially restart reconnect behavior if observed.
