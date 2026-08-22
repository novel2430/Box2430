# Box2430 Reference

This document describes the current user-facing command, configuration, binding,
widget, and rule surface. `config.example.toml` is the canonical complete example.

## Invocation

```text
box2430 [-d display] [-c config.toml] [-a path|--autostart path]
```

Options:

* `-d display` selects the X display.
* `-c config.toml` selects an explicit TOML configuration file.
* `-a path` / `--autostart path` selects one executable to launch after WM
  initialization and startup window discovery.

Without `-c`, Box2430 looks for:

```text
$XDG_CONFIG_HOME/box2430/config.toml
```

or, when `XDG_CONFIG_HOME` is unset:

```text
~/.config/box2430/config.toml
```

If the default configuration file does not exist, built-in defaults are used.
If a configuration file is present but invalid, the file is rejected as a whole
and Box2430 falls back to built-in defaults rather than partially applying it.

## Commands

Commands are parsed from binding strings. Different binding tables provide
different command contexts, so a command that depends on a clicked client/tab or
workspace label is only valid in the corresponding context.

### General and keyboard commands

| Command | Meaning |
| --- | --- |
| `wm quit` | Exit Box2430 |
| `wm restart` | Re-exec Box2430 while preserving restart-session semantics |
| `spawn <program> [args...]` | Launch argv directly with `execvp`, without shell interpretation |
| `spawn-shell <command>` | Run one command string through `/bin/sh -c` |
| `workspace <N>` | Activate workspace `N` on the selected monitor |
| `workspace next` / `workspace prev` | Activate the next/previous workspace on the selected monitor |
| `monitor next` / `monitor prev` | Select another monitor |
| `window close` | Request the focused client to close |
| `window raise` / `window lower` | Raise/lower the focused client in workspace stack order |
| `window move-workspace <N> [--follow]` | Move the focused client to workspace `N` on the selected monitor |
| `window move-monitor next\|prev [--follow] [--keep-workspace]` | Move the focused client to another monitor |
| `focus next` / `focus prev` | Cycle focus in stable workspace client/tab order |
| `mode free` | Set the selected monitor's active workspace to FREE |
| `mode monocle` | Set it to MONOCLE |
| `mode monocle toggle` | Toggle FREE/MONOCLE |
| `snap none` | Clear snap/maximize presentation and restore normal geometry |
| `snap left` / `snap right` | Snap the focused client to a side |
| `snap top-left` / `snap top-right` | Snap to a top corner |
| `snap bottom-left` / `snap bottom-right` | Snap to a bottom corner |
| `snap maximize` | Maximize the focused client |
| `maximize` | Maximize the focused client |
| `maximize toggle` | Toggle maximize |
| `fullscreen` | Enter user-requested real fullscreen |
| `fullscreen toggle` | Toggle user-requested real fullscreen |
| `tab focus <N>` | In keyboard context, focus MONOCLE tab number `N` if it exists |

Workspace numbers are 1-based. The configured workspace count is `1..32`.

`window move-workspace <N>` targets workspace `N` on the currently selected
monitor. `--follow` activates the destination and follows the client.

`window move-monitor next|prev` normally moves the client to the target monitor's
currently active workspace. `--keep-workspace` instead keeps the client's current
workspace index on the target monitor. `--follow` follows focus to the moved
client. The two flags may be combined in either order.

`focus next` / `focus prev` use the stable workspace client/tab order. Raising or
focusing a client does not reorder that cycle.

`spawn` receives tokenized argv and performs no shell expansion. Use
`spawn-shell` when a binding needs pipes, redirection, `$()`/variables, `&&`, or
other shell syntax. Because the command parser expects `spawn-shell` to receive
one argument, quote the shell command in the binding value, for example:

```toml
[bindings.keys]
"Super+p" = "spawn-shell 'playerctl pause && notify-send Paused'"
```

### Client-mouse commands

These commands are valid only in `[bindings.mouse]`, where the clicked managed
client is supplied by the mouse context:

| Command | Meaning |
| --- | --- |
| `mouse move-window` | Begin interactive move |
| `mouse resize-window` | Begin interactive resize |

### MONOCLE tab-bar commands

These commands are valid only in `[bindings.tabbar]` and operate on the clicked
tab:

| Command | Meaning |
| --- | --- |
| `tab focus` | Focus the clicked tab/client |
| `tab close` | Close the clicked tab/client |
| `focus next` / `focus prev` | Cycle the active workspace in stable tab order |

### Workspace-bar commands

These commands are valid in `[bindings.workspacebar]` when a pointer event hits
a workspace label. The clicked monitor and workspace are supplied by the UI
context.

| Command | Meaning |
| --- | --- |
| `workspace activate` | Activate the clicked workspace |
| `workspace move-window` | Move the focused client to the clicked workspace |
| `workspace move-window --follow` | Move the focused client and follow it |
| `workspace <N>` | Activate workspace `N` on the clicked monitor |
| `workspace next` / `workspace prev` | Switch relatively on the clicked monitor |

`workspace move-window` can cross monitors when the workspace label belongs to a
different monitor's bar. Client geometry is translated/clamped for that move.

## Session startup and restart

Box2430 can launch one executable after WM initialization and startup discovery:

```sh
box2430 --autostart ~/.config/box2430/autostart.sh
box2430 -a ~/.config/box2430/autostart.sh
```

The path is executed directly rather than through a shell or `PATH` lookup. The
file must therefore be executable and, for a script, provide an appropriate
shebang. Autostart failure is non-fatal and is reported on stderr.

Autostart runs only on a fresh Box2430 session. `wm restart` re-executes the WM
without running autostart again.

`appearance.background` follows the same fresh-session rule: it paints a
fallback root background during initial startup only. Box2430 does not maintain
that background afterward, and a restart does not repaint it.

## Configuration

The configuration file is TOML. Unknown keys, invalid value types/ranges,
unknown widgets, duplicate widgets, invalid rules, invalid commands, or commands
used in the wrong binding context invalidate the entire file.

Colors use exactly `#RRGGBB`.

### Core options

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
```

Options:

| Option | Values / default |
| --- | --- |
| `workspaces.count` | `1..32`, default `9` |
| `focus.mode` | `"click"` or `"sloppy"`, default `"click"` |
| `focus.active_window` | `"urgent"` or `"focus"`, default `"urgent"` |
| `focus.raise_on_focus` | boolean, default `false` |
| `focus.focus_on_map` | boolean, default `true` |
| `focus.raise_on_map` | boolean, default `true` |
| `placement.normal` | `"center"` or `"client"`, default `"center"` |
| `placement.dialog` | `"center"` or `"client"`, default `"center"` |
| `fullscreen.client_policy` | `"allow"`, `"fake"`, or `"deny"`, default `"fake"` |
| `appearance.background` | `#RRGGBB`, default `#000000` |

`focus.active_window` controls client `_NET_ACTIVE_WINDOW` requests. `urgent`
keeps focus under WM/user control and marks the requesting client urgent;
`focus` permits the request to focus the client.

Client fullscreen policy applies to client-initiated EWMH fullscreen requests:

* `allow` — the request may become real fullscreen;
* `fake` — keep the client's fullscreen request/state visible to the client but
  do not give it real screen-covering fullscreen;
* `deny` — reject client fullscreen state.

The `fullscreen` command is user-driven and is separate from the client policy.

### Borders

FREE and MONOCLE presentation have independent border styles:

```toml
[appearance.border.free]
width = 2
focused = "#89b4fa"
unfocused = "#45475a"
urgent = "#f38ba8"

[appearance.border.monocle]
width = 0
focused = "#89b4fa"
unfocused = "#45475a"
urgent = "#f38ba8"
```

`width` may be `0..64`. A rule with `border = false` suppresses both mode-specific
WM borders for that client. Real fullscreen also presents with zero border.

### Native bar

The native bar is configured under `[appearance.bar]`:

```toml
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
```

* `position` is `"top"` or `"bottom"`.
* `height` is `12..128`.
* `padding` and `gap` are `0..128`.
* `font` and `font_bold` are Xft font patterns.
* `left`, `center`, and `right` are arrays containing any of:
  `workspaces`, `mode`, `title`, `status`, `clock`, `tray`.
* Each widget may appear at most once across all three groups.
* A group may contain at most six widgets.

The bar is created per monitor and reserves space from that monitor's workarea.
External Dock struts are applied first; the native bar then reserves its own
edge from the remaining area.

#### Common widget style model

Bar `fg`/`bg` and fonts form the base style. Most widget tables can override:

```text
fg = "#RRGGBB"
bg = "#RRGGBB"
font_style = "normal" | "bold"
format = "...%s..."
```

A normal label `format` must contain exactly one `%s`. It is stored as a prefix
and suffix around the label; it is not a general `printf` format string.

The clock is the exception: its `format` is passed through `strftime()` and does
not use the `%s` label-format rule.

#### Workspace widget

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
# same keys

[appearance.bar.widgets.workspaces.active]
# same keys

[appearance.bar.widgets.workspaces.urgent]
# same keys

[appearance.bar.widgets.workspaces.active_urgent]
# same keys
```

Workspace labels are their 1-based indices. Visual state is resolved as one of:
`empty`, `occupied`, `active`, `urgent`, or `active_urgent`.

A workspace becomes urgent when it contains an urgent client. The active
workspace has its own active/active-urgent presentation.

#### Mode widget

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

Each mode state supports `label`, `fg`, `bg`, `font_style`, and `format`.

#### Title widget

```toml
[appearance.bar.widgets.title]
source = "title"
format = "%s"
font_style = "normal"
```

`source` may be `title`, `class`, or `instance` and reads the corresponding
cached metadata from the focused client.

#### Status widget

```toml
[appearance.bar.widgets.status]
format = "%s"
font_style = "normal"
```

Status text comes from the root window:

1. UTF-8 `_NET_WM_NAME` when valid;
2. legacy `WM_NAME` as fallback;
3. an empty string when neither yields usable text.

This makes ordinary root-name producers such as `xsetroot -name ...` usable.
Box2430 does not launch or schedule a status command itself.

#### Clock widget

```toml
[appearance.bar.widgets.clock]
format = "%H:%M"
font_style = "normal"
```

`format` is a non-empty `strftime()` format string. When a visible bar contains
the clock widget, the event loop wakes periodically (about once per second) to
refresh it.

#### Tray widget

```toml
[appearance.bar.widgets.tray]
bg = "#222222"
```

The tray widget only supports an independent background override. It does not
render a text label.

The XEmbed tray manager is created only when the native bar is enabled and the
`tray` widget appears in one of the bar groups. X11 provides one system-tray
selection per screen, so Box2430 exposes that tray only in the **selected
monitor's** bar. Other monitors' tray widget allocation is empty.

If `_NET_SYSTEM_TRAY_S<screen>` is already owned by another tray manager,
Box2430 continues running and disables its tray. If Box2430 later loses the
selection, the tray is disabled rather than terminating the WM.

### MONOCLE tabs

```toml
[appearance.tabs]
enabled = true
position = "top"
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
```

* `position` is independently `"top"` or `"bottom"`; it does not have to match
  the native bar edge.
* `height` is `12..128`; `padding` is `0..128`.
* `source` is `title`, `class`, or `instance`.
* Base and state `format` values contain exactly one `%s`.
* State styles inherit unspecified values from the base tab style.

The tab bar materializes only for a visible MONOCLE workspace containing at
least one client. Its height is clamped if necessary so at least a minimal
client content area remains.

### Snap and preview

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

* preview `width` is `1..32`;
* `edge_zone` is `1..256`;
* each ratio must be strictly between `0` and `1`.

Snap/maximize targets use the monitor workarea rather than raw monitor geometry.
Real fullscreen uses raw monitor geometry.

## Bindings

```toml
[bindings]
inherit_defaults = true
```

With `inherit_defaults = true`, custom bindings replace matching built-ins and
`"none"` removes a matching built-in. With `false`, all built-in key, client
mouse, tab-bar, and workspace-bar bindings are cleared before custom bindings
are parsed.

### Keyboard bindings

Keyboard specs use X11 keysym names plus zero or more modifiers:

```toml
[bindings.keys]
"Super+Return" = "spawn kitty"
"Super+p" = "spawn-shell 'playerctl pause && notify-send Paused'"
"Super+Shift+q" = "wm quit"
```

Recognized modifier names are:

```text
Super  Shift  Ctrl  Alt
```

The final component is passed to `XStringToKeysym`, so ordinary X11 keysym names
are accepted.

### Client mouse bindings

```toml
[bindings.mouse]
"Super+Button1" = "mouse move-window"
"Super+Button3" = "mouse resize-window"
```

Buttons are `Button1` through `Button5`. The same modifier names as keyboard
bindings are accepted.

### Tab-bar and workspace-bar bindings

These UI tables take unmodified buttons only:

```toml
[bindings.tabbar]
"Button1" = "tab focus"
"Button2" = "tab close"
"Button3" = "none"
"WheelUp" = "focus prev"
"WheelDown" = "focus next"

[bindings.workspacebar]
"Button1" = "workspace activate"
"Button2" = "none"
"Button3" = "none"
"WheelUp" = "none"
"WheelDown" = "none"
```

Accepted names are `Button1` through `Button5`, plus the aliases `WheelUp`
(`Button4`) and `WheelDown` (`Button5`). Modifiers are not accepted in these UI
binding tables.

### Built-in bindings

| Binding | Command |
| --- | --- |
| `Super+Return` | `spawn kitty` |
| `Super+q` | `window close` |
| `Alt+Tab` | `focus next` |
| `Super+m` | `mode monocle toggle` |
| `Super+j` / `Super+k` | `focus next` / `focus prev` |
| `Super+Left` / `Super+Right` | `snap left` / `snap right` |
| `Super+Up` | `maximize toggle` |
| `Super+f` | `fullscreen toggle` |
| `Super+Ctrl+Left` / `Super+Ctrl+Right` | `monitor prev` / `monitor next` |
| `Super+1..9` | `workspace 1..9` |
| `Super+Shift+1..9` | `window move-workspace 1..9` |
| `Super+Button1` / `Super+Button3` | move / resize client |
| Tab `Button1` / `Button2` | `tab focus` / `tab close` |
| Tab `WheelUp` / `WheelDown` | `focus prev` / `focus next` |
| Workspace `Button1` | `workspace activate` |

When `workspaces.count` is lower than a built-in numbered workspace binding, the
invalid default binding is pruned during configuration validation.

## Rules

Rules are declared with `[[rules]]`. String match fields use case-sensitive
shell-style `fnmatch` patterns.

Match fields:

* `class`
* `instance`
* `title`
* `window_type`: parser values are `normal`, `dialog`, `dock`, `desktop`, or `notification`

Rules are evaluated on the ordinary `Client` management path. Dock, Desktop,
and Notification special windows are handled before that path, so rule actions
do not currently apply to those special windows even though their type names are
accepted by the configuration parser.

Action fields:

* `workspace`: 1-based workspace number
* `monitor`: 1-based monitor number (`1..32`); if that monitor does not exist at manage time, the current default monitor is kept
* `focus_on_map`: boolean
* `raise_on_map`: boolean
* `border`: boolean
* `fullscreen_policy`: `allow`, `fake`, or `deny`
* `placement`: `center` or `client`

Example:

```toml
[[rules]]
class = "Firefox"
workspace = 2
focus_on_map = true

[[rules]]
class = "mpv"
fullscreen_policy = "allow"

[[rules]]
window_type = "dialog"
placement = "client"
```

A rule must contain at least one match field and one action field. Up to 64
rules are accepted.

All matching rules are applied in declaration order, so later matching rules can
override action fields established by earlier matching rules.

Transient windows inherit monitor/workspace placement from a managed transient
parent before ordinary rule/default policy is applied where appropriate. See
`docs/ARCHITECTURE.md` for the management pipeline and geometry semantics.
