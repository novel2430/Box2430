# Box2430 V2 Native UI Contract

## Status

This document defines the target native UI contract for the development line after the frozen Box2430 V1.5 baseline and before V2 is considered complete.

V2 is not considered complete merely because a basic bar exists. The native UI work should be carried through to a stable daily-use result matching this contract.

The contract is intentionally bounded. It adds a native bar and consolidates the existing MONOCLE tab UI, but it does not turn Box2430 into a general panel, widget-plugin, or desktop-shell framework.

## Goals

V2 Native UI should provide:

- a native per-monitor bar;
- configurable top or bottom placement;
- independently configurable native bar and MONOCLE tabs;
- shared text/font/color/rendering primitives between bar and tabs;
- workspace, mode, title, external status, clock, and tray bar widgets;
- true physical centering for the center widget group;
- configurable visual states for workspace and mode widgets;
- configurable client label source and formatting for title/tab text;
- an internal clock without requiring an external status loop;
- dwm-style external status text supplied by the user;
- an XEmbed system tray based primarily on the mature dwm systray patch behavior;
- client border presentation integrated with the UI resource layer without moving border geometry ownership out of the WM core.

## Non-goals

V2 Native UI does not add:

- workspace names;
- arbitrary executable widgets;
- CPU, RAM, network, battery, volume, weather, or other built-in telemetry widgets;
- a plugin API;
- a generic widget tree or layout framework;
- custom click commands for bar widgets;
- bar autohide;
- per-monitor themes;
- multi-monitor mirroring of the same XEmbed tray;
- SNI/StatusNotifierItem, D-Bus tray hosting, XComposite tray mirroring, or an XEmbed-to-SNI bridge;
- a new workspace, focus, stacking, or client lifecycle model.

External status generation remains the user's responsibility. Box2430 consumes text; it does not supervise a status process.

## V1.5 baseline constraints

The V2 UI must preserve the semantic behavior already stabilized in V1.5.

In particular:

- `wm.c` remains authoritative for monitors, workspaces, clients, focus, stacking, urgency, workspace mode, semantic geometry, fullscreen, maximize, snap, and lifecycle.
- UI windows are not ordinary managed clients and do not enter the `Client` or `SpecialWindow` lifecycle.
- workspace hiding remains distinct from minimization;
- managed client border width continues to participate in geometry calculations;
- fullscreen continues to use zero presentation border width and cover native UI;
- existing external DOCK/strut handling remains supported;
- existing MONOCLE tab interaction semantics remain supported;
- strict, atomic TOML configuration behavior remains the default configuration policy.

V2 UI is an additional presentation subsystem over the V1.5 semantic state, not a rewrite of that state.

## Source layout and ownership

The intended source boundary is:

```text
src/
    wm.c
    ui.c
    ui.h
    tray.c
    tray.h
    ...existing modules...
```

### `wm.c`

`wm.c` continues to own semantic state and transitions.

It owns, among other things:

- workspace activation;
- FREE/MONOCLE mode transitions;
- focus and workspace focus history;
- urgency semantics;
- client stacking;
- client border width;
- per-rule border enable/disable policy;
- workarea-triggering state changes;
- semantic client geometry;
- fullscreen behavior;
- monitor topology reconciliation.

The UI may ask the WM to perform an action, but must not directly mutate these semantics.

For example, clicking workspace 3 may be recognized by the UI, but workspace activation must still go through the WM's normal workspace transition path.

### `ui.c` / `ui.h`

The UI layer owns native presentation:

- Xft font resources and fallback fonts;
- UI colors;
- UTF-8 text measurement and drawing;
- UTF-8-safe clipping and ellipsis;
- label formatting;
- style resolution;
- native bar windows;
- bar layout;
- bar widget measurement, drawing, and hit testing;
- MONOCLE tab windows;
- tab layout, drawing, and hit testing;
- client border color resources and border-color refresh.

It must not become a second WM state machine.

The implementation should remain direct C code. The word "widget" describes the user-visible/configuration abstraction; it does not require a class hierarchy, generic object system, virtual dispatch framework, or CSS-like engine.

### `tray.c` / `tray.h`

The tray subsystem owns the XEmbed system tray protocol and tray icon lifecycle:

- `_NET_SYSTEM_TRAY_Sn` ownership;
- `MANAGER` announcement;
- dock requests;
- XEmbed state and messages;
- icon reparenting;
- icon map/unmap/resize/removal;
- tray host geometry;
- selected-monitor relocation;
- cleanup and selection release.

The mature dwm systray patch should be the primary implementation precedent. Box2430 should deviate only where its per-monitor bar and existing monitor model require it.

Tray icons are not Box2430 `Client` or `SpecialWindow` objects.

## Shared rendering primitives

Bar and tab rendering should share small concrete primitives rather than duplicate V1.5 tab drawing logic.

The exact function names are implementation details, but the responsibilities should resemble:

```text
UTF-8 decode / iteration
font fallback lookup
text width measurement
text drawing
rectangle fill
single-%s label formatting
UTF-8-safe ellipsis
style resolution
```

The renderer should support two configured font slots per UI surface:

- normal;
- bold.

The bar and tabs have independent font configuration and independent color/style trees. They share implementation, not theme state.

## Client labels

The bar title widget and MONOCLE tabs use the same client-label mechanism.

Supported sources are exactly:

- `title`;
- `class`;
- `instance`.

The selected source is strict. There is no implicit fallback chain.

If `source = "title"` and the cached title is missing or empty, the produced label is empty. Box2430 must not silently fall back to class, instance, or a built-in `(untitled)` string.

For non-clock labels, `format` is a Box2430 format containing exactly one `%s` placeholder. It is not passed to `printf` as an unrestricted user format string. The implementation should parse a prefix and suffix around the single `%s` and reject invalid formats during strict configuration validation.

Examples:

```text
%s
[ %s ]
* %s
<%s>
```

## Native bar

### Per-monitor behavior

Each Box2430 monitor has its own native bar window when the bar is enabled.

All monitor bars use the same configuration. V2 does not add per-monitor bar themes or per-monitor widget lists.

The bar is an internal override-redirect UI window and is not represented as an EWMH DOCK client.

### Position

The bar supports:

- `top`;
- `bottom`.

There is no autohide mode in V2.

The configured bar edge also determines which edge MONOCLE tabs attach to. With the default `top` placement, the visual order is:

```text
monitor edge
external top strut, if any
native bar
MONOCLE tabs, when visible
client content
```

With `bottom` placement:

```text
client content
MONOCLE tabs, when visible
native bar
external bottom strut, if any
monitor edge
```

The important invariant is that the native bar stays at the configured usable monitor edge while the MONOCLE tab bar stays adjacent to client content.

If the native bar is disabled, the configured bar edge still defines the MONOCLE tab edge. The default remains `top`, preserving V1.5 behavior unless explicitly changed.

### Workarea pipeline

The V1.5 external strut calculation remains the first reservation step.

Conceptually:

```text
monitor geometry
    -> external DOCK/strut reservation
    -> native bar reservation
    -> client workarea
    -> MONOCLE tab reservation when computing MONOCLE content
```

`Monitor.workarea` should represent the client workarea after external struts and native bar reservation. `_NET_WORKAREA` should therefore reflect the native bar reservation as well.

MONOCLE tabs are presentation inside that client workarea and further reduce only MONOCLE client content, as the V1.5 tab bar already does.

FREE geometry, snap, maximize, and non-fullscreen placement use the post-bar `Monitor.workarea`.

### Fullscreen

Real fullscreen covers the native bar, tabs, tray host, and other normal WM UI presentation.

Entering fullscreen must not destroy UI windows or tray state. Leaving fullscreen reveals the existing UI again.

### Stacking

The existing single stacking authority remains in the WM core.

The intended relative ordering is:

```text
desktop windows
ordinary clients
native bar
MONOCLE tab bar
special windows / notifications / external docks
real fullscreen client
```

Tray host/icon windows are visually part of the native bar and must be raised/positioned with the bar without entering ordinary client stacking structures.

The exact X calls may differ, but UI code must not create an independent competing stacking policy.

## Bar regions and layout

A bar consists of three configured widget lists:

- `left`;
- `center`;
- `right`.

Supported widget types are exactly:

- `workspaces`;
- `mode`;
- `title`;
- `status`;
- `clock`;
- `tray`.

A widget type may appear at most once across the complete `left + center + right` configuration. Duplicate widget types are a configuration error.

### Left and right

The left group is anchored to the left bar padding.

The right group is anchored to the right bar padding.

Widgets inside a group are separated by the configured bar `gap`.

### True center

The center group is centered on the physical horizontal center of the monitor bar, not the center of the space remaining between the left and right groups.

For a monitor width of 1920 pixels, the center group is centered at x=960 regardless of unequal left/right group widths.

When available space becomes constrained, the center group must remain centered and be clipped/ellipsized symmetrically rather than pushed away from the monitor center.

### Space pressure

The layout must never intentionally overlap left, center, and right groups.

The V2 compression policy is:

1. workspace, mode, clock, and tray are treated as fixed/natural-width widgets;
2. external `status` text is the first edge widget allowed to shrink toward zero width;
3. center/title content is clipped or ellipsized inside the largest symmetric region that preserves true centering;
4. if an extremely narrow monitor still cannot fit the remaining fixed edge widgets, the edge groups are clipped to their available bar bounds rather than overlapping each other.

No scrolling or multiline bar text is added in V2.

## Style model

The bar uses a deliberately small three-level inheritance model:

```text
bar defaults
    -> widget defaults
        -> widget state overrides
```

Style properties are limited to presentation needs such as:

- `fg`;
- `bg`;
- `font_style = "normal" | "bold"` slot selection;
- label `format` where the widget produces a Box2430 label.

Missing widget values inherit from the bar. Missing state values inherit from the widget.

The model must not grow into general CSS-style selectors, margins, borders, nested layout rules, arbitrary font weights, or theme inheritance.

Bar `padding` controls the outer horizontal bar edge padding. Bar `gap` controls spacing between widgets. Widgets do not gain generic per-widget margin/padding properties in V2; textual spacing can be expressed through the widget/state format string where appropriate.

Tabs use a separate two-level style tree:

```text
tab defaults
    -> tab state overrides
```

Tabs do not inherit bar appearance.

## Widget contracts

### `workspaces`

The workspace widget displays the existing 1-based workspace index only.

V2 does not add workspace names or any workspace display metadata to the core workspace model.

Each workspace resolves to exactly one visual state:

- `empty`;
- `occupied`;
- `active`;
- `urgent`;
- `active_urgent`.

Resolution is semantic rather than precedence-based:

- `active_urgent`: active workspace with at least one urgent client;
- `active`: active workspace without urgency, regardless of whether it currently contains clients;
- `urgent`: inactive workspace with at least one urgent client;
- `occupied`: inactive workspace with at least one client and no urgency;
- `empty`: inactive workspace with no clients and no urgency.

Each state may independently override:

- foreground;
- background;
- `font_style = "normal" | "bold"`;
- label format.

Example output styles may therefore produce:

```text
 1   2  [ 3 ]  ! 4 !
```

or:

```text
1  * 2  <3>  !!4!!
```

without changing workspace semantics.

Left-clicking a workspace item activates that workspace on that monitor through the normal WM workspace activation path.

No configurable workspace-widget click command is added.

### `mode`

The mode widget represents the active workspace mode of that monitor.

The two V2 modes remain:

- FREE;
- MONOCLE.

The user may configure a separate label and style for each mode. For example, FREE may display `F`, `FREE`, or a glyph; MONOCLE may display `M`, `MONO`, or another glyph.

Mode style may independently override foreground, background, `font_style = "normal" | "bold"`, and label format.

The mode widget is display-only in V2; clicking it has no built-in action.

### `title`

The title widget displays the configured client label source for the semantic focus target of that monitor's active workspace.

For the selected monitor this normally corresponds to the actual focused client. For a non-selected monitor, it uses the active workspace's remembered/semantic focus target rather than the global X input focus on another monitor.

This keeps every monitor bar meaningful in the per-monitor workspace model.

The title widget supports:

- `source = "title" | "class" | "instance"`;
- a single-`%s` format;
- normal style inheritance/overrides.

If there is no target client or the selected source is empty, the widget produces an empty label.

The title widget is display-only in V2.

### `status`

`status` is the dwm-style external text input.

Box2430 does not execute or supervise a status command. User code may produce status text using an external script, for example from the existing Box2430 autostart mechanism.

The status source is the root window name:

1. prefer root `_NET_WM_NAME` when valid UTF-8 text is available;
2. otherwise fall back to root `WM_NAME`.

Box2430 reads the current status once at startup and refreshes its cached value on relevant root `PropertyNotify` events.

A traditional usage remains valid:

```sh
xsetroot -name "VOL 32% | VPN"
```

or a user-managed loop that periodically changes the root name.

The `status` widget may apply a single-`%s` display format around the external string, but it does not interpret the status text as markup, colors, commands, or multiple channels.

There is one root status channel for the X screen.

The status widget is display-only in V2.

### `clock`

The clock is built into Box2430.

Its `format` is a `strftime(3)` format, not the Box2430 single-`%s` label format.

The clock is evaluated using local system time.

The event loop must wake at least once per second while the clock widget is configured and visible. The implementation should continue to use the X connection plus `poll()`; no worker thread, signal timer, helper process, or background daemon is needed.

A simple design is to use a poll timeout bounded to approximately one second and redraw clock-containing bars when the displayed clock text may have changed.

The clock is display-only in V2.

### `tray`

The tray widget exposes one traditional XEmbed system tray for the X screen.

Protocol behavior should follow the mature dwm systray patch as closely as practical, including selection ownership, `MANAGER`, docking, XEmbed, icon mapping, resizing, removal, and cleanup.

Because one XEmbed icon window cannot simultaneously be reparented into multiple monitor bars, V2 does not mirror the same tray across monitors.

The tray is displayed only on the currently selected monitor. On all other monitor bars, the tray widget resolves to zero width.

When the selected monitor changes, the tray host is relocated to the new selected monitor's tray allocation.

The tray subsystem starts only when:

- the native bar is enabled; and
- the configured widget lists contain `tray`.

If `_NET_SYSTEM_TRAY_Sn` is already owned by another tray manager, failure to acquire the selection is non-fatal to the WM. Box2430 should report the condition on stderr and render the tray widget as empty/zero-width rather than refusing to start the window manager.

Tray icon events remain the responsibility of the embedded icon/XEmbed protocol. Box2430 does not translate tray clicks into Box2430 commands.

## MONOCLE tab bar

The existing V1.5 tab bar becomes a consumer of the shared UI rendering layer rather than remaining a separate collection of text/Xft helpers inside `wm.c`.

It is not itself a native-bar widget.

The hierarchy is conceptually:

```text
UI
├── Native Bar
│   ├── workspaces
│   ├── mode
│   ├── title
│   ├── status
│   ├── clock
│   └── tray
└── MONOCLE Tab Bar
    └── per-client tab items
```

The bar and tabs share rendering primitives, not widget semantics or configuration.

### Tab label

Tabs support the same strict client-label sources as the title widget:

- `title`;
- `class`;
- `instance`.

Tabs support a single-`%s` label format.

### Tab states

Tabs have exactly three visual states:

- `inactive`;
- `active`;
- `urgent`.

An active client should not normally remain urgent because focusing clears urgency. No separate `active_urgent` tab state is required.

For multi-monitor MONOCLE presentation, `active` is the semantic focus target of that tab bar's workspace, not merely equality with the single global `wm->focused_client`. This allows each MONOCLE monitor to show its own current tab consistently.

### Tab layout and interaction

Preserve the V1.5 tab layout policy unless a change is required for UTF-8-safe rendering:

- use natural tab widths when all tabs fit;
- distribute available width across tabs when natural widths overflow;
- clip/ellipsize text safely inside each allocated tab rectangle.

Existing tab bindings remain under `[bindings.tabbar]` and continue to dispatch through the command system.

The V2 native bar does not add a parallel generic bar-binding table.

## Client border presentation

Client borders are visually part of the native UI, but only their presentation belongs in the UI layer.

The ownership split is:

```text
WM core:
    border width
    per-rule border enable/disable
    geometry impact
    fullscreen width = 0

UI layer:
    focused color resource
    unfocused color resource
    urgent color resource
    refresh of the current border color
```

The existing `[appearance.border]` configuration surface is retained.

Moving color allocation/refresh into `ui.c` must not change V1.5 border semantics.

## Events and redraw triggers

The UI should redraw only in response to relevant semantic or X events, including:

- monitor topology/geometry changes;
- active workspace changes;
- workspace mode changes;
- client manage/unmanage/move events affecting occupancy;
- focus-target changes;
- urgency changes;
- title/class/instance property changes when used by visible labels;
- root status property changes;
- Expose events for UI windows;
- the once-per-second clock wakeup;
- selected-monitor changes affecting tray placement;
- tray/XEmbed lifecycle events.

The UI layer may expose coarse update functions if that keeps call sites simple. It should not attempt to build a reactive dependency graph or generalized invalidation framework.

## Configuration behavior

V2 configuration remains strict and atomic:

- unknown keys are errors;
- unknown widget names are errors;
- duplicate widget types across left/center/right are errors;
- invalid client label sources are errors;
- invalid font-slot names are errors;
- invalid colors are errors;
- invalid label formats are errors;
- malformed widget state tables are errors.

A rejected configuration falls back atomically to built-in defaults, preserving the V1.5 configuration safety model.

The complete V1.5-to-V2 key migration and target TOML shape are defined separately in `V2_CONFIG_MIGRATION.md`.

## Implementation sequence

The product contract is intended to land as one coherent V2 native UI feature, but implementation should still be staged so regressions remain attributable.

Recommended order:

1. **UI extraction without behavior change**
   - introduce `ui.c/.h`;
   - move V1.5 tab text/font/color/drawing helpers;
   - preserve existing tab behavior and tests.

2. **V2 UI configuration model**
   - add strict bar/widget/tab style structures;
   - migrate flat tab state fields to nested V2 state configuration;
   - keep all unrelated V1.5 configuration unchanged.

3. **Native per-monitor bar**
   - create/destroy/reconcile bar windows with monitors;
   - reserve workarea at top/bottom;
   - integrate stacking and fullscreen behavior.

4. **Bar layout and text widgets**
   - left/true-center/right layout;
   - workspace, mode, title;
   - shared formatting, inheritance, clipping, and ellipsis.

5. **External status and internal clock**
   - root name cache and PropertyNotify handling;
   - one-second poll wakeup when needed.

6. **MONOCLE tab V2 presentation**
   - source/format/state style support;
   - active state based on per-workspace semantic focus target;
   - top/bottom adjacency behavior.

7. **Border presentation integration**
   - move border color resources/refresh to UI while preserving width semantics.

8. **XEmbed systray**
   - implement `tray.c/.h` using dwm systray behavior as primary precedent;
   - selected-monitor relocation;
   - graceful selection conflict handling.

9. **Regression and real-session validation**
   - preserve every V1.5 regression;
   - add focused V2 UI and tray scenarios;
   - validate daily-use behavior on real X11.

## Required validation

V2 should not be considered complete until at least the following behavior is covered by automated or focused Xephyr tests where practical:

- strict parsing of all new UI keys;
- rejection of unknown/duplicate widgets;
- top and bottom bar workarea reservation;
- correct `_NET_WORKAREA` after native bar reservation;
- external DOCK struts combined with native bar reservation;
- FREE/maximize/snap geometry using post-bar workarea;
- MONOCLE content and tab placement for top and bottom bars;
- fullscreen covering UI and restoring it afterward;
- true-center geometry with asymmetric left/right groups;
- status startup read and root property updates;
- workspace state resolution including `active_urgent`;
- mode label/style changes on FREE/MONOCLE transitions;
- title/tab source updates for title/class/instance;
- strict source behavior when the selected property is empty;
- per-monitor semantic title/tab target behavior;
- bar and tab recreation/repositioning across monitor topology changes;
- border colors still track focus/urgency without changing border geometry semantics;
- XEmbed tray selection, docking, map/unmap, resize, removal, selected-monitor relocation, and clean WM shutdown;
- non-fatal behavior when another system tray already owns the selection.

Real-session handoff should include normal browser/terminal use, rofi, dunst, fullscreen applications, multiple monitors, and representative tray applications.

## Completion criterion

V2 Native UI is complete when the full contract above is stable enough for daily use and no longer requires temporary compatibility/configuration shapes.

At that point Box2430 may freeze a V2 baseline in the same sense as V1.5: new UI mechanisms stop expanding, while concrete bugs and compatibility regressions remain maintenance work.
