# Box2430 V1.5 to V2 Configuration Migration

## Purpose

This document defines the intended configuration migration from the frozen Box2430 V1.5 baseline to the V2 Native UI configuration.

It is based on the actual V1.5 parser and `config.example.toml`, not on an earlier design document.

V1.5 currently accepts these top-level sections:

```text
[workspaces]
[focus]
[placement]
[fullscreen]
[appearance]
[snap]
[bindings]
[[rules]]
```

V2 keeps that top-level structure. Native UI is added under `[appearance]`; it does not introduce workspace names, a plugin section, an executable-widget section, or a second command system.

Configuration remains strict and atomic. Unknown keys or invalid values reject the configuration and cause Box2430 to use built-in defaults.

## Migration summary

Most V1.5 configuration remains unchanged.

| V1.5 area | V2 action |
| --- | --- |
| `[workspaces]` | unchanged; still only `count` |
| `[focus]` | unchanged |
| `[placement]` | unchanged |
| `[fullscreen]` | unchanged |
| `[appearance].background` | unchanged |
| `[appearance.border]` | unchanged keys and semantics |
| `[appearance.snap_preview]` | unchanged |
| `[snap]` | unchanged |
| `[bindings]` | unchanged |
| `[bindings.keys]` | unchanged |
| `[bindings.mouse]` | unchanged |
| `[bindings.tabbar]` | unchanged |
| `[[rules]]` | unchanged |
| `[appearance.tabs]` | retained but state styling is restructured |
| `[appearance.bar]` | new |
| `[appearance.bar.widgets.*]` | new |

There are no V2 workspace-name keys.

There is no `[bindings.bar]` section in V2.

There is no `command`, `exec`, `interval`, or equivalent arbitrary script-execution key for bar widgets.

## V1.5 baseline shape

The V1.5 example configuration is structurally equivalent to:

```toml
[workspaces]
count = 9

[focus]
mode = "click"
active_window = "urgent"
raise_on_focus = false
focus_on_map = true
raise_on_map = true

[placement]
normal = "center"
dialog = "center"

[fullscreen]
client_policy = "fake"

[appearance]
background = "#000000"

[appearance.border]
width = 2
focused = "#89b4fa"
unfocused = "#45475a"
urgent = "#f38ba8"

[appearance.tabs]
enabled = true
height = 24
padding = 8
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
active_fg = "#ffffff"
active_bg = "#3b4252"
inactive_fg = "#aaaaaa"
inactive_bg = "#222222"
urgent_fg = "#ffffff"
urgent_bg = "#bf616a"
active_bold = true
inactive_bold = false
urgent_bold = true

[appearance.snap_preview]
color = "#89b4fa"
width = 2

[snap]
enabled = true
edge_zone = 16
side_ratio = 0.5
corner_width_ratio = 0.5
corner_height_ratio = 0.5
preview = true

[bindings]
inherit_defaults = true

[bindings.keys]
# key -> command

[bindings.mouse]
# button -> command

[bindings.tabbar]
# button/wheel -> tab-context command

[[rules]]
# existing V1.5 rule fields
```

The only existing area that requires a meaningful schema migration is `[appearance.tabs]`.

## Unchanged V1.5 sections

### Workspaces

V2 deliberately does not introduce workspace names.

```toml
[workspaces]
count = 9
```

Workspace identity and user-facing workspace labels remain the existing 1-based index.

The V2 `workspaces` bar widget formats that index but does not add metadata to `Workspace`.

### Focus

Unchanged:

```toml
[focus]
mode = "click"              # click | sloppy
active_window = "urgent"    # urgent | focus
raise_on_focus = false
focus_on_map = true
raise_on_map = true
```

### Placement

Unchanged:

```toml
[placement]
normal = "center"           # center | client
dialog = "center"           # center | client
```

### Fullscreen

Unchanged:

```toml
[fullscreen]
client_policy = "fake"      # allow | fake | deny
```

### Background

Unchanged:

```toml
[appearance]
background = "#000000"
```

This remains the fallback root background painted on a fresh Box2430 session.

### Client border

The V1.5 keys remain valid without renaming:

```toml
[appearance.border]
width = 2
focused = "#89b4fa"
unfocused = "#45475a"
urgent = "#f38ba8"
```

Internally V2 may move border color resources/refresh into `ui.c`, but this is an implementation refactor, not a user-facing configuration migration.

`width` remains WM geometry policy. Colors remain presentation.

### Snap preview and snap policy

Unchanged:

```toml
[appearance.snap_preview]
color = "#89b4fa"
width = 2

[snap]
enabled = true
edge_zone = 16
side_ratio = 0.5
corner_width_ratio = 0.5
corner_height_ratio = 0.5
preview = true
```

### Bindings

The existing binding model remains unchanged:

```toml
[bindings]
inherit_defaults = true

[bindings.keys]
"Super+m" = "mode monocle toggle"

[bindings.mouse]
"Super+Button1" = "mouse move-window"

[bindings.tabbar]
"Button1" = "tab focus"
"Button2" = "tab close"
"Button3" = "none"
"WheelUp" = "focus prev"
"WheelDown" = "focus next"
```

V2 does not add generic configurable click commands for bar widgets.

Workspace items have their built-in left-click activation behavior. Tray input is handled by XEmbed/tray clients. Mode/title/status/clock are display-only.

### Rules

All V1.5 rule match/action fields remain unchanged:

```toml
[[rules]]
class = "Firefox"
workspace = 2
focus_on_map = true

[[rules]]
class = "mpv"
fullscreen_policy = "allow"
```

Native UI does not add rule fields in V2.

## New `[appearance.bar]`

V2 adds a native bar section.

The intended shape is:

```toml
[appearance.bar]
enabled = true
position = "top"            # top | bottom
height = 24
padding = 8                  # outer horizontal bar padding
gap = 8                      # gap between widgets in the same region
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
fg = "#aaaaaa"
bg = "#222222"
left = ["workspaces", "mode"]
center = ["title"]
right = ["status", "clock", "tray"]
```

Supported widget names are exactly:

```text
workspaces
mode
title
status
clock
tray
```

Each widget type may appear at most once across the concatenation of `left`, `center`, and `right`.

Unknown widget names and duplicate widgets are configuration errors.

`center` is true monitor-center positioning; it is not centered in the leftover space between left/right groups.

The bar configuration applies to all monitors. V2 does not add per-monitor widget lists or themes.

## Bar style inheritance

Bar widget styling uses:

```text
appearance.bar defaults
    -> appearance.bar.widgets.<widget>
        -> state table, for stateful widgets
```

The style keys available to normal text-producing widgets are:

```text
fg       = "#RRGGBB"
bg       = "#RRGGBB"
font_style = "normal" | "bold"
format   = a Box2430 single-%s format, where applicable
```

`appearance.bar.font` and `appearance.bar.font_bold` are Xft font names.

Nested widget/state `font_style = "normal" | "bold"` chooses one of those two configured font slots; it is not an Xft pattern.

A missing widget style value inherits the bar value. A missing state style value inherits the widget value.

This is intentionally the complete inheritance model; V2 does not add CSS-style selectors or arbitrary font weights.

## `workspaces` widget

The widget displays only the existing workspace index.

```toml
[appearance.bar.widgets.workspaces]
format = " %s "
font_style = "normal"

[appearance.bar.widgets.workspaces.empty]
fg = "#666666"
bg = "#222222"
font_style = "normal"
format = " %s "

[appearance.bar.widgets.workspaces.occupied]
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"
format = " %s "

[appearance.bar.widgets.workspaces.active]
fg = "#ffffff"
bg = "#3b4252"
font_style = "bold"
format = "[ %s ]"

[appearance.bar.widgets.workspaces.urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
format = "! %s !"

[appearance.bar.widgets.workspaces.active_urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
format = "[! %s !]"
```

The complete V2 workspace visual states are:

```text
empty
occupied
active
urgent
active_urgent
```

There is no `label = "name"`, `name_or_index`, workspace-name table, or other workspace metadata in V2.

All `%s` substitutions receive the 1-based workspace index rendered as text.

## `mode` widget

The mode widget allows user-defined text and style for each existing workspace mode.

```toml
[appearance.bar.widgets.mode]
format = "%s"
font_style = "normal"

[appearance.bar.widgets.mode.free]
label = "F"
format = "[ %s ]"
font_style = "normal"

[appearance.bar.widgets.mode.monocle]
label = "M"
format = "[ %s ]"
font_style = "bold"
```

The only state names are:

```text
free
monocle
```

Each state may additionally override `fg` and `bg`.

`label` is literal user text. It may be a word, short abbreviation, or glyph supported by the configured/fallback fonts.

## `title` widget

```toml
[appearance.bar.widgets.title]
source = "title"            # title | class | instance
format = "%s"
font_style = "normal"
```

`fg` and `bg` may be specified to override bar defaults.

`source` is strict. If the selected property is empty, the widget produces empty text; it does not fall back to another source.

The widget uses the semantic focus target for that monitor's active workspace, allowing non-selected monitors to retain a meaningful title.

## `status` widget

```toml
[appearance.bar.widgets.status]
format = "%s"
font_style = "normal"
```

`fg` and `bg` may be specified to override bar defaults.

`status` consumes the root window status text. Box2430 prefers root `_NET_WM_NAME` and falls back to `WM_NAME`.

It does not execute a configured command.

A user can keep the dwm-style model:

```sh
xsetroot -name "VOL 32% | VPN"
```

or start a user-owned status loop from Box2430 autostart.

There is one external status string for the X screen, not multiple named status channels.

## `clock` widget

```toml
[appearance.bar.widgets.clock]
format = "%H:%M"
font_style = "normal"
```

`fg` and `bg` may be specified to override bar defaults.

Unlike the other `format` keys, clock `format` is passed through `strftime(3)` semantics. It is not a single-`%s` Box2430 label format.

Box2430 supplies the clock internally and updates it on an approximately one-second event-loop cadence when the clock widget is present.

## `tray` widget

The tray has no external command and no text label.

Its presence in the left/center/right widget arrays enables the XEmbed tray subsystem when the bar itself is enabled.

V2 intentionally keeps the configuration small. The tray uses bar height and native layout geometry rather than adding a configurable icon-size model.

The tray is visible only on the selected monitor; on other monitors the tray widget has zero width.

If another process already owns `_NET_SYSTEM_TRAY_Sn`, Box2430 continues running and the tray widget remains empty.

## V2 `[appearance.tabs]` migration

V2 keeps `[appearance.tabs]`, but replaces the V1.5 flat active/inactive/urgent fields with a common style plus nested state overrides.

### V1.5 fields removed

These V1.5 keys are removed from the accepted V2 schema:

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

A V1.5 file containing these keys is expected to fail V2 strict validation until migrated.

V2 does not keep compatibility aliases for the old flat names.

### V1.5 fields retained

These keys remain:

```text
enabled
height
padding
font
font_bold
```

### V2 fields added

V2 adds common tab label/style keys:

```text
source
format
fg
bg
font_style
```

and state tables:

```text
[appearance.tabs.inactive]
[appearance.tabs.active]
[appearance.tabs.urgent]
```

To avoid ambiguity with the existing top-level Xft `font` key, tab common/state selection uses `font_style = "normal" | "bold"`.

The intended V2 shape is:

```toml
[appearance.tabs]
enabled = true
height = 24
padding = 8
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
source = "title"            # title | class | instance
format = "%s"
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"

[appearance.tabs.inactive]
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"
format = "%s"

[appearance.tabs.active]
fg = "#ffffff"
bg = "#3b4252"
font_style = "bold"
format = "%s"

[appearance.tabs.urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
format = "%s"
```

Tab state values inherit from `[appearance.tabs]` when omitted.

`source` is strict and does not fall back. This intentionally differs from the V1.5 drawing helper, which could display `(untitled)` when a title was missing.

The V1.5 tab binding section remains `[bindings.tabbar]` without changes.

## Direct tab-key conversion

A V1.5 tab configuration such as:

```toml
[appearance.tabs]
enabled = true
height = 24
padding = 8
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
active_fg = "#ffffff"
active_bg = "#3b4252"
inactive_fg = "#aaaaaa"
inactive_bg = "#222222"
urgent_fg = "#ffffff"
urgent_bg = "#bf616a"
active_bold = true
inactive_bold = false
urgent_bold = true
```

migrates to:

```toml
[appearance.tabs]
enabled = true
height = 24
padding = 8
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
source = "title"
format = "%s"
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"

[appearance.tabs.active]
fg = "#ffffff"
bg = "#3b4252"
font_style = "bold"

[appearance.tabs.inactive]
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"

[appearance.tabs.urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
```

The mapping is therefore:

| V1.5 key | V2 key |
| --- | --- |
| `active_fg` | `appearance.tabs.active.fg` |
| `active_bg` | `appearance.tabs.active.bg` |
| `active_bold = true` | `appearance.tabs.active.font_style = "bold"` |
| `active_bold = false` | `appearance.tabs.active.font_style = "normal"` |
| `inactive_fg` | `appearance.tabs.inactive.fg` |
| `inactive_bg` | `appearance.tabs.inactive.bg` |
| `inactive_bold` | `appearance.tabs.inactive.font_style` |
| `urgent_fg` | `appearance.tabs.urgent.fg` |
| `urgent_bg` | `appearance.tabs.urgent.bg` |
| `urgent_bold` | `appearance.tabs.urgent.font_style` |

`source = "title"` and `format = "%s"` preserve the normal V1.5 title presentation for clients that have titles.

## Complete proposed V2 configuration shape

The following example shows the complete intended V2 configuration surface in one file. Values are representative built-in/default-style values; users may omit optional overrides and rely on inheritance/defaults.

```toml
[workspaces]
count = 9

[focus]
mode = "click"
active_window = "urgent"
raise_on_focus = false
focus_on_map = true
raise_on_map = true

[placement]
normal = "center"
dialog = "center"

[fullscreen]
client_policy = "fake"

[appearance]
background = "#000000"

[appearance.border]
width = 2
focused = "#89b4fa"
unfocused = "#45475a"
urgent = "#f38ba8"

[appearance.bar]
enabled = true
position = "top"
height = 24
padding = 8
gap = 8
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
fg = "#aaaaaa"
bg = "#222222"
left = ["workspaces", "mode"]
center = ["title"]
right = ["status", "clock", "tray"]

[appearance.bar.widgets.workspaces]
format = " %s "
font_style = "normal"

[appearance.bar.widgets.workspaces.empty]
fg = "#666666"
bg = "#222222"
font_style = "normal"
format = " %s "

[appearance.bar.widgets.workspaces.occupied]
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"
format = " %s "

[appearance.bar.widgets.workspaces.active]
fg = "#ffffff"
bg = "#3b4252"
font_style = "bold"
format = "[ %s ]"

[appearance.bar.widgets.workspaces.urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
format = "! %s !"

[appearance.bar.widgets.workspaces.active_urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
format = "[! %s !]"

[appearance.bar.widgets.mode]
format = "%s"
font_style = "normal"

[appearance.bar.widgets.mode.free]
label = "F"
format = "[ %s ]"
font_style = "normal"

[appearance.bar.widgets.mode.monocle]
label = "M"
format = "[ %s ]"
font_style = "bold"

[appearance.bar.widgets.title]
source = "title"
format = "%s"
font_style = "normal"

[appearance.bar.widgets.status]
format = "%s"
font_style = "normal"

[appearance.bar.widgets.clock]
format = "%H:%M"
font_style = "normal"

[appearance.tabs]
enabled = true
height = 24
padding = 8
font = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"
source = "title"
format = "%s"
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"

[appearance.tabs.inactive]
fg = "#aaaaaa"
bg = "#222222"
font_style = "normal"
format = "%s"

[appearance.tabs.active]
fg = "#ffffff"
bg = "#3b4252"
font_style = "bold"
format = "%s"

[appearance.tabs.urgent]
fg = "#ffffff"
bg = "#bf616a"
font_style = "bold"
format = "%s"

[appearance.snap_preview]
color = "#89b4fa"
width = 2

[snap]
enabled = true
edge_zone = 16
side_ratio = 0.5
corner_width_ratio = 0.5
corner_height_ratio = 0.5
preview = true

[bindings]
inherit_defaults = true

[bindings.keys]
"Super+m" = "mode monocle toggle"
"Super+j" = "focus next"
"Super+k" = "focus prev"
"Alt+Tab" = "focus next"
"Super+Left" = "snap left"
"Super+Right" = "snap right"
"Super+Up" = "maximize toggle"
"Super+f" = "fullscreen toggle"
"Super+Ctrl+Left" = "monitor prev"
"Super+Ctrl+Right" = "monitor next"

[bindings.mouse]
"Super+Button1" = "mouse move-window"
"Super+Button3" = "mouse resize-window"

[bindings.tabbar]
"Button1" = "tab focus"
"Button2" = "tab close"
"Button3" = "none"
"WheelUp" = "focus prev"
"WheelDown" = "focus next"

[[rules]]
class = "Firefox"
fullscreen_policy = "fake"

[[rules]]
class = "mpv"
fullscreen_policy = "allow"
```

## Keys added in V2

At a high level, V2 adds:

```text
appearance.bar.*
appearance.bar.widgets.workspaces.*
appearance.bar.widgets.mode.*
appearance.bar.widgets.title.*
appearance.bar.widgets.status.*
appearance.bar.widgets.clock.*
appearance.tabs.source
appearance.tabs.format
appearance.tabs.fg
appearance.tabs.bg
appearance.tabs.font_style
appearance.tabs.inactive.*
appearance.tabs.active.*
appearance.tabs.urgent.*
```

The `tray` widget is enabled by listing `"tray"` in a bar region. V2 does not require a large tray configuration table.

## Keys removed in V2

Only the flat V1.5 tab-state presentation keys are intentionally removed:

```text
appearance.tabs.active_fg
appearance.tabs.active_bg
appearance.tabs.inactive_fg
appearance.tabs.inactive_bg
appearance.tabs.urgent_fg
appearance.tabs.urgent_bg
appearance.tabs.active_bold
appearance.tabs.inactive_bold
appearance.tabs.urgent_bold
```

They are replaced by nested state style tables.

No unrelated V1.5 option is removed as part of Native UI work.

## Validation rules for new UI configuration

The V2 parser should reject at least:

- unknown bar positions;
- unknown widget names;
- duplicate widgets across left/center/right;
- unsupported `title`/tab sources;
- unsupported font slot values;
- colors outside the existing accepted `#RRGGBB` form;
- non-clock label formats that do not contain exactly one `%s`;
- non-clock label formats containing more than one `%s`;
- unknown workspace state names;
- unknown mode state names;
- unknown tab state names;
- unknown keys inside widget/state tables.

Clock format follows `strftime` syntax and is not subject to the single-`%s` rule.

The parser should preserve V1.5's atomic-candidate behavior: partially parsed V2 appearance state must never leak into the running/default configuration when validation fails.

## Migration procedure

For an existing V1.5 user configuration:

1. keep `[workspaces]`, `[focus]`, `[placement]`, `[fullscreen]`, `[appearance].background`, border, snap, bindings, and rules unchanged;
2. convert the flat `[appearance.tabs]` active/inactive/urgent color/bold keys to nested state tables;
3. add `source = "title"` and `format = "%s"` to tabs unless different client-label presentation is desired;
4. add `[appearance.bar]` and choose the left/center/right widget arrangement;
5. configure workspace state formatting using the existing index only;
6. configure FREE/MONOCLE mode labels if the built-in defaults are not desired;
7. configure title source/format;
8. include `status` if external root-name status text should be displayed;
9. include `clock` for the built-in clock;
10. include `tray` for the dwm-style XEmbed tray on the selected monitor.

No workspace semantic migration or rule migration is required.

## Compatibility policy

Because V2 is a deliberate major UI/configuration step and V1.5 remains a frozen baseline, the preferred migration model is strict rather than carrying permanent compatibility aliases.

In particular, V2 should not silently accept both the old flat tab keys and the new nested state keys indefinitely. The old keys should be rejected once V2 configuration becomes authoritative, making stale configuration visible immediately.

This keeps the parser simple and preserves Box2430's existing preference for explicit, validated configuration over permissive legacy behavior.
