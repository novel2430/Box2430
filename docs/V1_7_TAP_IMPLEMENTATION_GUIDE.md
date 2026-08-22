# Box2430 V1.7 TAP Refactor — Implementation Guide

## Status

This document is the execution guide for the behavior-preserving TAP refactor of
Box2430 V1.7.

Read `docs/V1_7_TAP_REFACTOR.md` (or the project copy of
`V1_7_TAP_REFACTOR.md`) first. That document defines the architectural contract,
semantic invariants, and TAP terminology. This guide answers a different
question:

> **How should a coding agent execute the refactor safely, in bounded goals,
> while preserving all current WM behavior?**

The highest-priority rule remains:

> **Current V1.7 behavior is the specification. This is a refactor, not a
> redesign.**

The implementation should remain direct C in the existing Box2430 style. TAP is
a responsibility model, not a request to build an event framework, generic
transaction engine, render tree, object system, or multi-seat subsystem.

---

## 1. Reference hierarchy

Before changing a path, use evidence in this order:

1. current Box2430 documentation and explicit behavioral contracts;
2. current Box2430 regression/integration tests;
3. current Box2430 V1.7 implementation and all callers of the path;
4. mature references under `ref/`;
5. architectural preference from TAP.

A mature reference may explain *how to structure a concern*, but it does not
overrule current Box2430 behavior.

If Box2430 behavior differs from bspwm, i3, Openbox, or dwm, preserve Box2430
unless a separate behavior-changing decision has explicitly been made.

### 1.1 Reference roles

Use the local references deliberately rather than browsing them indiscriminately.

#### `ref/dwm` — simplicity and X11 baseline

Use dwm as the constraint against overengineering and for compact X11 lifecycle,
focus, mapping, stacking, and ICCCM/EWMH patterns.

Useful areas/symbols include:

- `dwm.c`: `focus()`, `focusin()`, `view()`, `arrange()`, `showhide()`,
  `restack()`, `manage()`, `unmanage()`;
- its distinction between client order and stack/focus order;
- its handling of semantic selected client versus observed X focus.

Do **not** copy dwm's willingness to combine many semantic and X side effects in
one helper merely for minimal line count. Box2430 has a denser feature surface.

#### `ref/bspwm` — primary semantic/activation reference

This is the primary implementation-style reference for this refactor.

Useful areas/symbols include:

- `src/tree.c`: `focus_node()`, `activate_node()`, `transfer_node()`;
- desktop show/hide and monitor/desktop/node focus relationships;
- `src/events.c`: raw X event interpretation;
- `src/messages.c`: socket command -> semantic operation;
- `src/history.c` and `src/stack.c`: distinct histories/orders.

Study especially the distinction between:

```text
monitor selection
    + desktop activation
    + desktop-local remembered node
    + actual focused node
```

Do **not** import bspwm's binary-tree layout model or its data model wholesale.

#### `ref/i3` — primary Authority/Projection architecture reference

Use i3 mainly for architectural reasoning, not as a structural template.

Start with its hacking/architecture documentation, then inspect:

- render/layout code;
- `src/x.c` and `x_push_changes()`;
- tree/container/workspace state;
- command handling;
- IPC query/command/event handling.

The key lesson is why i3 moved away from X requests being emitted throughout the
codebase toward an authoritative model plus a more centralized realization of X
state.

Do **not** introduce an i3-style universal container tree or centralized render
engine into Box2430.

#### `ref/openbox` — request/policy/compatibility reference

Use Openbox selectively when a path involves client requests or focus
compatibility.

Useful areas include:

- `openbox/event.c`;
- `openbox/client.c`;
- `openbox/focus.c`;
- `openbox/stacking.c`;
- `openbox/screen.c`.

The main lesson is:

```text
client request
    -> explicit WM policy
    -> accepted / transformed / denied semantic operation
```

Do not use Openbox's larger module structure as a reason to split Box2430 into
many files.

---

## 2. Existing Box2430 code to treat as reference-quality

Not every subsystem needs refactoring. Several existing V1.7 paths already
express TAP boundaries well enough and should be treated as local reference
implementations.

### 2.1 Geometry

Read in `src/wm.c`:

- `present_client_geometry()`;
- `commit_client_geometry()`;
- `materialize_client_geometry()`;
- semantic snap/maximize/fullscreen handling.

This is the clearest current example of:

```text
semantic state
    -> presentation derivation
    -> X geometry
```

Do not redesign this subsystem merely to make other code look symmetrical.

### 2.2 Monitor topology

Read:

- `src/monitor.c` topology planning helpers;
- `reconcile_monitors()` in `src/wm.c`.

This is the strongest current example of:

```text
plan
 -> semantic ownership/latent-state changes
 -> coherent commit
 -> workarea
 -> rematerialization/mapping
 -> focus/stack/UI/EWMH reconciliation
```

The existing comment that semantic ownership/latent geometry are updated before
focus/mapping/stacking/presentation helpers is an important local precedent.

### 2.3 Stacking

Read:

- workspace stack order helpers;
- `client_raise()` / `client_lower()`;
- `enforce_stacking()`.

Preserve the distinction between semantic stack order and X root-stack
realization. Preserve current native-UI/special-window ordering behavior,
including the override-redirect notification protection.

### 2.4 Command dispatch

Read `src/command.c` and the `CommandContext` path.

This already demonstrates the desired input-adapter shape:

```text
keyboard / mouse / tab / workspace bar
    -> typed command context
    -> shared semantic WM operation
```

Future IPC is **not** part of this refactor, but the resulting semantic API
should make another input adapter possible later without duplicating WM logic.

---

## 3. Refactor strategy

Execute the work as six bounded goals.

Do not combine phases merely because a later cleanup looks obvious. Each phase
must leave the repository buildable and behaviorally equivalent.

The intended dependency chain is:

```text
Goal 0  Characterize current behavior
   |
Goal 1  Make invariants executable
   |
Goal 2  Make interaction/seat authority explicit
   |
Goal 3  Consolidate activation transitions
   |
Goal 4  Refactor client ownership/move + visibility/lifecycle glue
   |
Goal 5  Projection/input convergence and final audit
```

The order is intentional. Do not begin by splitting `handle_event()` or by
inventing a generic transition type.

---

# Goal 0 — Characterize and freeze the current behavior

## Goal

Create a reliable behavioral baseline before changing architecture.

This goal should make little or no production-code change. Its purpose is to
answer exactly what the current WM does on the paths that will be refactored.

## Read first

Current Box2430:

- `docs/ARCHITECTURE.md`:
  - Focus and raise;
  - Selected monitor and focused client;
  - Workspace ordering;
  - Focus model;
  - Workspace transitions;
  - Moving clients between monitors/workspaces;
  - Command and binding path;
  - Event loop;
  - X11 compatibility boundary;
  - Architectural invariants.
- `docs/IMPLEMENTATION_STYLE.md`;
- `DEVELOPMENT.md` testing and verification sections;
- `src/wm.c`:
  - `workspace_focus_target()` / fallback helpers;
  - `focus_client()`;
  - `workspace_activate()`;
  - `monitor_select()`;
  - `client_move_to_workspace()`;
  - `manage_window()` / `unmanage_client()`;
  - `client_is_visible()` / `reconcile_client_mapping()`;
  - `reconcile_monitors()`;
  - relevant `handle_event()` cases.
- `src/command.c` callers of the above operations.

## Mature references

Only after current behavior is understood:

- bspwm: `focus_node()`, `activate_node()`, `transfer_node()`;
- dwm: `focus()`, `focusin()`, `view()`;
- Openbox: focus/client activation policy when relevant;
- i3 documentation explaining state model vs X realization.

## Work

For each target path, write a short characterization note in the working branch
or commit message before restructuring it:

```text
input(s)
semantic state read
semantic state changed
X11 side effects
EWMH/ICCCM side effects
UI side effects
ordering constraints
existing tests
```

At minimum characterize:

1. `focus_client()` and all direct callers;
2. `workspace_activate()`;
3. `monitor_select()`;
4. `client_move_to_workspace()` including `follow` and cross-monitor geometry;
5. manage/unmanage focus fallback;
6. mapping reconciliation and `ignored_unmaps`;
7. `FocusIn` compatibility behavior;
8. root-background monitor selection;
9. workspace-bar and tab-bar activation paths;
10. topology focus preservation/recovery.

Add focused characterization tests **only where current behavior is not already
encoded strongly enough**. Tests added here must describe current behavior, not
new desired behavior.

## Important current behavior to freeze

Do not accidentally change at least the following:

- focus normally does not imply raise;
- MONOCLE tab selection does couple visibility/focus with raising as currently
  implemented;
- selected monitor and focused client are distinct;
- selected monitor remains meaningful when no client is focused;
- workspace state is per-monitor;
- workspace-local focus history/fallback order is preserved;
- activating a workspace preserves the current anti-flash ordering;
- WM-generated unmaps remain distinct from client withdrawal;
- inactive-workspace clients remain managed;
- current root-click, workspace-bar, tab-bar, sloppy-focus, map-focus and
  `_NET_ACTIVE_WINDOW` behavior remains unchanged;
- current tray-selected-monitor behavior remains unchanged;
- current topology focus continuity behavior remains unchanged.

## Verification

Run at minimum the focused scenarios touching the characterized path, then the
full Xvfb suite before leaving the goal.

Relevant existing scenarios include:

- `tests/xvfb_workspace_transition.sh`;
- `tests/xvfb_workspacebar.sh`;
- `tests/xvfb_focus_cycle.sh`;
- `tests/xvfb_focus_history.sh`;
- `tests/xvfb_focus_protocol.sh`;
- `tests/xvfb_focus_compat.sh`;
- `tests/xvfb_lifecycle.sh`;
- `tests/xvfb_visibility_withdrawal.sh`;
- `tests/xvfb_semantic_geometry.sh`;
- `tests/xvfb_configure_request.sh`;
- relevant Xephyr multi-monitor/topology scenarios.

## Exit criteria

- current behavior is documented well enough to refactor without guessing;
- missing high-risk behavior has characterization coverage;
- full current tests still pass;
- no architectural production rewrite has begun.

---

# Goal 1 — Make semantic invariants executable

## Goal

Turn the most important implicit Authority assumptions into cheap, debug-only
semantic checks and small predicates.

This goal should **not** alter user-visible behavior or change ownership of
major operations.

## Primary idea

TAP becomes useful only if the code can answer:

> Is the authoritative model coherent after this transition?

Prefer explicit predicates and a debug-only invariant checker over new runtime
frameworks.

## Work

Introduce a debug-only checker along the lines of:

```c
wm_check_invariants(wm);
```

Exact naming/location is implementation-defined. Do not perform X round trips in
the checker. It checks Box2430 semantic memory only.

The checker should cover invariants confirmed by current implementation, such
as:

### Ownership

- every managed ordinary client belongs to exactly one workspace;
- every workspace points to its owning monitor;
- every monitor's `active_workspace` belongs to that monitor.

### Workspace-local orders

- membership, stable/tab order, and stack order contain coherent client sets;
- focus-history entries are a subset of clients owned by that workspace;
- list links are internally consistent and acyclic where practical to check.

### Interaction/seat coherence

After a completed semantic transition:

```text
focused_client == NULL
OR
(
    focused_client is focusable
    AND its workspace is its monitor's active workspace
    AND its monitor == selected_monitor
)
```

`selected_monitor` remains valid independently of `focused_client`.

### Geometry/presentation intent

Preserve currently established relations such as:

```text
snap_state != SNAP_NONE  => !maximized
maximized                => snap_state == SNAP_NONE
```

Do not attempt to assert transient X geometry or mappedness during an ordered
workspace transition.

### Global membership

- every workspace-owned ordinary client is also reachable from the global
  client list exactly as the existing ownership model requires.

## Where to run checks

Do not scatter assertions after every assignment. Prefer stable semantic
boundaries, for example after completed command/event transitions and after
complex ownership/topology operations.

If checking after every `handle_event()` iteration is practical in the debug
profile, it can be useful, but exclude known in-progress internal transition
windows if necessary. The invariant definition should remain about completed
semantic state, not every intermediate statement.

## Mature references

No external WM should dictate these predicates. Use references only for
comparison:

- bspwm for monitor/desktop/node focus coherence;
- i3 for the value of a coherent authoritative model;
- dwm for keeping checks simple rather than introducing runtime abstractions.

## Do not

- add a generic `Transition` object;
- add a `Seat` struct just because the concept now has a name;
- query X to define semantic truth;
- change current behavior to make an invariant easier to state;
- assert a property that current valid behavior violates.

If a proposed invariant fails against current established behavior, investigate
the invariant before changing the WM.

## Verification

Run the entire Xvfb suite in the debug build with invariant checks enabled.
Run relevant Xephyr multi-monitor/topology scenarios.

The checker should help expose hidden coupling during later goals; it is a
refactor instrument first and a permanent feature only if it remains cheap and
useful.

## Exit criteria

- core Authority invariants have executable coverage;
- current tests pass with invariant checking enabled;
- no behavior has changed;
- the checker can detect intentionally corrupted semantic state in a focused
  unit/debug experiment if such a test is practical.

---

# Goal 2 — Make interaction/seat Authority explicit

## Goal

Make the relationship among selected monitor, active workspace, semantic focused
client, and workspace-local focus memory explicit enough that callers no longer
rely on accidental side effects.

This is a conceptual **seat/interaction Authority** refactor, not a multi-seat
feature.

## Current pressure point

`focus_client()` currently combines several responsibilities:

```text
semantic focused_client mutation
selected_monitor mutation
focus-history promotion
urgency clearing
border/grab updates
X input focus / WM_TAKE_FOCUS
_NET_ACTIVE_WINDOW / workarea effects
UI refresh
optional raise_on_focus
```

The function is compact, but its semantic side effects are stronger than its
name/signature communicates. Several callers rely on those side effects to make
selected-monitor/focus state coherent.

## Desired responsibility shape

At the end of this goal, code should make a clear distinction between:

1. **semantic activation/focus changes** that may change Authority;
2. **X focus realization/reassertion** that must not silently redefine
   Authority.

A useful conceptual split is:

```text
semantic client activation/focus
    -> may update selected monitor / focused client / focus history

X focus projection/reassertion
    -> XSetInputFocus / WM_TAKE_FOCUS / active-window mirror
    -> does not independently choose semantic focus
```

Exact function names are not prescribed.

## Work

1. Audit every direct `focus_client()` caller.
2. Classify each caller as one of:
   - user semantic activation;
   - workspace/monitor activation;
   - lifecycle fallback;
   - map policy;
   - sloppy/click focus;
   - topology reconciliation;
   - client activation request;
   - X focus compatibility/reassertion.
3. Separate the FocusIn recovery path from a genuine semantic focus transition.
4. Remove reliance on “focus this client, therefore selected monitor will
   happen to become correct” where the higher-level operation semantically owns
   monitor/workspace activation.
5. Preserve current focus history, urgency, border, passive-grab,
   `WM_TAKE_FOCUS`, `_NET_ACTIVE_WINDOW`, `raise_on_focus`, and UI behavior.
6. Keep `workspace_focus_target()` / fallback semantics unless characterization
   proves a structural rename/helper extraction is useful.

## Reference guidance

### bspwm — primary

Study `focus_node()` versus `activate_node()` and how monitor/desktop/node focus
state is represented. The important lesson is that desktop-local remembered
activation and global input focus are not automatically the same operation.

Do not copy bspwm's entire focus implementation.

### dwm

Study `focus()` and `focusin()` specifically for the distinction between the
WM's selected client and an observed unexpected X focus change.

The lesson from dwm is useful; the combined side-effect shape of `focus()` is
not necessarily the target for Box2430.

### Openbox

Use only when checking compatibility behavior such as focus stealing,
`WM_TAKE_FOCUS`, or client activation policy.

## Do not

- create `seat.c` unless a later concrete code boundary proves it reduces
  coupling substantially;
- add multi-seat abstractions;
- change focus policy;
- make focus imply raise where it does not today;
- make X `FocusIn` authoritative by default;
- change workspace focus fallback ordering.

## Tests / verification

At minimum exercise:

- focus cycle;
- focus history restoration;
- `WM_TAKE_FOCUS` / InputHint behavior;
- FocusIn compatibility;
- urgency clearing/preservation;
- root-background monitor selection;
- multi-monitor selection with focusable and empty active workspaces;
- tab focus behavior in MONOCLE;
- map focus and `raise_on_focus` behavior.

Prefer strengthening a multi-monitor interaction test here if current coverage
does not fully characterize selected-monitor/focused-client coherence.

## Exit criteria

- semantic focus changes and X focus reassertion are visibly distinct in code;
- selected-monitor/focused-client coherence no longer depends on an accidental
  side effect hidden inside a lower-level X-focus helper;
- current interaction behavior is unchanged;
- invariant checks and full tests pass.

---

# Goal 3 — Consolidate monitor/workspace/client activation transitions

## Goal

Make the main interaction transitions explicit and complete:

```text
activate/select monitor
activate workspace
activate/focus client
```

Different input adapters should converge on these semantic operations instead of
reassembling state changes themselves.

This goal is where the TAP model should become most visible in normal code.

## Current Box2430 paths to study

- `workspace_activate()`;
- `monitor_select()`;
- client click/sloppy/tab focus paths;
- root-background click path;
- workspace-bar command path;
- keyboard command path in `src/command.c`;
- `_NET_ACTIVE_WINDOW` accepted-focus path;
- topology focus recovery (without redesigning topology itself).

## Preserve the current workspace transition protocol

The current sequence is behavior, not incidental implementation detail:

```text
choose incoming focus target
prepare/materialize incoming geometry while hidden
commit active workspace
map/raise incoming clients
establish focus
unmap outgoing clients with ignored_unmaps bookkeeping
update bar / enforce final stacking
```

Do not “clean this up” into an unmap-old-first sequence or a generic final-pass
renderer if that changes paint ordering.

## Work

1. Give monitor/workspace/client activation clear semantic entry points and
   contracts.
2. Ensure a workspace activation owns all semantic state it conceptually
   changes, regardless of whether the destination has a focusable client.
3. Ensure monitor selection remains meaningful with no focused client.
4. Have keybindings, workspace-bar interactions, root clicks and other existing
   input paths reuse the same semantic operation when their current behavior is
   equivalent.
5. Keep input-specific context outside the semantic core:
   - event timestamps;
   - hit-testing;
   - command parsing;
   - button matching.
6. Keep protocol-specific ordered projection inside or directly adjacent to the
   transition when ordering is necessary. TAP does not require a separate render
   pass.
7. Reduce direct writes to core interaction Authority outside the owning
   transition functions where practical.

## Mature references

### bspwm — primary

Use `focus_node()` / `activate_node()` and monitor/desktop focus relationships as
semantic inspiration.

### i3 — secondary

Use command handling as evidence that keyboard and IPC-like command sources can
converge on semantic operations. Do not import the tree model.

### dwm — constraint

Keep the resulting API small and directly traceable.

## Do not

- introduce a generic action/route/event bus;
- force all input through string commands internally;
- merge monitor selection, workspace activation and client focus into one
  universal function with flags;
- add boolean parameters to create a new “do everything” helper unless the
  semantics are genuinely one operation;
- change pointer-warp behavior of `monitor_select()`;
- change tray migration semantics when selected monitor changes;
- change bar/tab hit-test behavior.

## Tests / verification

Focus especially on:

- workspace activation with populated and empty workspaces;
- same-monitor and cross-monitor workspace-bar activation;
- root click monitor selection;
- monitor next/prev;
- per-monitor workspace independence;
- focus fallback/history;
- workspace transition observer/paint ordering;
- MONOCLE tab behavior;
- tray location when selected monitor changes;
- Xephyr multi-monitor tests.

## Exit criteria

A reviewer should be able to answer:

> “What semantic operation caused selected monitor/workspace/focus to change?”

by following a small number of named transitions rather than grepping direct
field assignments across unrelated event handlers.

All current behavior and tests remain unchanged.

---

# Goal 4 — Refactor client ownership transfer, visibility, and lifecycle glue

## Goal

Apply the same TAP discipline to the most cross-cutting remaining transaction:
client movement between workspaces/monitors, plus the mapping/lifecycle behavior
that depends on ownership and visibility.

Do not attempt a general lifecycle state machine.

## Primary hotspot

`client_move_to_workspace()` currently spans several domains:

```text
source focus fallback
mapping
workspace ownership
workspace-local orders
cross-monitor latent geometry translation/clamp
presentation rematerialization
optional follow/seat change
workspace activation
focus
raise
UI refresh
```

This is the main place where future changes are likely to produce another
side-effect-driven path if responsibility is not made clearer.

## Current local references

Read carefully:

- `client_move_to_workspace()`;
- `unlink_workspace_orders()` / `append_workspace_orders()`;
- topology's `topology_reassign_client()` path;
- `client_is_visible()`;
- `reconcile_client_mapping()`;
- `manage_window()`;
- `unmanage_client()`;
- `ignored_unmaps` handling in `UnmapNotify`;
- drag release paths that move a client across monitors before snap/maximize.

Topology is particularly useful because it already performs a deliberately
semantic ownership reassignment without normal focus/mapping helpers.

## Work

### 4.1 Ownership transfer

Make the semantic ownership portion easy to identify:

```text
unlink old workspace-local structures
translate/clamp latent geometry if required
change client.workspace
insert new workspace-local structures
```

This does not require making it a globally reusable function if topology and
normal user movement intentionally have different contracts, but duplicated
invariant-maintenance logic should be examined carefully.

### 4.2 Follow semantics

Keep current `follow` behavior exactly intact.

Do not treat `follow` as a mere mapping flag. It is a semantic choice that can
change interaction Authority after the ownership transfer.

Prefer composing an ownership transition with the explicit activation
operations established in Goal 3 instead of manually rebuilding selected
monitor/workspace/focus side effects.

### 4.3 Visibility

`client_is_visible()` is already a strong semantic predicate:

```text
client.workspace == client.workspace.monitor.active_workspace
```

Use it consistently as the definition of **desired steady-state visibility**.

Do not confuse desired visibility with actual X mappedness during an ordered
transition.

`reconcile_client_mapping()` is a useful projection/reconciliation helper, but
it must not replace transition-specific map/unmap ordering where that ordering
is observable.

### 4.4 Lifecycle

Preserve lifecycle distinctions rather than inventing a generic enum:

```text
managed != mapped != visible
WM-generated unmap != client withdrawal
destroy != withdrawal
```

Preserve `ignored_unmaps` as causal bookkeeping between WM projection and later
X observations.

Preserve ordering requirements such as computing focus fallback before
unlinking/removing list state that the fallback algorithm needs.

## Mature references

### bspwm

Study `transfer_node()` for how a mature WM coordinates ownership, desktop
visibility and focus/activation. Use it to identify concerns, not to copy its
large combined transaction.

### Openbox

Use client desktop changes and show/hide predicates as a comparison for desired
visibility derived from semantic state.

### dwm

Use `manage()`, `unmanage()`, `showhide()` as compact lifecycle references.

### i3

Use only for the broad model/projection boundary. Do not turn client movement
into a render-tree rewrite.

## Do not

- change geometry translation/clamp behavior;
- change `--follow` or `--keep-workspace` semantics;
- change stable/tab/stack/focus-history ordering semantics;
- replace `ignored_unmaps` without a protocol-equivalent causal mechanism;
- unmap outgoing clients earlier if it changes paint behavior;
- infer semantic ownership from X mappedness;
- merge special windows or tray icons into ordinary client lifecycle.

## Tests / verification

Exercise at minimum:

- same-monitor move with and without follow;
- cross-monitor move with and without follow;
- keep-workspace behavior;
- drag cross-monitor + snap/maximize release;
- semantic geometry preservation/translation;
- inactive-workspace visibility;
- withdrawal vs WM-generated unmap;
- focus fallback when moving/removing the focused client;
- topology migration as a regression boundary;
- stacking and native UI after movement.

## Exit criteria

- ownership mutation is structurally recognizable;
- follow uses the established activation semantics rather than duplicating seat
  mutations;
- desired visibility has one clear semantic definition;
- mapping/lifecycle ordering remains behaviorally identical;
- all invariants and full tests pass.

---

# Goal 5 — Projection/input convergence and final audit

## Goal

After the semantic transitions are explicit, remove remaining accidental
cross-layer mutations and leave the code in a stable form suitable for later IPC
work — **without implementing IPC now**.

This is a convergence/audit goal, not another broad rewrite.

## 5.1 Audit Projection ownership

Search for direct low-level side effects in semantic-heavy code:

```text
XMapWindow / XUnmapWindow
XSetInputFocus
XRaiseWindow / XConfigureWindow stacking operations
_NET_* update helpers
native UI update/draw calls
selected_monitor direct assignments
focused_client direct assignments
active_workspace direct assignments
```

For each occurrence, classify it:

1. necessary ordered projection inside a semantic transition;
2. clean projection/reconciliation helper;
3. accidental duplicate side effect that should use an established semantic or
   projection operation.

Do **not** remove category (1) merely for architectural purity.

## 5.2 Audit input authority

Review relevant `handle_event()` cases and ensure they clearly fall into:

```text
User Intent
Client Request
X Observation
```

Expected patterns:

```text
user intent
    -> semantic transition

client request
    -> existing policy
    -> maybe semantic transition

X observation
    -> interpret/reconcile
    -> maybe semantic transition
```

Examples to audit:

- `ButtonPress` client/root/bar/tab cases;
- `EnterNotify` sloppy focus;
- `FocusIn` compatibility;
- `MapRequest`;
- `ConfigureRequest`;
- `_NET_ACTIVE_WINDOW`;
- `_NET_WM_STATE`;
- `UnmapNotify` / `DestroyNotify`;
- root `ConfigureNotify` topology observation.

The `switch` in `handle_event()` may remain. Splitting it is optional and should
be done only when a handler becomes clearer, not to satisfy an architectural
pattern.

## 5.3 Audit query/read semantics for future IPC readiness

Do not add IPC, but check that the code has unambiguous sources for questions a
future IPC layer will need to answer:

```text
selected monitor?
active workspace per monitor?
semantic focused client?
workspace client/order state?
semantic vs presented geometry?
snap/maximize/fullscreen state?
```

The future rule should be possible to uphold:

> **IPC reads Authority, requests Transitions, and subscribes to committed
> semantic changes; it does not directly mutate Projection.**

No protocol/schema needs to be designed now.

## 5.4 Final reference comparison

Perform a short targeted comparison, not a new research phase:

- **bspwm:** Are the interaction/activation semantics now similarly explicit
  without copying its tree model?
- **i3:** Can semantic truth be distinguished from X/UI realization in the
  refactored hotspots?
- **Openbox:** Do client requests still pass through explicit existing policy?
- **dwm:** Is the code still simple and directly traceable?

If a change makes Box2430 more abstract than necessary without reducing a real
semantic ambiguity, simplify it.

## 5.5 Documentation update

Update current architecture/development documentation only to describe the
final code that actually exists.

At minimum ensure `docs/ARCHITECTURE.md` accurately describes:

- TAP responsibility model at the level actually implemented;
- interaction/seat invariants;
- activation transitions;
- projection boundaries;
- preserved workspace/lifecycle ordering.

Do not turn user-facing documentation into a TAP tutorial.

## Full verification

Before considering the refactor complete:

1. clean build;
2. full unit/focused tests;
3. full Xvfb suite;
4. relevant Xephyr visual/multi-monitor/topology/tray scenarios;
5. sanitizer build/tests where supported;
6. real-session smoke test according to `DEVELOPMENT.md` if available.

Pay special attention to behavior that can pass state tests but still regress
visually/protocol-wise:

- background flash during workspace transition;
- focus pointer/keyboard behavior across monitors;
- border focus presentation;
- MONOCLE visibility/raise behavior;
- override-redirect notification stacking;
- tray movement with selected monitor;
- fullscreen/snap/maximize restoration;
- withdrawal/restart behavior.

## Exit criteria

The final result should satisfy all of the following:

- no user-visible behavior intentionally changed;
- current tests pass and high-risk characterization coverage has improved;
- core semantic Authority has explicit invariants;
- selected monitor/workspace/client activation is controlled by a small number
  of clear semantic transitions;
- X focus recovery is distinguishable from semantic focus changes;
- client ownership transfer no longer reconstructs seat behavior ad hoc;
- semantic visibility is clearly distinguished from actual mappedness during
  transition protocols;
- X11/EWMH/native UI side effects are recognizable as Projection or justified
  ordered projection steps;
- input handlers interpret requests/observations rather than becoming a second
  semantic implementation layer;
- geometry, topology and stacking reference-quality behavior remains intact;
- the code remains direct C and is not burdened by a generic TAP framework.

---

# Goal-mode operating rules for the coding agent

These rules apply to every goal above.

### 4.1 Start from current code, not the reference WM

For each goal:

1. read the current Box2430 implementation and callers;
2. identify current tests;
3. state the behavior/invariants being preserved;
4. only then inspect the specified mature references;
5. use references to validate structure or discover proven patterns;
6. implement the smallest Box2430-native change that clarifies responsibility.

Never begin by porting a reference implementation.

### 4.2 Keep each goal behaviorally closed

A goal is not complete if it leaves two competing ways to perform the same
semantic state change without an intentional reason.

For example, after activation consolidation, do not leave new semantic
activation functions while unrelated event handlers continue directly assigning
`selected_monitor` for equivalent interactions.

At the same time, do not mechanically eliminate every direct assignment if a
special low-level path such as topology staging has a documented reason.

### 4.3 Prefer semantic names over generic architecture names

Good:

```text
workspace_activate
monitor_select / monitor_activate
client focus/activation helper
reconcile_client_mapping
materialize_client_geometry
```

Avoid introducing abstractions named only after the architecture diagram:

```text
TransitionManager
AuthorityStore
ProjectionEngine
EventRouter
SemanticBus
```

TAP should make existing domain code clearer, not create a framework vocabulary.

### 4.4 Avoid flag-driven mega-functions

If a function starts accumulating flags such as:

```text
follow
focus
raise
map
select_monitor
update_ui
translate_geometry
```

stop and reconsider whether multiple semantic operations are being collapsed.

Composition of a few explicit domain operations is preferable to one generic
function whose behavior depends on a flag matrix.

### 4.5 Preserve protocol ordering explicitly

When an ordering exists because of X11 behavior, say so in a concise code
comment and preserve it.

Examples include:

- workspace transition paint ordering;
- WM-generated unmap bookkeeping;
- focus fallback before destructive unlink;
- topology staging/commit order;
- stacking relative to known special/override-redirect windows.

Do not hide these protocols inside a generic “flush projections” helper.

### 4.6 Characterize surprises instead of fixing them incidentally

If the refactor reveals behavior that appears odd or inconsistent:

- do not silently “correct” it;
- add/inspect characterization coverage;
- record the discrepancy;
- preserve it for this refactor unless it violates an already-established
  current contract.

Behavior changes belong in separate work.

### 4.7 Verification after every meaningful step

Do not accumulate several architectural moves before testing.

A good loop is:

```text
small structural change
 -> compile
 -> focused scenario(s)
 -> invariant checks
 -> continue
```

At the end of each goal run the broader suite required by that goal.

---

# Practical review checklist

Before submitting each goal, answer these questions in the implementation
summary.

### Transition

- What semantic transition became clearer?
- Which inputs can request it?
- Did any input path retain duplicate semantic logic?
- Are client requests still subject to existing policy?
- Are X observations still treated as observations where appropriate?

### Authority

- Which authoritative fields does the transition own?
- What must be true before and after it?
- Are those invariants executable or otherwise locally obvious?
- Did the refactor accidentally make X state authoritative?

### Projection

- Which X11/EWMH/UI effects realize the new state?
- Which effects require special ordering?
- Are projection helpers being reused where semantically equivalent?
- Did the refactor change paint/focus/stack timing?

### Behavior preservation

- Which existing tests cover the behavior?
- What characterization tests were added?
- What current behavior looked surprising but was intentionally preserved?
- Were geometry/topology/stacking reference-quality paths left intact unless a
  concrete dependency required touching them?

### Simplicity

- Did the change add a framework that Box2430 does not need?
- Could a direct C helper/predicate express the same concept more clearly?
- Is the final control flow easier to trace than before?

---

# Expected end-state

The desired result is not a visibly different WM. It is a WM whose current
behavior is easier to reason about and harder to accidentally break.

A typical interaction should read conceptually like:

```text
input
  -> interpret current intent/request/observation
  -> call a named semantic transition
  -> transition leaves Authority coherent
  -> ordered projection realizes the result in X11/EWMH/native UI
```

For example, a workspace-bar interaction should not need to know all of the
following independently:

```text
selected_monitor assignment
active_workspace assignment
focus fallback
X input focus
map/unmap protocol
bar refresh
stack enforcement
```

It should identify the existing semantic operation it requests. That operation
owns the authoritative transition and preserves the established X11 projection
protocol.

Likewise, an unexpected `FocusIn` should be understandable as an observation of
X state and, where current policy requires, a reassertion of Box2430's semantic
focus — not an unrelated alternate path that silently creates new Authority.

The final architecture should therefore preserve the best properties of the
current project and its mature references:

```text
Box2430/dwm:
    direct, explicit C; easy control-flow tracing

bspwm:
    clear monitor/workspace/client activation semantics

Openbox:
    explicit authority boundary for client requests

i3:
    clear distinction between semantic model and X realization
```

The TAP refactor is complete when those ideas are visible in Box2430's own
existing model **without changing what V1.7 does**.


# Suggested goal completion report

At the end of each goal, the coding agent should report concisely in this shape:

```text
Goal completed:

Behavior characterized/preserved:
- ...

Semantic boundary changed:
- ...

Important invariants:
- ...

Reference code consulted:
- Box2430: ...
- bspwm/i3/Openbox/dwm: ...

Tests added/strengthened:
- ...

Verification run:
- ...

Known behavior intentionally left unchanged:
- ...

Remaining work for the next goal:
- ...
```

Do not report architectural success merely because code was moved or helpers were
renamed. The meaningful result is that an existing semantic transition is easier
to identify and verify while V1.7 behavior remains unchanged.
