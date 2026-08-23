# Box2430 V1.7 TAP Refactor Contract

## Status and purpose

This document defines the architectural contract for a behavior-preserving
refactor of the current Box2430 V1.7 implementation.

The goal is **not** to redesign the window manager. The goal is to make the
semantic architecture that is already partly present in V1.7 more explicit,
especially around interaction, focus, workspace activation, monitor selection,
client movement, visibility, and lifecycle glue.

The central rule is:

> **Existing V1.7 behavior is the specification for this refactor.**

The refactor is successful only if the observable behavior of the current WM is
preserved: X11 behavior, focus behavior, workspace behavior, stacking,
geometry, EWMH/ICCCM behavior, native UI behavior, tray behavior, startup and
restart behavior, and established transition ordering.

TAP is a reasoning and responsibility model for organizing that behavior. It is
not permission to replace existing behavior with a cleaner hypothetical design.

---

## 1. Non-goals

This refactor must not be used to introduce unrelated architectural or product
changes.

Explicit non-goals:

- no user-visible feature changes;
- no new focus policy;
- no new workspace semantics;
- no new monitor semantics;
- no multi-seat support;
- no generic event bus or callback framework;
- no generic transaction framework;
- no new object system;
- no i3-style full render-tree rewrite;
- no wholesale data-model rewrite for architectural symmetry;
- no IPC implementation as part of this refactor;
- no redesign of working geometry, topology, stacking, tray, or native-UI
  behavior merely to make the TAP model look more uniform;
- no changes to protocol ordering unless equivalence is demonstrated by current
  behavior and regression tests.

The implementation should remain direct C in the existing Box2430 style:
explicit structures, explicit calls, a single X11 event loop, and abstractions
only where they represent real semantic boundaries.

---

## 2. Behavior-preservation rule

Do not begin from "what should a WM do?" Begin from "what does current V1.7 do,
and which parts of that behavior are intentional or relied upon?"

When characterizing a path, use the following evidence:

1. current explicit behavioral contracts and intentional decisions in the
   repository documentation;
2. current integration and focused regression tests;
3. stable behavior encoded by the current V1.7 implementation;
4. architectural preference, including the TAP model described here.

If these appear to conflict, do **not** silently resolve the conflict by changing
behavior during this refactor. Characterize the discrepancy and preserve the
current established behavior until a separate behavior-changing decision is
made.

Passing existing tests is necessary but not sufficient. The current test suite
is broad, but not every established interaction is guaranteed to be encoded in
a regression test. Read the implementation and callers before restructuring a
complex path.

A useful default rule is:

> **Characterize first, refactor second.**

For a complex path, identify its semantic mutations, X11/EWMH/UI effects,
ordering constraints, and current tests before changing its structure.

---

## 3. The TAP model

TAP stands for:

- **T — Transition**
- **A — Authority**
- **P — Projection**

The conceptual runtime flow is:

```text
input / observation
        |
        v
interpretation + existing policy
        |
        v
Transition: T(A)
        |
        v
new authoritative state: A'
        |
        v
Projection: P(A')
        |
        +----> X11 state
        +----> ICCCM / EWMH properties
        +----> native UI
```

Equivalently:

```text
T(A) -> A' -> P(A')
```

This is a responsibility model, not a required three-pass runtime engine.
X11-specific transition protocols may need to interleave semantic decisions and
projection operations to preserve ordering, paint behavior, or asynchronous
protocol causality.

For example, workspace switching intentionally maps the incoming workspace
before unmapping the outgoing workspace. TAP must preserve that ordering; it
must not force a simplistic "commit everything, then redraw everything" model.

### 3.1 Transition

A Transition is an operation that changes Box2430's semantic truth.

Examples include:

- activate a monitor;
- activate a workspace;
- select/focus a client;
- move a client between workspaces;
- manage or unmanage a client;
- change workspace mode;
- change snap/maximize/fullscreen semantic state;
- reconcile monitor topology;
- change urgency or other semantic client state.

A semantic transition should take a valid authoritative state to another valid
authoritative state:

```text
valid A -> transition -> valid A'
```

The implementation may pass through temporary internal/X11 states while the
operation is in progress. The invariant requirement applies to the completed
semantic transition, not every individual C statement or X request.

### 3.2 Authority

Authority is the state Box2430 treats as semantic truth.

A useful test is:

> If all external presentation disappeared, which state would Box2430 need in
> order to reconstruct what it believes the desktop world is?

Authority includes concepts such as:

- client ownership and workspace membership;
- monitor/workspace relationships;
- each monitor's active workspace;
- selected monitor;
- semantic focused client;
- workspace-local stable, stack, and focus-history ordering;
- semantic/restore geometry;
- snap, maximize, fullscreen intent/effective state;
- workspace mode;
- urgency and relevant client policy state.

Authority does **not** mean Box2430 owns the entire X server. Override-redirect
windows and other external actors have their own lifecycle and protocol state.
The TAP model must preserve those authority boundaries.

### 3.3 Projection

Projection is the externally observable realization of authoritative state.

Examples include:

- X mapped/unmapped state;
- actual client X geometry;
- actual root stacking realization;
- X input focus and `WM_TAKE_FOCUS` delivery;
- border appearance and passive button-grab presentation;
- `_NET_ACTIVE_WINDOW`;
- `_NET_WORKAREA`;
- `_NET_CLIENT_LIST` / `_NET_CLIENT_LIST_STACKING`;
- native bar and MONOCLE tab rendering;
- tray placement derived from selected-monitor behavior.

Projection should be derived from semantic truth where practical. It should not
silently become a second independent source of semantic truth.

---

## 4. Inputs are outside TAP's semantic core

The same semantic transition may be requested by different input sources.

Current and future inputs include:

```text
key bindings
mouse bindings
native bar / tab interaction
client requests
X observations
future IPC
```

These inputs do not all have the same authority.

### 4.1 User intent

User commands and configured native-UI interactions usually request semantic
transitions directly, subject to normal validation and existing policy.

### 4.2 Client request

A client request is not authoritative state.

The existing pattern should remain:

```text
client request
    -> existing WM policy
    -> accept / transform / reject
    -> maybe semantic Transition
```

Important examples include:

- `_NET_ACTIVE_WINDOW`;
- `ConfigureRequest`;
- `_NET_WM_STATE` fullscreen/maximize requests;
- `MapRequest`.

A client must not gain authority over monitor/workspace/focus/geometry policy
merely because it can emit a protocol request.

### 4.3 X observation

An X event may report something that has already happened. It is not
necessarily permission to redefine Box2430 semantic state.

Important examples include:

- `FocusIn`;
- `UnmapNotify`;
- `DestroyNotify`;
- `PropertyNotify`;
- topology observations.

Some observations legitimately cause semantic transitions, such as a destroyed
managed client being unmanaged. Others may only require reconciliation of a
projection with existing Authority.

For example, current focus compatibility behavior can reassert the WM's chosen
X input focus when the observed X focus diverges from Box2430 semantic focus.
That observation must not automatically become a new semantic focus decision.

Do not introduce a generic event-router framework to express this distinction.
The current explicit X11 event loop may remain. The important part is that event
handlers converge on clear semantic operations instead of rebuilding semantic
side-effect sequences independently.

---

## 5. Authoritative domains in current V1.7

These are conceptual domains. They do not imply new structs or source files.
Prefer recognizing a concept before introducing an object for it.

### 5.1 Ownership domain

Current truth:

- every managed ordinary `Client` appears in the WM-wide client ownership list;
- every managed ordinary client belongs to exactly one `Workspace`;
- every `Workspace` belongs to exactly one `Monitor`;
- moving a client changes ownership coherently rather than cloning or partially
  attaching the client to two workspaces.

### 5.2 Workspace-ordering domain

A workspace deliberately maintains several different views of its clients:

- membership chain;
- stable/tab order;
- bottom-to-top stack order;
- focus-history order.

These are not interchangeable.

Current semantics include:

- normal focus changes do not change stable/tab order;
- raise/lower changes stack order rather than stable order;
- focus-history order records recent semantic focus preference;
- MONOCLE tabs use stable order;
- focus fallback uses focus history first and stable order second.

### 5.3 Interaction / seat domain

"Seat" is a useful conceptual name for the coherent interaction state of the
single X11 seat currently supported by Box2430. This refactor does **not** imply
a `Seat` struct, `seat.c`, or multi-seat support.

The important state includes:

```text
selected_monitor
selected_monitor->active_workspace
focused_client | NULL
workspace-local remembered/preferred focus
```

`selected_monitor` is semantically meaningful even when `focused_client == NULL`.
It determines the target context for monitor/workspace/mode commands and the
current single-tray monitor policy.

Client-specific semantic focus normally implies the client's monitor becomes
selected. The inverse is not guaranteed: a selected monitor or active workspace
may have no focusable client.

### 5.4 Geometry and presentation-intent domain

Current V1.7 already models this domain well.

Important semantic state includes:

```text
geometry
normal_geometry
snap_state
maximized
fullscreen
user_fullscreen
client_fullscreen
workspace mode
```

Temporary MONOCLE and real-fullscreen presentation must not destroy FREE/restore
geometry.

### 5.5 Special-window and external-authority domain

Dock, Desktop, and Notification windows are represented separately from ordinary
managed clients. Tray owner/host windows and XEmbed icons also follow their own
protocol rules.

The refactor must not collapse these actors into ordinary client semantics for
architectural uniformity.

Box2430 controls its own semantic stacking and native UI, but must continue to
respect external override-redirect behavior such as notification overlays.

---

## 6. Stable-state invariant catalog

These invariants describe states that should hold after a completed semantic
transition. If code review discovers an established V1.7 exception, preserve the
behavior and refine the invariant rather than forcing the code to fit this text.

### 6.1 Ownership invariants

For every managed ordinary client `c`:

```text
c belongs to exactly one Workspace
c is present exactly once in the WM-wide Client list
c is present in the corresponding workspace membership/order structures
```

For every workspace `w`:

```text
w->monitor is its owning Monitor
```

For every monitor `m`:

```text
m->active_workspace belongs to m
```

### 6.2 Workspace-order invariants

Within one workspace, membership, stable/tab order, and stack order represent
the same set of managed ordinary clients.

Focus history is a subset/order over clients belonging to that workspace.

A client must not remain linked into a previous workspace's tab/stack/focus
chains after ownership transfer.

### 6.3 Visibility invariant

Current semantic visibility is derived by:

```c
client->workspace == client->workspace->monitor->active_workspace
```

This is the meaning represented by the current `client_is_visible()` helper.

Important distinction:

```text
semantic visibility != instantaneous X mappedness during a transition
```

Workspace switching deliberately uses ordered mapping/unmapping, so a transient
moment may contain both outgoing and incoming mapped windows. The completed
state must converge to semantic visibility.

### 6.4 Interaction / seat coherence invariants

After a completed semantic interaction transition:

```text
focused_client == NULL
OR
(
    focused_client is semantically focusable
    AND focused_client belongs to its monitor's active workspace
    AND focused_client->workspace->monitor == selected_monitor
)
```

`focused_client == NULL` does **not** imply that `selected_monitor` is undefined.
An empty or non-focusable active workspace still has a selected monitor and an
active workspace.

Workspace-local focus history/preference remains meaningful for inactive
workspaces. It must not be confused with the single current X input focus.

### 6.5 Focus and stacking invariant

Semantic focus and semantic stacking are separate.

Current behavior must remain:

- focusing an ordinary existing client does not inherently raise it;
- `raise_on_focus` may explicitly couple the two;
- operations such as `raise`, `lower`, `raise_on_map`, or explicit MONOCLE
  selection may change stacking;
- MONOCLE intentionally ensures the selected/focused client becomes visible at
  the top of overlapping ordinary clients.

Do not turn "focus" into "focus + raise" globally during the refactor.

### 6.6 Focusability invariant

A client is semantically focusable when the current ICCCM metadata says it
accepts direct input or supports `WM_TAKE_FOCUS`.

Do not replace this with a weaker "managed client" check.

### 6.7 Geometry invariants

Preserve the current distinction between semantic/restore geometry and temporary
presentation geometry.

The current presentation precedence is effectively:

```text
real fullscreen
    > MONOCLE presentation
    > maximized presentation
    > snap presentation
    > FREE semantic geometry
```

Existing state relationships must remain coherent, including the current mutual
exclusion behavior between snap and maximize.

MONOCLE and real fullscreen must not overwrite the geometry needed to restore
FREE/snap/maximize behavior.

### 6.8 Requested state vs. effective state

Client protocol requests are not automatically effective WM state.

Fullscreen is the clearest current example: client fullscreen request state,
user fullscreen intent, configured fullscreen policy, and effective real
fullscreen are distinct concepts.

Do not collapse request state into effective state merely to simplify the
refactor.

### 6.9 UI/EWMH mirror invariant

Native UI and EWMH properties should reflect Box2430 semantic state according to
existing compatibility rules. They are not independent sources of WM truth.

Examples:

- `_NET_ACTIVE_WINDOW` mirrors semantic focus behavior;
- `_NET_WORKAREA` follows current selected-monitor/workarea behavior;
- bar workspace/mode/title state derives from monitor/workspace/client state;
- client-list properties derive from managed/stacking state.

Existing compatibility exceptions or protocol-specific representations must be
preserved.

---

## 7. Transition/protocol invariants

Some important V1.7 behavior is not captured by a final-state predicate. The
ordering of side effects is itself part of the behavioral contract.

### 7.1 Workspace-switch paint ordering

Current `workspace_activate()` deliberately follows an ordered presentation
protocol similar to:

```text
identify incoming focus target
materialize incoming geometry
select target monitor / update relevant monitor state
commit active workspace
map/raise incoming workspace clients
establish semantic/X focus
unmap outgoing workspace clients
refresh native UI / stacking
```

The critical behavioral property is that incoming content is made available
before outgoing content is hidden, avoiding background flash and preserving the
current paint ordering.

Do not replace this with a mechanically "cleaner" sequence that first unmaps the
old workspace and later maps the new one.

### 7.2 WM-generated unmap causality

`Client.ignored_unmaps` represents outstanding X observations caused by
Box2430's own `XUnmapWindow` operations.

Conceptually:

```text
WM decides client should be hidden
    -> ignored_unmaps++
    -> XUnmapWindow
    -> later UnmapNotify
    -> consume expected notification
```

This is not ordinary semantic client state; it is an asynchronous protocol
causality token. Do not remove or reinterpret it as withdrawal state.

A genuine client withdrawal must continue to be distinguished from a WM-caused
workspace hide.

### 7.3 Unmanage/focus-fallback ordering

When the focused client disappears, fallback selection may depend on the
client's still-valid focus/tab links.

Current behavior computes/resolves the focus fallback before destructive
workspace-order unlinking. Preserve this dependency unless an equivalent
explicit plan is introduced and tested.

### 7.4 Monitor-topology plan/commit ordering

Current monitor reconciliation is a reference implementation for TAP-like
separation and should generally be preserved.

It already follows an explicit shape:

```text
observe/match topology
    -> build MonitorTopologyPlan
    -> mutate semantic client ownership/latent geometry without presentation
    -> commit coherent monitor/workspace world
    -> compute workareas
    -> create/retarget UI resources
    -> clamp/rematerialize geometry
    -> reconcile mapping
    -> resolve focus
    -> refresh EWMH/UI/stacking
```

Do not flatten this back into incremental monitor mutation mixed with arbitrary
focus/mapping effects.

### 7.5 Focus observation vs. focus decision

Current `FocusIn` compatibility behavior treats X focus as an observation, not as
a general semantic focus-authority channel.

If an observed ordinary-client focus conflicts with Box2430's chosen semantic
focus, the WM may reassert the expected X input focus without performing a new
semantic focus transition.

A refactor should make the distinction between:

```text
change semantic focus
```

and:

```text
reassert/project current semantic focus into X
```

more explicit, not less.

### 7.6 Stacking vs. external override-redirect overlays

`enforce_stacking()` materializes Box2430's intended order for managed clients,
native UI, special windows, and fullscreen behavior, while current V1.7 also
avoids repeatedly raising native UI above unrelated override-redirect
notification overlays.

Do not reinterpret "Box2430 authoritative stacking" as authority over every
window in the root stack.

---

## 8. Existing code that already demonstrates the desired architecture

The refactor should extend patterns that already work well in V1.7 rather than
inventing a new framework.

### 8.1 Geometry: semantic state vs. materialized geometry

Useful reference helpers include:

```c
commit_client_geometry()
present_client_geometry()
materialize_client_geometry()
```

This area already clearly distinguishes semantic/restore geometry from temporary
MONOCLE/fullscreen presentation.

Treat it as a model for responsibility separation, not a target for broad
rewriting.

### 8.2 Monitor topology: plan, coherent semantic commit, then realization

`MonitorTopologyPlan` and monitor reconciliation are the strongest existing
example of a complex transition avoiding premature focus/mapping/presentation
side effects.

Treat this area as a reference pattern.

### 8.3 Stacking: authoritative order plus realization

Workspace `stack_head` / `stack_tail` represent semantic ordinary-client stack
order. `client_raise()` / `client_lower()` mutate that order and
`enforce_stacking()` realizes it in X.

This is already close to the desired TAP mental model.

### 8.4 Visibility predicate and mapping reconciliation

`client_is_visible()` is a good example of a pure semantic predicate.

`reconcile_client_mapping()` is a good example of deriving mappedness from that
predicate while respecting WM-generated-unmap accounting.

The current repository still contains direct map/unmap paths where transition
ordering matters. Do not blindly replace all direct operations with generic
reconciliation.

### 8.5 InitialPolicy and client-request policy

The current management path already computes `InitialPolicy` before committing
many management decisions. Fullscreen and `_NET_ACTIVE_WINDOW` behavior also
already separate client request from WM policy.

These patterns support the TAP direction and should remain explicit.

### 8.6 CommandContext and command dispatch

Current command handling already lets different user-interaction sources enter
shared command semantics instead of duplicating every operation in event
handlers.

Future IPC should eventually reuse the same semantic core rather than becoming a
new side-effect path, but IPC itself is outside this refactor.

---

## 9. Primary refactor hotspots

The following areas are the main candidates because they currently combine
several semantic domains and/or presentation effects. This list identifies where
to investigate; it is not permission to rewrite all of them at once.

### 9.1 `focus_client()`

Current responsibilities include both Authority and Projection:

- mutate `wm->model.focused_client`;
- implicitly select the focused client's monitor;
- update workspace focus history;
- clear urgency;
- refresh old/new borders;
- change passive button grabs;
- perform X input focus / `WM_TAKE_FOCUS`;
- update `_NET_ACTIVE_WINDOW`;
- update native UI;
- optionally raise.

The refactor should make the distinction between semantic focus/interaction
state and X focus projection clearer.

Do not change current focus policy while doing this.

### 9.2 `workspace_activate()`

This operation coordinates:

- selected monitor;
- active workspace;
- workspace focus target;
- geometry materialization;
- incoming mapping/raising;
- focus;
- outgoing unmapping;
- workarea/EWMH/native-UI refresh;
- final stacking.

It is a semantic transition plus an intentionally ordered presentation protocol.
Refactor for explicit responsibility, not for artificial purity.

### 9.3 `monitor_select()` and other direct `selected_monitor` mutation sites

`selected_monitor` is part of interaction Authority, but current code mutates it
from several paths, sometimes as a side effect of `focus_client()`.

The refactor should make monitor selection/activation semantics easier to trace
without assuming that every selected monitor has a focused client.

### 9.4 `client_move_to_workspace()`

This is the clearest multi-domain hotspot. It currently spans:

- source focus fallback;
- mapping decisions;
- workspace ownership transfer;
- stable/stack/focus-order relinking;
- optional cross-monitor latent-geometry translation/clamping;
- geometry materialization;
- optional follow behavior;
- selected-monitor/workspace activation;
- focus;
- raise;
- native UI refresh.

Before changing this function, characterize all callers and the exact behavior of
`follow` and `translate_monitor_geometry`.

A good refactor may factor semantic sub-operations or explicit plans, but must
preserve the existing transaction and its visible ordering.

### 9.5 Manage/unmanage and visibility glue

`manage_window()` and `unmanage_client()` currently contain both semantic
lifecycle operations and necessary X protocol setup/cleanup.

Do not attempt to make them purely semantic. Instead, clarify which pieces are
lifecycle Authority, which are policy, and which X effects are mandatory
protocol realization.

Direct mapping/unmapping around manage, workspace activation, movement, and
topology should be reviewed for a consistent semantic-visibility contract while
preserving transition-specific ordering.

---

## 10. Areas that should not be rewritten merely for TAP symmetry

Unless a concrete dependency requires it, do not broadly restructure:

- `materialize_client_geometry()` and the semantic/presentation geometry model;
- monitor topology planning/reconciliation;
- stable/tab/stack/focus-order separation;
- `enforce_stacking()`'s role as the main stacking realization helper;
- the native UI rendering model;
- tray/XEmbed protocol implementation;
- the explicit `switch`-based X11 event loop;
- strict atomic configuration loading.

A large `handle_event()` is not by itself the architectural problem. Splitting it
into many handlers without reducing scattered semantic mutation would only move
code around.

---

## 11. Case studies

These examples explain how to reason with TAP. They describe responsibility
boundaries, not mandatory new C types or a required multi-pass implementation.

### 11.1 Workspace activation

Suppose a user interaction requests activation of workspace `W3` on monitor
`M2`.

Interpretation determines the semantic operation:

```text
activate workspace M2/W3
```

The Transition must establish coherent Authority:

```text
selected_monitor = M2
M2.active_workspace = W3
focused_client = workspace_focus_target(W3) | NULL
```

The absence of a focusable client changes the focus result; it does not make
monitor/workspace activation incomplete.

Projection then realizes the result through the existing ordered protocol:

```text
materialize/map incoming
establish focus projection when applicable
unmap outgoing
refresh bar/EWMH/stacking
```

The UI should render current authoritative workspace/mode/title state; it should
not need to know which input originally caused the activation.

### 11.2 Move a client to another monitor/workspace with follow

A move-with-follow operation may change several authoritative domains as one
semantic transaction:

```text
client ownership: source workspace -> destination workspace
workspace-order membership: source -> destination
latent geometry coordinate space: old monitor -> new monitor
selected monitor: possibly source -> destination
semantic focused client: possibly fallback/current -> moved client
```

After that semantic operation, projection realizes:

```text
mapping/visibility
materialized geometry
X input focus
stacking
EWMH
native UI
```

The current code may interleave these effects where required for correct
behavior. The TAP goal is to make responsibility explicit and avoid relying on
incidental side effects, not to force all X calls to the end of the function.

### 11.3 X focus observation

Suppose Box2430 Authority says:

```text
focused_client = A
```

but a `FocusIn` observation reports another ordinary managed client.

This is not automatically:

```text
Transition: focused_client = observed client
```

Under current compatibility policy it may instead be:

```text
Authority remains focused_client = A
Projection is inconsistent
re-project X input focus to A
```

This case demonstrates why X observations and semantic transitions must remain
distinct.

### 11.4 WM-caused unmap observation

Box2430 hides an inactive-workspace client:

```text
Authority: client is semantically not visible
Projection: ignored_unmaps++, XUnmapWindow(client)
```

Later:

```text
X server -> UnmapNotify(client)
```

Interpretation recognizes the outstanding WM-caused unmap and consumes it
without treating it as client withdrawal.

A genuine withdrawal follows a different lifecycle transition.

This demonstrates that TAP is a loop around an asynchronous X server, not a
one-way MVC pipeline:

```text
Authority -> Projection -> X observation -> interpretation -> maybe Transition
```

---

## 12. Refactor procedure for an Agent

For every path being changed, answer these questions before editing code:

1. What current behavior does this path implement?
2. Which input category reaches it: user intent, client request, or X
   observation?
3. Which authoritative state does it read?
4. Which authoritative state does it mutate?
5. Which stable-state invariants must hold after it completes?
6. Which X11/EWMH/UI operations are projections of that state?
7. Does the ordering of those projection operations itself affect current
   behavior?
8. Which existing tests cover the path?
9. Which behavior is currently only characterized by implementation and needs a
   regression/characterization test before restructuring?

During the refactor:

- prefer named semantic operations over repeated side-effect sequences;
- prefer small predicates for important invariants;
- reduce direct mutation of authoritative fields outside the operations that own
  those transitions;
- separate "semantic focus changed" from "project/reassert current X focus";
- keep policy decisions explicit;
- preserve protocol causality such as `ignored_unmaps`;
- preserve X11 paint/stack/focus ordering when current behavior depends on it;
- do not add a new abstraction only because its name fits TAP;
- do not broaden scope while moving code;
- keep changes reviewable and behavior-preserving in small steps.

If the semantic contract of a path cannot be stated confidently, do not perform
a large structural rewrite of that path yet. Characterize it first.

---

## 13. Debug invariant checker

A debug-only semantic invariant checker is strongly recommended as a refactor
instrument, although its exact implementation and whether it remains permanently
are secondary to behavior preservation.

Conceptually:

```c
wm_check_invariants(wm);
```

It should avoid X11 round trips and verify only in-memory semantic coherence.

Useful checks include:

- every workspace points at its owning monitor;
- every monitor's `active_workspace` belongs to that monitor;
- every managed ordinary client belongs to exactly one workspace;
- workspace membership, stable/tab order, and stack order contain the same
  clients without duplicates/cycles;
- focus-history clients belong to the workspace and contain no duplicates;
- a non-NULL semantic focused client is focusable, belongs to an active
  workspace, and belongs to `selected_monitor`;
- snap/maximize state satisfies existing mutual-exclusion rules;
- client workspace links do not remain attached to an old workspace after
  movement/topology migration.

Do not make this checker define new behavior. It should encode invariants first
established by reading current V1.7 behavior.

A useful place to run it in debug/test builds is after a completed event or
semantic command, not necessarily inside partially completed transition
protocols.

---

## 14. Testing contract

Preserve and use the existing real-X11 testing philosophy in `DEVELOPMENT.md`.

Relevant current regression areas include:

- workspace transition mapping/focus/paint ordering;
- per-monitor workspace interaction;
- stable client/tab focus cycling;
- focus history and urgency restoration;
- `WM_TAKE_FOCUS`, InputHint, and FocusIn compatibility;
- inactive-workspace visibility vs. client withdrawal;
- semantic geometry, restore state, snap, maximize, MONOCLE, and fullscreen
  nesting;
- ConfigureRequest ownership and idempotence;
- stacking and override-redirect notification behavior;
- monitor topology reconciliation and client movement;
- native bar/workspace/tab interactions;
- tray relocation as selected monitor/topology changes;
- restart and lifecycle behavior.

When refactoring a path with an implicit behavior not already covered, prefer a
small characterization/regression scenario through the real X11 path before
moving the implementation.

Do not replace integration coverage with only internal-unit tests. The invariant
checker and focused C tests support the refactor; real X11 behavior remains the
final behavioral authority.

---

## 15. Expected architectural result

The desired result is not a visibly more abstract codebase. It is a codebase in
which a maintainer can answer, for an interaction:

```text
What semantic Transition is this?
What Authority does it change?
What Projection realizes the result?
```

A future workspace-bar click, key binding, or IPC request should ideally converge
on the same semantic workspace activation behavior instead of reconstructing its
side effects independently.

Conceptually:

```text
key binding -----------+
mouse / native UI -----+--> semantic Transition --> Authority --> Projection
future IPC ------------+
```

Likewise, a future IPC state query should read authoritative WM state rather than
reverse-engineering semantic truth from transient X presentation.

This refactor therefore improves future IPC readiness without implementing IPC
or designing an IPC object model now.

---

## 16. Final design principle

The refactor should make explicit what current V1.7 already partially practices:

> **Box2430 owns an authoritative semantic model. Inputs are interpreted through
> existing policy into semantic transitions. Completed transitions leave that
> model coherent. X11 state, ICCCM/EWMH state, and native UI are projections of
> that model, subject to the protocol-specific ordering required to preserve
> current behavior.**

Use the TAP model to expose hidden coupling, not to create a framework.

Preserve Box2430's current character:

- direct C;
- explicit state;
- explicit transitions;
- minimal abstraction;
- observable behavior over architectural cleverness;
- mature X11 practice over theoretical purity.

The best refactor is the one that makes current behavior easier to reason about
without making that behavior different.
