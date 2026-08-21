# Box2430 V2 Native UI Implementation Plan

## Status

This document defines the recommended implementation sequence for Box2430 V2 Native UI.

It translates the V2 Native UI product contract into six implementation phases designed around the actual V1.5 codebase.

The primary goals of the sequence are:

- preserve the stabilized V1.5 WM semantics;
- keep each phase independently buildable and testable;
- separate geometry/WM integration risk from rendering complexity;
- reuse mature X11 implementation precedent wherever practical;
- avoid introducing a generic panel, widget framework, or second WM state machine;
- defer XEmbed systray complexity until the native bar is otherwise complete.

This document is an implementation plan, not a replacement for:

- `V2_NATIVE_UI_CONTRACT.md`;
- `V2_CONFIG_MIGRATION.md`;
- the existing architectural and development documentation.

If this document conflicts with the Native UI Contract, the contract is authoritative.

---

# 1. Implementation principles

## 1.1 Preserve V1.5 semantic ownership

V2 Native UI must remain a presentation subsystem over the existing WM state.

The existing WM core remains authoritative for:

- monitors;
- workspaces;
- active workspace selection;
- FREE/MONOCLE mode;
- semantic focus targets;
- client lifecycle;
- client stacking;
- urgency;
- fullscreen;
- maximize;
- snap;
- semantic geometry;
- border width;
- external struts;
- monitor topology reconciliation.

The UI layer may read these states and ask the WM to execute existing semantic transitions.

It must not create parallel state.

For example:

```text
bar workspace click
    -> identify workspace index
    -> invoke normal workspace activation path
```

not:

```text
bar workspace click
    -> directly mutate monitor->active_workspace
```

The same rule applies to focus, modes, stacking, and monitor selection.

---

## 1.2 Extend existing integration points rather than creating new pipelines

The current V1.5 code already has useful centralized integration points.

In particular, implementation should preserve and extend the roles of concepts equivalent to:

```text
calculate_workareas()
recompute_workareas()

enforce_stacking()

workspace_focus_target()

monitor topology reconciliation

main poll() event loop
```

V2 should attach native UI behavior to these existing transitions rather than creating parallel geometry, stacking, focus, or timer systems.

The intended long-term shape remains approximately:

```text
WM semantic state
      |
      v
UI presentation
      |
      +-- native bars
      +-- MONOCLE tabs
      +-- borders

Tray protocol
      |
      +-- hosted inside native bar allocation
```

---

## 1.3 Reference mature code selectively

The local `ref/` directory should be treated as a reference corpus.

Recommended shape:

```text
ref/
├── dwm/
├── dwm-patches/
│   ├── centretitle.diff
│   └── systray.diff
├── spectrwm/
├── tabbed/
├── stalonetray/
└── specs/
    ├── system-tray.html
    └── xembed.html
```

Not every reference has equal authority.

### Primary implementation references

Use these when their problem closely matches Box2430:

```text
ref/dwm/dwm.c
ref/dwm/drw.c
ref/dwm/drw.h
ref/dwm-patches/systray.diff
```

### Focused secondary references

Use these for specific design problems:

```text
ref/dwm-patches/centretitle.diff
ref/spectrwm/
ref/tabbed/
```

### Protocol / cross-check references

Use these when XEmbed or system-tray behavior is unclear:

```text
ref/specs/system-tray.html
ref/specs/xembed.html
ref/stalonetray/
```

`stalonetray` should primarily be treated as a second independent implementation for checking protocol interpretation rather than as the architectural model for Box2430.

The purpose of the references is:

> avoid reinventing mature low-level behavior.

It is not:

> make Box2430 structurally resemble another WM.

---

# 2. Phase overview

The recommended V2 implementation is divided into six phases:

```text
Phase 1
UI Foundation and Tab Extraction

Phase 2
V2 Configuration, Styles, and Client Labels

Phase 3
Native Bar Windows and Workarea Integration

Phase 4
Bar Layout and Core Widgets

Phase 5
Status, Clock, and UI Consolidation

Phase 6
XEmbed Systray and V2 Regression
```

The intended progression is:

```text
V1.5
  |
  | Phase 1
  v
same behavior, reusable UI layer
  |
  | Phase 2
  v
V2 configuration + V2 MONOCLE tabs
  |
  | Phase 3
  v
correct native bar geometry
  |
  | Phase 4
  v
useful native bar
  |
  | Phase 5
  v
complete non-tray native UI
  |
  | Phase 6
  v
complete V2 Native UI
```

The two highest-risk phases are deliberately separated:

- Phase 3: WM geometry/workarea integration;
- Phase 6: XEmbed/system-tray protocol.

---

# 3. Phase 1 — UI Foundation and Tab Extraction

## Goal

Introduce the V2 UI source boundary without changing visible V1.5 behavior.

Create:

```text
src/ui.c
src/ui.h
```

and move the existing MONOCLE tab rendering implementation out of `wm.c`.

This phase is primarily a refactor.

It must not introduce the V2 configuration schema or native bar yet.

---

## Why this phase comes first

V1.5 already contains many of the primitives required by V2, but they are currently embedded in tab-specific code inside `wm.c`.

Current responsibilities include concepts equivalent to:

```text
UTF-8 decoding
font fallback
text measurement
tab natural width
tab rectangle calculation
Xft text rendering
tab drawing
tab hit testing
tab window lifecycle
font/color allocation
```

If the native bar is implemented before extracting these capabilities, Box2430 is likely to acquire two separate text/rendering implementations.

Phase 1 prevents that duplication before any new UI feature is added.

---

## Recommended references

### Primary

```text
ref/dwm/drw.c
ref/dwm/drw.h
```

Study especially:

- UTF-8 decoding;
- measuring text using the same font-selection behavior as drawing;
- Xft font fallback;
- missing-glyph handling;
- clipping;
- ellipsis placement at a valid UTF-8 boundary;
- separation between measuring and rendering.

Do not necessarily copy dwm's complete `Drw` abstraction.

Box2430 only needs the useful low-level behavior.

A smaller API matching the existing codebase is preferable.

### Secondary

```text
ref/tabbed/tabbed.c
```

Use it to compare:

- tab rectangle management;
- selected/urgent states;
- pointer hit testing;
- tab interaction behavior.

Box2430 V1.5 itself remains the primary precedent for MONOCLE tab semantics.

---

## Target ownership boundary

After this phase:

### `wm.c`

Still owns:

```text
which workspace is MONOCLE
which clients belong to the workspace
tab ordering
semantic active client
focus transitions
tab commands
workspace/mode transitions
```

### `ui.c`

Owns:

```text
fonts
Xft colors
text measurement
text drawing
UTF-8 clipping/ellipsis
tab windows
tab geometry
tab drawing
tab hit testing
```

A useful rule is:

> `wm.c` determines what a tab means; `ui.c` determines where and how it is drawn.

---

## Suggested implementation shape

The exact API is not fixed, but the implementation should converge toward small concrete primitives such as:

```text
ui_text_width(...)
ui_draw_text(...)
ui_fill_rect(...)
ui_format_label(...)
ui_tab_update(...)
ui_tab_draw(...)
ui_tab_hit_test(...)
```

Avoid:

- virtual widget objects;
- inheritance;
- callback-heavy generic GUI frameworks;
- CSS-like style engines;
- independent event loops.

---

## Ellipsis

Phase 1 is the appropriate time to fix the renderer so V2 does not inherit unsafe byte-level clipping.

Required behavior:

```text
available width sufficient
    -> draw complete UTF-8 text

available width insufficient
    -> stop at valid UTF-8 boundary
    -> append ellipsis if possible
```

Measurement and rendering must use compatible font fallback behavior.

This is one of the strongest reasons to inspect `ref/dwm/drw.c`.

---

## Do not do in Phase 1

Do not add:

```text
appearance.bar
new tab config schema
workspace widget states
clock
status
tray
bar workarea reservation
border ownership refactor
```

Do not change tab state semantics yet.

V1.5 config should remain accepted exactly as before.

---

## Tests

All existing tab and WM regressions should remain green.

Pay particular attention to:

```text
tests/xvfb_tabbar.sh
tests/xephyr_visual.sh
tests/xephyr_topology.sh
```

Also retain all general V1.5 regressions because extracting UI code must not affect:

- focus;
- workspace switching;
- topology;
- fullscreen;
- special windows;
- geometry.

Additional focused tests may be added for UTF-8 clipping if practical.

---

## Completion criterion

Phase 1 is complete when:

1. V1.5 tab behavior is unchanged;
2. `wm.c` no longer implements low-level Xft/tab rendering;
3. reusable text measurement/drawing exists in `ui.c`;
4. UTF-8-safe clipping/ellipsis is available;
5. existing V1.5 tests still pass.

The user should not be able to identify Phase 1 from normal visual behavior.

---

# 4. Phase 2 — V2 Configuration, Styles, and Client Labels

## Goal

Introduce the final V2 Native UI configuration model before creating the native bar.

This phase performs the deliberate V1.5 → V2 tab configuration migration and introduces the configuration structures needed by later bar phases.

At the end of Phase 2:

- MONOCLE tabs use V2 styling;
- V2 bar configuration parses and validates;
- there may still be no native bar window.

---

## Why configuration comes before bar creation

The native bar should be built against its final data model rather than temporary hard-coded defaults that are immediately replaced.

At the same time, configuration should not be introduced before Phase 1, because the V2 style model should have a clean UI layer to consume it.

---

## Main implementation areas

Likely files:

```text
src/box2430.h
src/config.c
src/ui.c
src/ui.h
config.example.toml
tests/fixtures/
```

---

## V2 tab migration

Remove V1.5 flat fields such as:

```text
active_fg
active_bg
inactive_fg
inactive_bg
urgent_fg
urgent_bg
active_bold
inactive_bold
urgent_bold
```

Introduce:

```text
appearance.tabs
appearance.tabs.inactive
appearance.tabs.active
appearance.tabs.urgent
```

with common/state values such as:

```text
fg
bg
font_style
format
source
```

No permanent compatibility aliases should be added.

A stale V1.5 configuration containing removed keys should fail strict V2 validation.

---

## Bar configuration

Parse and validate the final:

```text
appearance.bar
appearance.bar.widgets.*
```

surface, including:

```text
enabled
position
height
padding
gap
font
font_bold
fg
bg
left
center
right
```

Supported widgets remain exactly:

```text
workspaces
mode
title
status
clock
tray
```

Duplicate widget types across all regions are errors.

---

## Style representation

Use a deliberately small representation.

Conceptually useful types include:

```text
UIFontStyle
UILabelSource
UIStyle
UILabelStyle

TabConfig
BarConfig
WorkspaceWidgetConfig
ModeWidgetConfig
TitleWidgetConfig
StatusWidgetConfig
ClockWidgetConfig
```

The exact names are not contractual.

Do not introduce a generic recursive widget tree.

---

## Style inheritance

Bar:

```text
bar defaults
    -> widget defaults
        -> widget state override
```

Tabs:

```text
tab defaults
    -> tab state override
```

Resolve styles explicitly in ordinary C code.

Do not introduce:

- selectors;
- arbitrary property maps;
- generic inheritance machinery;
- cascading theme objects.

---

## Client label source

Create one shared client-label mechanism for both:

- native bar `title`;
- MONOCLE tabs.

Supported sources:

```text
title
class
instance
```

Selection is strict.

Example:

```text
source = title
cached title = empty
```

must produce:

```text
empty label
```

not:

```text
class
instance
"(untitled)"
```

The UI should not invent fallback semantics.

---

## Label format

For normal labels, V2 format strings contain exactly one `%s`.

Do not pass an unrestricted configuration string directly to `printf`.

Prefer validating during parse and representing the value internally as something equivalent to:

```text
prefix
suffix
```

For example:

```text
"[ %s ]"
```

becomes conceptually:

```text
prefix = "[ "
suffix = " ]"
```

Rendering then becomes deterministic string composition.

The clock is the exception and continues to use `strftime()` semantics.

---

## Recommended references

### Primary

Existing Box2430 V1.5 configuration parser.

The existing strict/atomic candidate pattern should remain authoritative.

### Rendering behavior

```text
ref/dwm/drw.c
```

for how later label measurement and clipping should behave.

No external project's configuration architecture should replace Box2430's existing strict TOML model.

---

## Validation requirements

Add focused parser coverage for at least:

- unknown bar keys;
- unknown widget names;
- duplicate widgets;
- unknown tab states;
- unknown workspace states;
- unknown mode states;
- invalid `source`;
- invalid `font_style`;
- invalid colors;
- non-clock format with zero `%s`;
- non-clock format with more than one `%s`;
- old V1.5 tab state keys;
- malformed nested tables;
- atomic fallback after any failure.

Partial V2 appearance state must never leak from a rejected candidate.

---

## Completion criterion

Phase 2 is complete when:

1. final V2 UI TOML parses strictly;
2. stale flat V1.5 tab state keys are rejected;
3. MONOCLE tabs render using the V2 source/format/style model;
4. `title`, `class`, and `instance` source semantics work;
5. configuration failure remains atomic;
6. the bar configuration is ready for Phase 3 without further schema redesign.

---

# 5. Phase 3 — Native Bar Windows and Workarea Integration

## Goal

Create the native per-monitor bar and integrate it correctly into Box2430 geometry, stacking, monitor topology, and fullscreen behavior.

This phase is deliberately about **geometry correctness rather than useful widgets**.

A visually empty bar is acceptable at the end of Phase 3.

An incorrectly reserved or incorrectly stacked bar is not.

---

## Why this phase is separate

Native bar workarea reservation affects nearly every geometry-sensitive feature:

```text
FREE windows
placement
maximize
snap
MONOCLE
external DOCK struts
_NET_WORKAREA
fullscreen
multi-monitor behavior
topology changes
```

Combining all of that with widget layout would make regressions unnecessarily difficult to isolate.

---

## Recommended references

### Primary

```text
ref/dwm/dwm.c
```

Inspect the bar-related monitor lifecycle, especially the concepts represented by:

```text
updatebars()
updatebarpos()
resizebarwin()
drawbar()
```

Important lessons to extract:

- one bar window per monitor;
- bar geometry derives from monitor geometry;
- top/bottom placement modifies usable geometry;
- bar windows are WM-owned presentation windows rather than managed clients.

Do not copy dwm's tag/workspace semantics.

Box2430's monitor/workspace model remains authoritative.

---

## Native bar windows

When enabled, each Box2430 monitor owns one native bar window.

Properties:

```text
override-redirect
WM-owned
not Client
not SpecialWindow
not EWMH DOCK client
```

The bar should be created, destroyed, resized, or repositioned as monitors are reconciled.

Do not create a second monitor list inside the UI layer.

---

## Workarea pipeline

Extend the existing workarea calculation.

The required pipeline is:

```text
physical monitor geometry
        |
        v
external DOCK / strut reservation
        |
        v
native bar reservation
        |
        v
Monitor.workarea
        |
        +--> FREE geometry
        +--> snap
        +--> maximize
        +--> placement
        |
        v
MONOCLE tab reservation
        |
        v
MONOCLE client content
```

`Monitor.workarea` remains the authoritative ordinary client workarea.

The UI layer must not maintain a separate independent usable-area rectangle.

---

## `_NET_WORKAREA`

After native bar reservation:

```text
_NET_WORKAREA
```

must reflect the post-native-bar client workarea.

MONOCLE tabs remain internal presentation within that workarea and should not become an additional global `_NET_WORKAREA` reservation.

---

## Top / bottom behavior

Support both:

```text
top
bottom
```

For top:

```text
external top strut
native bar
MONOCLE tab
client content
```

For bottom:

```text
client content
MONOCLE tab
native bar
external bottom strut
```

If the native bar is disabled, the configured bar edge still determines the MONOCLE tab edge.

---

## Stacking

Extend the existing central stacking authority.

Conceptually:

```text
desktop
ordinary clients
native bar
MONOCLE tab bar
special windows / notifications / external docks
real fullscreen client
```

Do not let `ui.c` create an independent competing stacking policy.

Bar and tab positioning may occur in UI code, but relative WM stacking policy stays centralized.

---

## Fullscreen

Real fullscreen should cover:

- native bar;
- MONOCLE tabs;
- tray host later;
- other normal WM UI.

Entering fullscreen should not destroy or reconstruct UI state.

The normal sequence should remain:

```text
enter real fullscreen
    -> fullscreen geometry/border semantics
    -> enforce normal stacking authority
    -> fullscreen client ends above UI
```

Leaving fullscreen exposes the already-existing UI.

---

## Monitor topology

Bar windows must participate in the existing monitor reconciliation path.

Test:

- monitor added;
- monitor removed;
- monitor geometry changed;
- monitor order/index changes;
- selected monitor changes;
- workspace/client state survives topology reconciliation.

Avoid creating a separate native-UI topology mechanism.

---

## Do not do in Phase 3

Do not implement:

```text
workspace drawing
mode widget
title widget
status
clock
tray
```

A filled background bar is enough.

This phase should prove the geometry model before the bar becomes visually complex.

---

## Testing priorities

This is one of the highest-risk phases.

Add focused tests for:

### Workarea

```text
top bar reservation
bottom bar reservation
_NET_WORKAREA
external strut + native bar
```

### Geometry users

```text
FREE placement
maximize
snap
MONOCLE content
```

### Presentation

```text
MONOCLE tab adjacency
fullscreen covers bar and tabs
fullscreen exit restores presentation
```

### Topology

```text
bar resize
bar relocation
bar create/destroy
multi-monitor workareas
```

Existing geometry and topology tests must continue passing.

---

## Completion criterion

Phase 3 is complete when the following statement is true:

> An empty native bar can be enabled at the top or bottom of every monitor without breaking any V1.5 geometry, stacking, fullscreen, external-dock, or topology semantics.

Only after this is stable should bar widgets be added.

---

# 6. Phase 4 — Bar Layout and Core Widgets

## Goal

Implement the native bar layout engine and the three semantic widgets that depend only on already-existing WM state:

```text
workspaces
mode
title
```

At the end of this phase, the bar should already be useful for normal daily navigation.

---

## Layout model

The bar has exactly three configured regions:

```text
left
center
right
```

This is not a general layout engine.

A straightforward two-step model is sufficient:

```text
measure widgets
    ->
allocate rectangles
    ->
draw rectangles
```

The produced rectangles should also be reused for hit testing.

Do not calculate widget positions independently in the drawing and pointer-event paths.

---

## Recommended references

### Physical center

```text
ref/dwm-patches/centretitle.diff
```

Use this as precedent for the key idea:

> center against physical monitor width rather than the leftover title area.

Do not blindly copy its complete layout because Box2430 has three independently configured widget regions.

### Section measurement / status-bar structure

```text
ref/spectrwm/
```

Study its mature internal bar implementation for general precedent around:

- measuring bar content;
- allocating bar sections;
- clipping content into allocated regions;
- refreshing state-driven bar content.

Do not import spectrwm's overall bar configuration architecture.

### Text overflow

```text
ref/dwm/drw.c
```

Continue using the shared text clipping/ellipsis behavior established in Phase 1.

---

## Physical center invariant

This is Box2430-specific and must remain explicit.

For:

```text
monitor width = 1920
```

the center axis is:

```text
x = 960
```

regardless of whether:

```text
left width = 300
right width = 80
```

or:

```text
left width = 50
right width = 500
```

The center group must not become "center of remaining space".

---

## Space-pressure policy

Implement the contract directly rather than inventing a generic flexbox model.

Required order:

1. workspace, mode, clock, and tray use fixed/natural widths;
2. `status` is the first edge widget allowed to shrink toward zero;
3. center/title is limited to the largest symmetric area around the physical center that does not overlap edge groups;
4. center/title clips or ellipsizes within that region;
5. in an extremely narrow bar, edge groups themselves are clipped to the bar bounds rather than intentionally overlapping.

No scrolling.

No multiline text.

No automatic reordering.

---

## Workspace widget

Derive visual state from existing workspace semantics.

Required states:

```text
empty
occupied
active
urgent
active_urgent
```

Do not store the state separately on `Workspace`.

Resolve it from facts such as:

```text
workspace == monitor->active_workspace
workspace contains clients
workspace contains urgent client
```

State resolution:

```text
active + urgent     -> active_urgent
active              -> active
inactive + urgent   -> urgent
inactive + occupied -> occupied
otherwise           -> empty
```

The displayed value remains the existing 1-based workspace index.

No workspace-name metadata should be introduced.

---

## Workspace interaction

Only the workspace widget has built-in native bar interaction in V2.

Left click:

```text
pointer coordinates
    ->
bar item rectangle
    ->
workspace item
    ->
normal WM workspace activation path
```

Do not create `[bindings.bar]`.

Do not allow arbitrary per-widget commands.

---

## Mode widget

Map the existing workspace mode:

```text
WORKSPACE_FREE
WORKSPACE_MONOCLE
```

onto the configured:

```text
free label/style
monocle label/style
```

The mode widget is display-only.

It must not become a second mode-changing control surface.

---

## Title widget

Use the semantic focus target for the active workspace of the corresponding monitor.

Do not simply use the single global currently focused X client.

Conceptually:

```text
monitor
    ->
monitor->active_workspace
    ->
workspace semantic focus target
    ->
configured client label source
```

This allows a non-selected monitor to continue displaying a meaningful title.

Use the shared Phase 2 client-label helper for:

```text
title
class
instance
```

and the shared renderer for clipping/ellipsis.

---

## Redraw triggers

By the end of this phase, bars should update for at least:

```text
workspace activation
client manage
client unmanage
client move between workspaces
urgency change
workspace mode change
focus-target change
title/class/instance change
Expose
monitor geometry/topology change
```

Do not implement a generalized reactive graph.

Coarse explicit redraw calls are acceptable.

---

## Tests

Focus particularly on:

### Layout

```text
asymmetric left/right groups
physical center remains fixed
very long title
very narrow bar
no group overlap
```

### Workspace state

Test all:

```text
empty
occupied
active
urgent
active_urgent
```

### Per-monitor semantics

Verify:

- non-selected monitor title;
- non-selected MONOCLE active tab;
- workspace changes only affect the correct monitor.

### Interaction

Verify workspace item hit testing and activation.

---

## Completion criterion

Phase 4 is complete when a bar such as:

```text
1  2  [3]  4    [ M ]          Firefox
```

works correctly across multiple monitors, including real physical centering and workspace interaction.

At this point the native bar should already be useful without status, clock, or tray.

---

# 7. Phase 5 — Status, Clock, and UI Consolidation

## Goal

Complete all non-tray V2 Native UI features and finish presentation ownership cleanup.

This phase adds:

```text
status
clock
border presentation integration
final redraw/event cleanup
```

After this phase, XEmbed should be the only major missing V2 feature.

---

# 7.1 External status

## Source

Use the root window status channel.

Required preference:

```text
_NET_WM_NAME
    ->
fallback to WM_NAME
```

Read the initial value during startup.

Then watch relevant root `PropertyNotify` events.

This is separate from ordinary client title updates.

---

## Recommended reference

```text
ref/dwm/dwm.c
```

Inspect dwm's root-name status behavior and status redraw path.

Box2430 differs because status may appear inside configured left/center/right regions rather than a single fixed dwm status location.

The source/event model is still a useful direct precedent.

---

## Status semantics

Box2430 consumes a string.

It does not:

- execute status commands;
- supervise an external process;
- parse markup;
- parse colors;
- create status blocks;
- create named channels.

Example producer remains external:

```sh
xsetroot -name "VOL 32% | VPN"
```

The status widget applies only its configured Box2430 single-`%s` label format.

---

# 7.2 Internal clock

## Event-loop model

Keep the existing single-threaded X event loop.

Do not add:

- worker thread;
- POSIX signal timer;
- helper process;
- daemon;
- second event loop.

The current indefinite poll concept becomes conditional.

Conceptually:

```text
clock absent/not visible
    -> poll timeout = infinite

clock visible
    -> poll timeout bounded to approximately one second
```

On timeout:

```text
time()
localtime_r()
strftime()
```

Then redraw clock-containing bars when needed.

---

## Recommended reference

```text
ref/spectrwm/
```

Use spectrwm as mature precedent for combining:

- an X11 WM event loop;
- a native status bar;
- internally formatted time;
- periodic bar refresh.

Box2430 does not need spectrwm's complete bar machinery.

The important precedent is that a simple periodic wakeup inside the existing event loop is sufficient.

---

## Clock precision

V2 does not need a complicated timer scheduler.

A wakeup approximately once per second is enough.

It is acceptable to optimize slightly by avoiding redraw if the formatted output has not changed.

Do not build a generalized timer subsystem solely for one clock widget.

---

# 7.3 Border presentation

Move only border presentation resources into the UI layer.

The ownership boundary remains:

```text
WM:
    border width
    rule border enable/disable
    geometry impact
    fullscreen border width = 0

UI:
    focused border color
    unfocused border color
    urgent border color
    X color/resource lifetime
    refreshing displayed border color
```

This is primarily cleanup rather than a new feature.

Do not alter V1.5 border geometry behavior.

---

# 7.4 UI invalidation cleanup

By this stage there will be several UI redraw callers.

Consolidate them enough to keep call sites understandable.

Reasonable coarse operations could resemble:

```text
update one monitor UI
update all native bars
update MONOCLE tab
update all presentation
handle UI Expose
```

Exact function names are implementation details.

Do not introduce:

```text
signals
observers
dependency graphs
reactive nodes
generic dirty-region framework
```

The UI remains small enough for explicit invalidation.

---

## Tests

Add or extend coverage for:

### Status

```text
startup root status read
_NET_WM_NAME update
WM_NAME fallback
root PropertyNotify
long status compression
```

### Clock

```text
clock format
periodic update
no unnecessary periodic wake requirement without clock
```

### Border

```text
focus color
unfocused color
urgent color
fullscreen geometry unchanged
rule border behavior unchanged
```

Also repeat Phase 4 pressure tests with a long status string.

---

## Completion criterion

Phase 5 is complete when every V2 native UI feature except the system tray is stable:

```text
workspaces
mode
title
status
clock
MONOCLE tabs
border presentation
top/bottom native bars
```

At this point the bar layout and its final tray allocation rectangle are stable enough for XEmbed integration.

---

# 8. Phase 6 — XEmbed Systray and V2 Regression

## Goal

Implement one traditional XEmbed system tray and integrate it with the already-stable native bar.

Create:

```text
src/tray.c
src/tray.h
```

Tray icons must never become normal Box2430 clients or special windows.

This is intentionally the last implementation phase.

---

## Why tray comes last

The system tray has the highest protocol complexity but very little reason to influence the architecture of the rest of Native UI.

Waiting until Phase 6 means the tray subsystem receives an already-defined interface:

```text
is tray enabled?
which monitor is selected?
what rectangle does the tray widget own?
what is the current bar height?
```

It should not need to participate in:

- generic bar layout design;
- workspace semantics;
- client lifecycle;
- MONOCLE logic;
- style architecture.

---

## Primary reference

```text
ref/dwm-patches/systray.diff
```

Treat the official dwm systray patch as the main practical implementation precedent.

Study the complete integration, not only the icon list.

Important areas include:

```text
_NET_SYSTEM_TRAY_Sn atom
selection ownership
MANAGER announcement
SYSTEM_TRAY_REQUEST_DOCK
icon lookup/list
XAddToSaveSet
XSelectInput
XReparentWindow
_XEMBED_INFO
XEMBED_MAPPED
icon resize
icon map/unmap
DestroyNotify
PropertyNotify
tray host resize
tray relocation
shutdown cleanup
```

The patch is particularly relevant because its multi-monitor model already follows the selected monitor.

---

## Protocol references

When behavior is unclear, consult:

```text
ref/specs/system-tray.html
ref/specs/xembed.html
```

These are authoritative for protocol meaning.

Do not infer protocol behavior solely from dwm implementation convenience.

---

## Secondary implementation cross-check

Use:

```text
ref/stalonetray/
```

when the protocol or lifecycle handling in dwm is ambiguous.

Useful areas to compare include:

- XEmbed state transitions;
- embedding lifecycle;
- `_XEMBED_INFO`;
- focus/activation messages;
- selection ownership;
- selection timestamps;
- client removal;
- shutdown/reparent behavior.

Do not redesign Box2430 into a standalone tray manager based on stalonetray.

Its value here is as an independent implementation to validate protocol interpretation.

---

## Tray model

There is one X screen tray selection:

```text
_NET_SYSTEM_TRAY_Sn
```

and one actual set of embedded icon windows.

Because an XEmbed icon window cannot be simultaneously reparented into multiple monitor bars, V2 deliberately does not mirror the tray.

Required behavior:

```text
selected monitor
    -> tray widget has actual width
    -> tray host located in that allocation

all other monitors
    -> tray widget width = 0
```

When selected monitor changes:

```text
existing tray state remains alive
    ->
host/allocation relocates to selected monitor
```

Do not undock and redock all clients unnecessarily.

---

## Selection conflict

Tray ownership is optional functionality.

If another tray manager already owns:

```text
_NET_SYSTEM_TRAY_Sn
```

Box2430 should:

```text
report diagnostic to stderr
leave tray empty/disabled
continue running normally
```

Failure to acquire tray ownership must not become WM startup failure.

---

## Icon geometry

Follow mature tray behavior rather than treating icons as arbitrary normal windows.

Important details include:

- respect or normalize size hints as appropriate;
- scale icons relative to bar/tray height;
- constrain resulting geometry;
- update host width when icons appear/disappear/change size;
- avoid letting tray clients independently expand the bar;
- handle client-requested geometry through tray-specific logic.

Do not reuse normal `Client` geometry paths.

---

## Event handling

Integrate tray event routing into the existing WM event loop.

Relevant events may include:

```text
ClientMessage
PropertyNotify
MapRequest / map-related behavior
UnmapNotify
DestroyNotify
ResizeRequest
ConfigureRequest where relevant to embedded icons
SelectionClear
Expose
```

The tray subsystem should receive only events relevant to:

- tray host;
- tray icons;
- tray selection/protocol.

Ordinary WM event semantics must remain unchanged.

---

## Selection loss

If tray ownership is lost unexpectedly, the WM itself should continue running.

The tray subsystem should cleanly transition to an inactive state rather than treating selection loss as fatal ownership loss of the window manager.

Do not confuse:

```text
WM_S0 ownership
```

with:

```text
_NET_SYSTEM_TRAY_Sn ownership
```

They have very different failure semantics.

---

## Shutdown

On WM shutdown:

- release tray-related resources;
- correctly remove/reparent embedded clients where required by protocol;
- destroy tray host;
- release selection;
- avoid leaving icons in an invalid embedded state.

Compare both dwm and stalonetray cleanup behavior when uncertain.

---

## Tests

Tray testing should include focused Xephyr tests where ordinary Xvfb coverage is insufficient.

Required scenarios:

```text
selection acquisition
MANAGER announcement
dock request
one icon
multiple icons
map/unmap
icon resize
icon property changes
icon destruction
selected-monitor relocation
monitor topology change
clean WM shutdown
another tray already owns selection
tray selection loss
```

Also test the tray with representative real applications.

---

# 9. Final V2 regression pass

Phase 6 should finish with a complete regression pass rather than immediately declaring V2 complete after the first successful tray icon.

At minimum, re-run all existing V1.5 test suites plus the new Native UI tests.

Pay special attention to cross-feature combinations.

---

## 9.1 Native bar + external dock

Test:

```text
external top dock + top native bar
external bottom dock + bottom native bar
opposite-edge combinations
_NET_WORKAREA
```

---

## 9.2 Native bar + MONOCLE tabs

Test:

```text
top bar + tabs
bottom bar + tabs
tabs disabled
bar disabled + tabs enabled
```

---

## 9.3 Native bar + fullscreen

Test:

```text
real fullscreen
fake fullscreen
client fullscreen requests
exit fullscreen
```

Real fullscreen must cover UI without destroying it.

---

## 9.4 Native bar + multiple monitors

Test:

```text
independent active workspaces
independent workspace modes
semantic titles on each monitor
MONOCLE active tab on each monitor
selected-monitor tray movement
```

---

## 9.5 Topology changes

Test add/remove/reconfigure while:

```text
bar visible
MONOCLE active
tray icons present
fullscreen present
different workspaces active
```

The existing monitor reconciliation logic should remain the semantic authority.

---

## 9.6 Real-session validation

Before freezing V2, test a real X11 session with at least:

```text
terminal
browser
rofi
dunst
maim / screenshot workflow
fullscreen video/application
multiple monitors
representative XEmbed tray applications
external root-name status producer
```

The purpose is compatibility validation, not expansion of the V2 contract.

If an application exposes a genuine X11 compatibility issue, fix the compatibility bug.

Do not use final testing as justification to add:

- SNI;
- D-Bus panel infrastructure;
- arbitrary executable widgets;
- workspace names;
- generic click bindings;
- plugin systems.

---

# 10. Phase risk model

Approximate risk:

| Phase | Main risk | Relative risk |
| --- | --- | --- |
| 1 | refactor / rendering regression | Low–medium |
| 2 | parser/schema correctness | Low–medium |
| 3 | geometry/workarea/stacking semantics | High |
| 4 | layout geometry and state rendering | Medium |
| 5 | root properties / periodic wakeup / cleanup | Low–medium |
| 6 | XEmbed/system-tray protocol | Very high |

The order deliberately avoids combining the two highest-risk areas.

---

# 11. Reference guide by problem

When giving implementation work to a coding agent, point it toward the narrowest relevant reference rather than saying only "look at `ref/`".

## Text rendering / UTF-8 / fallback / ellipsis

Read:

```text
ref/dwm/drw.c
ref/dwm/drw.h
```

Use for:

```text
UTF-8 decoding
Xft text measurement
font fallback
glyph selection
ellipsis
clipping
```

Do not automatically copy dwm's entire drawing abstraction.

---

## Per-monitor native bar / usable geometry

Read:

```text
ref/dwm/dwm.c
```

Focus on the bar/monitor geometry path.

Use for:

```text
one bar per monitor
top/bottom geometry
bar window lifecycle
monitor resize/reconfigure
```

Preserve Box2430's own workarea pipeline.

---

## Root status text

Read:

```text
ref/dwm/dwm.c
```

Use for:

```text
root name as status
PropertyNotify-driven refresh
simple external status producer model
```

---

## Physical center

Read:

```text
ref/dwm-patches/centretitle.diff
```

Use for the physical-screen-center precedent.

Do not copy its title-area assumptions directly.

Box2430 still needs its own three-region pressure algorithm.

---

## MONOCLE tabs

Primary:

```text
Box2430 V1.5 implementation
```

Secondary:

```text
ref/tabbed/tabbed.c
```

Use tabbed mainly for:

```text
tab rectangles
selected/urgent presentation
pointer hit testing
focus/close interaction precedent
```

Do not replace Box2430's tab ordering or semantic focus model.

---

## Clock / periodic native bar refresh

Read:

```text
ref/spectrwm/
```

Use for precedent around:

```text
internal clock formatting
native bar update
periodic wakeup inside normal WM runtime
```

Keep Box2430's existing `poll()` architecture.

---

## Systray practical implementation

Primary:

```text
ref/dwm-patches/systray.diff
```

Use as the practical code precedent.

---

## Systray protocol meaning

Read:

```text
ref/specs/system-tray.html
ref/specs/xembed.html
```

These override assumptions derived only from implementation examples.

---

## Systray difficult lifecycle questions

Cross-check:

```text
ref/stalonetray/
```

Use when comparing:

```text
selection ownership
embedding lifecycle
XEmbed state
focus/activation
cleanup
```

---

# 12. Guidance for coding-agent prompts

Each implementation phase should explicitly tell the agent:

1. read the current Box2430 architecture/development documentation first;
2. inspect the current implementation before proposing structural changes;
3. inspect only the relevant `ref/` sources;
4. preserve the V2 contract;
5. prefer established mature X11 behavior over speculative defensive machinery;
6. keep WM semantics inside existing WM paths;
7. add focused regression tests for the phase;
8. run the existing regression suite before declaring completion.

A useful general instruction is:

> Reference implementations are precedents, not specifications for Box2430 architecture. Reuse mature low-level X11 behavior where applicable, but adapt it to Box2430's existing monitor, workspace, focus, geometry, stacking, configuration, and lifecycle ownership model.

For tray work, add:

> Use the dwm systray patch as the primary practical implementation reference. Where protocol behavior is unclear, verify it against the local System Tray and XEmbed specifications and cross-check stalonetray rather than inventing behavior.

For layout work, add:

> No reference implementation exactly matches Box2430's physical-center and compression contract. Use dwm centretitle only for the physical-center precedent, spectrwm for mature bar layout ideas, and the Box2430 contract as the authoritative layout specification.

---

# 13. V2 completion criterion

V2 Native UI should be considered ready for a freeze candidate when:

- all six phases are complete;
- final V2 configuration is authoritative;
- no temporary V1.5 UI compatibility schema remains;
- native bar works at top and bottom;
- per-monitor workareas remain semantically correct;
- all six widget types behave according to contract;
- MONOCLE tabs use the shared V2 rendering layer;
- physical center and pressure behavior are correct;
- root status and internal clock are stable;
- border presentation preserves V1.5 geometry semantics;
- XEmbed tray survives normal icon lifecycle and monitor relocation;
- tray ownership conflicts are non-fatal;
- V1.5 regressions remain green;
- focused V2 automated/Xephyr tests pass;
- normal real-session use is stable.

At that point the implementation should stop expanding its Native UI mechanisms.

Subsequent work should be treated as:

```text
bug fixing
compatibility maintenance
regression repair
```

rather than continued V2 UI feature growth.