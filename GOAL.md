# Box2430 RandR 1.5 Monitor Backend Refactor — Goal

## Mission

Replace Box2430's Xinerama-based monitor topology discovery with a **RandR 1.5
logical-monitor backend** while preserving Box2430's existing monitor semantics,
workspace ownership model, topology reconciliation behavior, and deliberately
passive hotplug policy.

The highest-priority rule is:

> **This is a monitor observation backend refactor, not a monitor semantic redesign.**

RandR 1.5 should give Box2430 a more truthful description of the display world:
logical monitors, their geometry, and their associated physical outputs. It must
not silently redefine what a Box `Monitor` means, make connector identity own
workspace continuity, or turn Box2430 into an actively hotplug-reactive desktop
environment.

The intended architecture is:

```text
before
------
XineramaQueryScreens()
        |
        v
rectangle normalization
        |
        v
MonitorTopologyPlan
        |
        v
WMModel


after
-----
XRRGetMonitors(active = True)
        |
        v
RandR logical-monitor snapshot
(geometry + truthful output metadata)
        |
        v
existing/extended continuity matching
        |
        v
MonitorTopologyPlan
        |
        v
WMModel
```

The trigger policy remains separate:

```text
startup / existing accepted topology trigger
        |
        v
query RandR topology
        |
        v
possibly reconcile
```

There must **not** be a new path of the form:

```text
physical output hotplug
        |
        v
RandR hotplug event
        |
        v
immediate Box2430 reconciliation
```

---

## Required reading

Read these before implementation:

1. `AGENTS.md`
2. `DEVELOPMENT.md`
3. `docs/ARCHITECTURE.md`
4. `docs/IMPLEMENTATION_STYLE.md`
5. current `src/monitor.c`
6. current monitor initialization/reconciliation paths in `src/wm.c`
7. `tests/monitor_geometry_test.c`
8. `tests/xephyr_multimon.sh`
9. `tests/xephyr_topology.sh`
10. `tests/xephyr_native_bar_topology.sh`
11. `tests/xephyr_tray_topology.sh`

The current code and tests define the behavior that must survive this backend
change unless this document explicitly changes it.

Do not use the old V1.7 TAP development documents as the design authority for
this task. The current V1.8 architecture and this file are the relevant
contracts.

---

## Reference code

`ref/` is read-only. Never modify it.

Use the prepared references narrowly:

- `ref/libXrandr`
  - authoritative implementation reference for `XRRQueryVersion()`,
    `XRRGetMonitors()`, `XRRFreeMonitors()`, output-info ownership, and normal
    libXrandr memory/lifetime rules;
- `ref/xrandr`
  - practical C reference for RandR 1.5 monitor/output/name handling and the
    distinction between logical monitor names and physical output names;
- `ref/xorgproto`
  - protocol-level truth when RandR semantics are ambiguous;
- `ref/dwm`
  - reference only for the simplicity and historical Xinerama behavior that
    Box2430 is replacing; do not copy its Xinerama limitation forward.

Do **not** broaden the reference set by copying GTK/KDE/KWin-style monitor
management or hotplug policy into Box2430. Those projects solve a different
product problem and commonly react to output events that this task explicitly
must not subscribe to.

Reference implementations answer **how the protocol works**. This GOAL answers
**what Box2430 should mean**.

---

# Architectural contract

## 1. RandR 1.5 becomes the only monitor discovery backend

Replace the build/runtime requirement:

```text
x11 + xinerama + xft
```

with:

```text
x11 + xrandr >= 1.5 + xft
```

Requirements:

- remove the Xinerama dependency from the build;
- do not retain an Xinerama fallback path;
- check the server-side RandR version at WM startup;
- require RandR >= 1.5 because the implementation depends on the Monitor API;
- if RandR is unavailable or too old at startup, fail clearly rather than
  silently changing monitor semantics.

Maintaining two discovery backends would create two different truth levels for
monitor/output identity. Do not do that.

---

## 2. One active RandR logical monitor maps to one Box `Monitor`

Use the active RandR 1.5 monitor view (`XRRGetMonitors(..., True, ...)`) as the
observed logical topology.

The mapping rule is:

```text
one active XRRMonitorInfo
        =
one observed Box logical monitor
```

Do not infer monitor count by counting outputs or CRTCs.

Do not collapse two RandR logical monitors merely because they have identical
rectangles. The old duplicate-rectangle normalization existed because Xinerama
only exposed rectangles. RandR 1.5 is providing explicit logical-monitor
objects; trust that abstraction.

Therefore this old implication must disappear:

```text
same geometry => same monitor
```

Two distinct RandR logical monitors with the same geometry remain two observed
monitors.

---

## 3. Mirror and tiled-output topology is represented truthfully

A RandR logical monitor can own more than one physical output. Preserve that
relationship.

Example mirror/tiled observation:

```text
logical Monitor M0
├── DP-1
└── HDMI-1
```

Box2430 still has one semantic `Monitor` for M0:

```text
WMModel
└── Monitor M0
    └── Workspace[]
```

The associated outputs are platform topology metadata, not extra Box monitors.

Do not add a singular field whose semantics claim every Box monitor has exactly
one output, such as a guessed `output_name`.

The implementation must be able to represent:

```text
1 logical monitor -> 0 outputs   (synthetic/fallback observation only)
1 logical monitor -> 1 output    (ordinary case)
1 logical monitor -> N outputs   (mirror/tiled case)
```

Do not impose an arbitrary small fixed output-count cap merely to simplify the
structure. Use owned dynamic metadata or an equivalent truthful representation.

---

## 4. Output/monitor identity is non-authoritative platform metadata

RandR provides useful facts such as:

- logical monitor name;
- `primary`;
- `automatic`;
- associated `RROutput` objects;
- physical output/connector names such as `DP-1` or `HDMI-1`.

Retain enough of this information in a Box-owned snapshot/attachment so future
external integrations can use **truthful observed identities without making a
new live RandR query**.

Important distinction:

```text
RandR logical monitor name != necessarily physical connector name
```

A user-defined monitor can have a name such as `left` while its associated
physical output is `DP-1`. Preserve both concepts when useful; never synthesize
one from the other.

This metadata must not become the semantic identity of a Box `Monitor` in this
refactor.

In particular:

- do not make workspace ownership follow `DP-1` when that output moves;
- do not make selected-monitor continuity follow an output connector;
- do not reorder monitors because one is RandR `primary`;
- do not give `primary` or `automatic` any new user-visible behavior;
- do not make future IPC/Polybar concerns dictate WMModel ownership.

The platform metadata is observed truth associated with the current Box monitor
array. It is not new Authority for workspace/client semantics.

### Recommended ownership shape

Prefer a **Box-owned RandR topology snapshot/attachment in the runtime `WM`**
rather than introducing raw `XRRMonitorInfo *` lifetime into `WMModel`.

A good conceptual shape is:

```text
WM
├── WMModel
│   └── Monitor[]                 semantic Authority
│
└── RandR monitor snapshot        platform observation
    ├── observation[0]
    │   ├── geometry
    │   ├── logical monitor name
    │   ├── primary / automatic
    │   └── outputs[]
    │       ├── RROutput id + observed connector name
    │       └── ...
    └── ...
```

After a successful reconciliation, snapshot index and `Monitor.index` should
refer to the same current observed monitor position.

This approach is preferred because the current topology code intentionally
copies `Monitor` structs by value while staging a future array. Adding heap-owned
`outputs[]` pointers directly to `Monitor` without explicit ownership semantics
would create double-free/use-after-free risks.

An equivalent implementation is acceptable if ownership is equally explicit,
but:

> **Never keep pointers into the memory returned by `XRRGetMonitors()` after
> `XRRFreeMonitors()` and never rely on accidental shallow-copy ownership.**

Copy the factual metadata Box needs into Box-owned memory, then free the Xlib
query result normally.

---

## 5. Geometry remains the primary continuity authority

Preserve Box2430's current monitor continuity philosophy.

Current continuity is primarily spatial:

```text
exact geometry
    -> strongest continuity
then overlap
    -> larger overlap wins
then center distance
    -> nearer center wins
then deterministic tie-breaking
```

That remains the semantic rule after the backend replacement.

Example:

```text
before:
DP-1       HDMI-1
[left]     [right]

external reconfiguration:
HDMI-1     DP-1
[left]     [right]
```

Box2430 should continue to treat the left logical desktop state as the left
logical desktop state and the right state as the right state. Workspaces should
not automatically follow connector names across the screen.

### RandR identity may only break a geometry tie

RandR metadata is allowed to improve deterministic matching **only after the
geometry score is otherwise tied**.

Recommended tie-breaking order:

1. exact geometry remains stronger than non-exact geometry;
2. for multiple equal exact-geometry candidates, prefer the same observed
   output set / same logical-monitor identity where available;
3. for non-exact candidates, larger overlap remains stronger;
4. then shorter center distance remains stronger;
5. only when those geometry scores are equal may RandR identity prefer one
   candidate;
6. old/new array indices remain the final deterministic fallback.

Output-set comparisons should be semantic set comparisons, not assumptions that
RandR returns outputs in a permanently stable array order.

Most importantly:

> **RandR identity must never beat a better geometry match.**

Keep the matcher pure/testable. It must not perform Xlib queries while deciding
continuity.

---

## 6. RandR is the query backend, not the hotplug policy

This is a hard behavioral constraint.

Do **not** subscribe to RandR hotplug/topology events for this task.

Do not add a new event-driven reconciliation path via:

- `XRRSelectInput()`;
- `RROutputChangeNotifyMask`;
- `RRCrtcChangeNotifyMask`;
- `RRScreenChangeNotifyMask`;
- monitor/output notification events or equivalent RandR event hooks.

Physical unplug/replug by itself must not become a new reason for Box2430 to
migrate clients or destroy/recreate monitor state.

Keep the current observation policy:

- query topology at startup;
- keep the existing root `ConfigureNotify`-driven reconciliation path;
- other existing explicit query points may remain if the current repository has
  them;
- do not add a RandR event as a new query trigger.

The intended principle is:

> **RandR tells Box2430 the truth when Box2430 chooses to ask. RandR does not
> decide when Box2430 must react.**

If a physical disconnect does not trigger one of Box2430's existing accepted
reconciliation triggers, the current WMModel intentionally remains untouched.
That preserves the existing passive unplug/replug behavior.

If an external tool changes the root/logical topology in a way that reaches the
existing reconciliation path, Box2430 may query RandR at that point and run its
normal topology plan.

Do not introduce a `detached monitor` state in this refactor. That would be a
separate product/semantic decision.

---

## 7. Metadata-only changes are not semantic topology changes

The new backend exposes more information than Xinerama, so reconciliation must
not confuse platform metadata updates with a desktop-model transition.

Example:

```text
before:
M0 geometry = 0,0 1920x1080
outputs = [DP-1, HDMI-1]

after an accepted query:
M0 geometry = 0,0 1920x1080
outputs = [DP-1]
```

If monitor count/geometry/continuity is unchanged, this is only a metadata
refresh.

A metadata-only refresh must **not** cause:

- workspace migration;
- client ownership changes;
- latent geometry translation/clamping;
- focus recovery;
- mapping/unmapping;
- stacking changes;
- bar/tab destroy/recreate;
- tray relocation solely because metadata changed;
- EWMH changes unrelated to actual topology;
- pointer movement.

It should atomically replace/update the runtime RandR snapshot and return.

The topology plan should therefore be able to distinguish conceptually between:

```text
NO_CHANGE
METADATA_ONLY
SEMANTIC_TOPOLOGY_CHANGE
```

The exact C representation is an implementation choice, but the behavior is not.

---

## 8. Query failure must fail closed, not invent topology

Truthfulness is more important than silently fabricating a plausible topology.

### Startup

At startup:

- RandR unavailable / version < 1.5 -> fail startup clearly;
- `XRRGetMonitors()` query failure -> fail startup clearly;
- active logical monitor count above `BOX2430_MAX_MONITORS` -> fail startup
  clearly rather than collapsing everything into one giant root monitor;
- allocation failure -> normal fatal initialization failure.

### Zero active RandR monitors

A zero-monitor result is a special case rather than a protocol failure. Box2430
still owns a root framebuffer and its test/headless environments must remain
usable.

Allow one explicitly **synthetic root monitor**:

```text
geometry = current root/display geometry
RandR identity = absent
outputs = []
synthetic = true
```

This is not a guessed connector identity. It is explicitly marked as a Box
fallback logical monitor with no RandR output claim.

### Runtime reconciliation

For a query/reconciliation attempt while Box2430 is already running:

- if the new RandR observation is invalid, over capacity, or cannot be fully
  allocated/captured, discard the new observation;
- preserve the existing WMModel and previous valid platform snapshot;
- log a clear error;
- do not partially mutate topology and do not invent a fallback monitor that
  destroys the running desktop state.

A failed observation is not a topology transition.

---

## 9. RandR enumeration order does not become semantic identity

At initial startup, use the RandR monitor order as returned; do not invent a new
primary-based or connector-name-based ordering policy during this task.

At runtime, keep using continuity matching to preserve existing logical monitor
state across enumeration reorder.

`Monitor.index` remains the current Box array position, not a persistent
connector identity.

Do not sort the monitor array by:

- `primary`;
- output name;
- RandR logical monitor name;
- RROutput id.

A future product requirement can define a canonical ordering if one is actually
needed.

---

## 10. Existing WMModel semantics are frozen for this task

Do not redesign:

- per-monitor workspace ownership;
- selected-monitor semantics;
- focused-client semantics;
- monitor/workspace/client activation transitions;
- FREE/MONOCLE behavior;
- snap/maximize/fullscreen state;
- client placement rules;
- workspace focus history;
- stack/stable/tab ordering;
- special-window handling;
- bar/tray semantics;
- EWMH desktop behavior;
- TOML configuration;
- IPC (there is no IPC scope in this task).

The monitor backend may provide new platform facts, but those facts do not grant
permission to change unrelated behavior.

---

# Recommended source structure

Keep platform observation separate from pure continuity math and from the state
transition itself.

Preferred shape:

```text
src/monitor_randr.c
    RandR version preflight
    XRRGetMonitors()
    snapshot capture
    logical/output name capture
    snapshot free/compare helpers

src/monitor.c
    pure geometry / continuity matching
    optional metadata tie-break logic over already captured values
    no live Xlib queries

src/wm.c
    init Monitor Authority
    build MonitorTopologyPlan
    commit monitor/workspace/client Authority
    ordered X11/UI projection

src/box2430.h
    shared snapshot/metadata types and internal declarations as needed
```

Equivalent organization is acceptable if it preserves these boundaries. Do not
turn `wm.c` into the RandR parsing layer.

The backend should return a Box-owned observation/snapshot rather than leaking
libXrandr memory conventions into topology planning.

---

# Implementation sequence

Complete the following phases in order. Do not collapse the task into one large
rewrite.

## Phase 0 — Baseline characterization and scope freeze

Before editing behavior:

1. read the files listed above;
2. inspect all callers of the current `query_monitor_rects()`,
   `normalize_monitor_rects()`, `match_monitor_rects()`, monitor initialization,
   and `reconcile_monitors()`;
3. inspect how `Monitor` structs are copied/staged during reconciliation;
4. inspect all resources whose lifetime is monitor-local:
   - workspaces;
   - bar window / Xft draw state;
   - tab bar window / Xft draw state;
   - tray selection/placement dependencies;
   - drag preview monitor pointer;
5. run the current relevant baseline tests before changing the backend.

At minimum, when the environment allows:

```sh
make
make test
./tests/xephyr_multimon.sh
./tests/xephyr_topology.sh
./tests/xephyr_native_bar_topology.sh
./tests/xephyr_tray_topology.sh
```

Record actual failures if the environment cannot run one of them. Do not treat
an environmental limitation as permission to weaken the final contract.

Exit condition:

> The agent can describe the current monitor query -> matching -> topology plan
> -> Authority commit -> projection path and has a known-good baseline.

---

## Phase 1 — Introduce the RandR observation backend without changing WMModel

Add the RandR 1.5 backend as an isolated observation layer first.

Tasks:

1. replace build dependency plumbing from `xinerama` to `xrandr`;
2. add a startup RandR version check (`>= 1.5`);
3. add a Box-owned monitor snapshot/observation type;
4. implement a query helper around `XRRGetMonitors(root, True, ...)`;
5. copy each active logical monitor's:
   - geometry;
   - logical monitor name/Atom as useful;
   - `primary`;
   - `automatic`;
   - full associated output list;
   - physical output connector names observed at the same query point;
6. immediately release libXrandr-owned query memory after copying;
7. implement explicit snapshot cleanup;
8. implement the zero-active-monitor synthetic-root result;
9. reject over-capacity/invalid observations instead of old rectangle fallback
   normalization.

Do **not** switch `init_monitors()` or `reconcile_monitors()` to the new backend
until the observation object is independently understandable and ownership-safe.

Do not add `XRRSelectInput()`.

### Suggested focused verification

Add the smallest useful test/helper that can exercise the observation backend
against a real X server (Xvfb/Xephyr) and verify at least:

- RandR version acceptance;
- one active logical monitor produces one observation;
- geometry equals the server's logical-monitor geometry;
- returned metadata is Box-owned and remains valid after `XRRFreeMonitors()`;
- zero-monitor synthetic handling is separately testable where practical;
- cleanup is sanitizer-safe.

Do not make the test depend on a particular connector name such as `DP-1` unless
the fixture creates/controls that name.

Exit condition:

> Box2430 has a truthful, ownership-safe RandR 1.5 topology snapshot layer, but
> existing WM monitor semantics still use the old path.

---

## Phase 2 — Replace Xinerama normalization with RandR logical-monitor input

Switch initial monitor construction to consume the RandR snapshot.

Tasks:

1. remove `XineramaIsActive()` / `XineramaQueryScreens()` usage;
2. stop using duplicate rectangle normalization as monitor discovery policy;
3. initialize one Box monitor for every observed active RandR logical monitor;
4. initialize synthetic root-monitor state only for the explicit zero-monitor
   case;
5. establish/store the current valid platform snapshot alongside the current
   monitor array;
6. preserve current initial `Monitor.index` behavior using returned observation
   order; do not sort by primary/name/output;
7. keep workspace allocation and initial active-workspace semantics unchanged.

At this point a mirror/tiled monitor with multiple outputs still creates exactly
one Box `Monitor` because the logical monitor count is one.

Update or replace the old pure tests that assumed duplicate rectangles must be
collapsed. A new valid test must establish that two explicit logical-monitor
observations with identical geometry are not automatically treated as one
monitor.

Exit condition:

> Fresh startup is RandR 1.5 based, Xinerama is no longer used, and the rest of
> the runtime topology transition has not yet been unnecessarily rewritten.

---

## Phase 3 — Extend continuity matching with metadata-only tie-breaking

Before changing live reconciliation, update the pure matching layer so it can
handle explicit RandR logical monitors without losing Box's existing continuity
philosophy.

Tasks:

1. preserve exact geometry as the strongest continuity criterion;
2. preserve overlap area and center-distance behavior for non-exact changes;
3. remove assumptions that duplicate geometry means duplicate monitor;
4. use captured RandR identity/output-set information only to break otherwise
   equal geometry scores;
5. compare output associations as sets where ordering is not semantically
   meaningful;
6. keep deterministic old/new index fallback for unresolved ties;
7. keep the matcher independent from live Xlib calls.

Add focused pure tests for at least:

- exact geometry across enumeration reorder;
- insertion/removal;
- resolution/origin changes;
- overlap/center-distance continuity;
- two same-geometry logical monitors remaining distinct;
- same-geometry reorder resolved by matching output identity when available;
- output identity **not** beating a better geometry match;
- deterministic fallback when no usable metadata exists;
- synthetic/no-identity observations.

Exit condition:

> Continuity matching understands the richer observation without redefining
> connector identity as Box Monitor Authority.

---

## Phase 4 — Integrate RandR snapshots into `MonitorTopologyPlan`

Now replace the live reconciliation input while preserving the existing TAP
structure.

The desired flow is:

```text
existing accepted X observation
        |
        v
capture complete RandR snapshot
        |
        v
classify / plan
        |
        +--> invalid snapshot: discard, keep old world
        |
        +--> metadata only: replace snapshot only
        |
        `--> semantic topology change
                 |
                 v
         existing MonitorTopologyPlan
                 |
                 v
         Authority commit
                 |
                 v
         ordered projection
```

Tasks:

1. adapt `plan_monitor_topology()` (or equivalent) to compare the current valid
   snapshot with the new snapshot;
2. explicitly distinguish metadata-only changes from semantic topology changes;
3. for semantic changes, retain the current staged future-monitor-array design;
4. preserve selected-monitor continuity using the matching result;
5. preserve drag preview monitor continuity;
6. preserve client workspace ownership/migration rules;
7. preserve workspace index on migration;
8. preserve latent FREE geometry translation/clamping behavior;
9. preserve snap/maximize/fullscreen rematerialization behavior;
10. preserve focus recovery behavior;
11. preserve bar/tab/tray resource behavior;
12. atomically swap the platform snapshot only after the plan is valid and the
    new semantic world can be committed;
13. free the old snapshot exactly once after successful replacement;
14. on any runtime query/planning/allocation failure, free only the attempted
    new snapshot and leave the running world untouched.

### Metadata-only fast path

If semantic monitor topology is unchanged but RandR metadata differs:

```text
old WMModel stays exactly the same
new platform snapshot replaces old snapshot
return
```

Do not call client mapping, geometry, focus, UI recreation, workarea, or stacking
repair merely because output membership/name/primary metadata changed.

### Memory-safety warning

The current code shallow-copies `Monitor` values during staging because existing
workspace/UI pointers intentionally survive continuity. If any new heap-owned
RandR metadata is stored directly inside `Monitor`, this phase must introduce
explicit copy/move/free semantics before using that field.

Prefer the separate snapshot ownership described above so existing `Monitor`
staging does not accidentally alias/free metadata.

Exit condition:

> Startup and runtime reconciliation both use RandR 1.5 observations, while the
> existing Box topology Authority/projection behavior remains intact.

---

## Phase 5 — Preserve passive hotplug behavior and audit triggers

This phase is primarily a negative-scope audit: prove the refactor did not
silently add a new topology trigger.

Requirements:

1. keep the existing root `ConfigureNotify` reconciliation path;
2. do not add RandR event handling to `handle_event()`;
3. do not call `XRRSelectInput()` for topology/hotplug notification masks;
4. do not react directly to output/CRTC connect/disconnect;
5. do not introduce detached-monitor semantics;
6. do not add background polling/timers for output changes.

Audit the final source for accidental additions such as:

```text
XRRSelectInput
RROutputChangeNotifyMask
RRCrtcChangeNotifyMask
RRScreenChangeNotifyMask
RRNotify_OutputChange
RRNotify_CrtcChange
```

The absence of these reactive paths is intentional behavior, not an unfinished
implementation.

Also verify that the new backend does not make hidden live RandR queries from UI
or future-facing metadata accessors. External consumers should eventually read
the last accepted Box snapshot, not bypass Box's observation policy.

Exit condition:

> Replacing Xinerama with RandR has not changed when Box2430 decides to observe
> and reconcile topology.

---

## Phase 6 — Regression, sanitizer, documentation, and final audit

Run the broadest practical verification after the backend is fully integrated.

Required build/test targets when available:

```sh
make clean
make
make test
make sanitize
```

Run the monitor/topology Xephyr scenarios explicitly:

```sh
./tests/xephyr_multimon.sh
./tests/xephyr_topology.sh
./tests/xephyr_native_bar_topology.sh
./tests/xephyr_tray_topology.sh
```

Run additional Xephyr scenarios if the changes touch shared bar/tray/visual
code.

The existing topology tests must continue to verify behavior such as:

- monitor continuity through geometry changes;
- selected-monitor preservation;
- per-monitor workspace/client ownership;
- migration only when a logical monitor genuinely disappears in an accepted
  reconciliation;
- inactive workspace remembered focus;
- latent FREE geometry preservation/clamping;
- snap/maximize/fullscreen rematerialization;
- native bar lifecycle/geometry;
- stable XEmbed tray host behavior.

Do not weaken an existing test because the new backend makes it inconvenient.
Adapt fixture setup to RandR 1.5 while keeping the original behavioral assertion.

### Documentation updates

Update permanent documentation to describe the final repository state, not this
migration history.

At minimum review/update:

- `README.md`
  - build dependency becomes XRandR/RandR 1.5 rather than Xinerama;
- `DEVELOPMENT.md`
  - `pkg-config` preflight;
  - test environment dependencies;
  - monitor topology test descriptions that currently say Xinerama;
- `docs/ARCHITECTURE.md`
  - source layout if `monitor_randr.c` is added;
  - RandR 1.5 logical monitor as topology observation source;
  - non-authoritative output metadata;
  - passive/no-hotplug-subscription policy;
  - geometry-based continuity remains Box Authority;
- `docs/IMPLEMENTATION_STYLE.md`
  - add only a concise long-lived principle if useful: topology query source and
    hotplug reaction policy are separate concerns;
- `AGENTS.md`
  - remove/update any language that now incorrectly treats all output-identity
    metadata as an undecided concept;
  - retain the warning that new hotplug semantics require an explicit product
    decision.

`docs/REFERENCE.md` does not need new user-facing commands/configuration unless
this task unexpectedly changes them, which it should not.

Do not add Polybar IPC, runtime config, or external monitor commands in this
refactor merely because the new metadata makes them possible.

Exit condition:

> The repository builds against XRandR instead of Xinerama, all relevant
> behavior tests pass, documentation matches the implementation, and the final
> diff contains no accidental hotplug or monitor-semantic redesign.

---

# Detailed behavioral acceptance criteria

The completed refactor must satisfy all of the following.

## Startup

- RandR >= 1.5 is required and verified.
- Every active `XRRMonitorInfo` becomes one Box monitor.
- Same-geometry RandR monitors are not deduplicated.
- One RandR monitor with multiple outputs remains one Box monitor.
- Initial workspace allocation/count/mode remains unchanged.
- RandR `primary` does not change initial selected-monitor semantics.
- Zero active monitors produce one explicit synthetic root monitor with no fake
  output identity.

## Runtime accepted topology reconciliation

- Geometry continuity remains primary.
- RandR identity is only a geometry tie-breaker.
- Existing monitors preserve workspace/client state where geometry continuity
  says they continue.
- A genuinely removed logical monitor follows the existing migration/fallback
  behavior.
- A genuinely added logical monitor gets fresh workspace state in the existing
  manner.
- Resolution/origin changes still translate/clamp latent geometry correctly.
- Focus, snap, maximize, fullscreen, bar, tab, and tray behavior remain
  compatible with the current regression suite.

## Metadata

- Logical monitor name and physical output name are not conflated.
- Multiple output associations are retained truthfully.
- Metadata is Box-owned after the RandR query returns.
- Metadata-only changes do not trigger semantic/projection repair work.
- Runtime query failure does not partially replace valid metadata.

## Hotplug policy

- No RandR hotplug event subscription is introduced.
- Physical output disconnect alone is not a new Box topology trigger.
- No automatic client migration is added merely because an output reports
  disconnected.
- No detached-monitor model is introduced.
- Existing accepted triggers remain the only reconciliation triggers.

## Scope

- No IPC.
- No Polybar compatibility layer.
- No TOML redesign.
- No workspace-name feature.
- No global EWMH desktop redesign.
- No monitor connector-based workspace persistence policy.
- No new polling loop.
- No compositor/decoration work.

---

# TAP interpretation for this refactor

Use the current Box2430 Transition / Authority / Projection discipline.

### Observation

```text
accepted topology query point
    -> capture RandR snapshot
```

The snapshot is an **X observation**, not semantic Authority by itself.

### Transition / plan

```text
old Box monitor Authority
+ old accepted platform snapshot
+ new complete platform observation
    -> continuity match
    -> classify metadata-only vs semantic topology change
    -> build topology plan when needed
```

### Authority

For a real topology transition, Box2430 commits:

- monitor array/order for the new observed topology;
- workspace ownership continuity/migration;
- selected monitor;
- client workspace ownership and latent geometry;
- other existing semantic topology state.

RandR connector identity does not independently mutate this Authority.

### Projection

Only after/around the established Authority transition, preserve the current
ordered projection work:

- UI resources;
- workareas;
- client geometry materialization;
- mapping/unmapping;
- focus;
- stacking;
- EWMH;
- tray placement.

A metadata-only snapshot refresh has no semantic projection phase.

---

# Autonomous working rules

Work through all phases autonomously. Do not stop merely to ask about routine,
reversible implementation details that this GOAL already resolves.

Use the normal Box2430 loop:

```text
inspect
 -> characterize
 -> add/strengthen a focused test if needed
 -> make one bounded change
 -> build
 -> run the cheapest meaningful verification
 -> diagnose failures
 -> fix
 -> rerun
 -> inspect diff
 -> continue
```

A failing Xvfb/Xephyr test is a debugging signal, not permission to weaken the
contract.

Do not modify `ref/`.

Do not push/publish changes.

Do not take over the user's active graphical session. Use Xvfb/Xephyr for
agent-controlled integration tests; leave real-hardware unplug/replug smoke tests
to the user unless explicitly authorized.

If an implementation choice is genuinely not specified above, choose the option
that best preserves:

1. current Box2430 behavior;
2. truthful RandR observations;
3. simple ownership;
4. testable direct C control flow;
5. minimal new abstraction.

---

# Final report requirements

When complete, report:

1. the final architecture of the RandR observation/snapshot layer;
2. exact build dependency changes;
3. how continuity matching changed and how RandR identity is constrained to
   tie-breaking;
4. how metadata ownership/lifetime is handled;
5. explicit confirmation that no RandR hotplug event subscription was added;
6. tests actually run and their results;
7. any test not run because of an environment limitation;
8. documentation files updated;
9. a concise diff/stat summary;
10. any remaining real-hardware verification worth performing manually.

Do not claim full verification for tests that were not actually run.
