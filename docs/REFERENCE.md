# Box2430 Reference

This document describes the user-facing command and configuration surface implemented by the current Box2430 codebase.

## Commands

Commands are used by keyboard, mouse, and tab-bar bindings.

| Command | Meaning |
| --- | --- |
| `wm quit` | Exit Box2430 |
| `wm restart` | Restart Box2430 |
| `spawn <program> [args...]` | Launch a program |
| `workspace <N>` | Switch to workspace `N` on the selected monitor |
| `workspace next\|prev` | Switch workspace relatively |
| `monitor next\|prev` | Select another monitor |
| `window close` | Request the focused window to close |
| `window raise\|lower` | Raise or lower the focused window |
| `window move-workspace <N> [--follow]` | Move the focused window to another workspace |
| `window move-monitor next\|prev [--follow] [--keep-workspace]` | Move the focused window to another monitor |
| `focus next-tab\|prev-tab` | Cycle focus in tab order |
| `focus next-mru\|prev-mru` | Cycle focus in MRU order |
| `mode free` | Set the current workspace to FREE mode |
| `mode monocle` | Set the current workspace to MONOCLE mode |
| `mode monocle toggle` | Toggle FREE/MONOCLE |
| `snap none\|left\|right\|top-left\|top-right\|bottom-left\|bottom-right` | Apply or clear snap state |
| `snap maximize` | Maximize the focused window |
| `maximize [toggle]` | Maximize, or toggle maximization |
| `fullscreen [toggle]` | Enter, or toggle user fullscreen |
| `mouse move-window` | Begin interactive move; mouse-binding context only |
| `mouse resize-window` | Begin interactive resize; mouse-binding context only |
| `tab focus` | Focus the clicked tab; tab-bar context only |
| `tab close` | Close the clicked tab; tab-bar context only |

Workspace numbers are 1-based. `--follow` moves focus with the window. `--keep-workspace` preserves the workspace number when moving between monitors instead of using the target monitor's active workspace.

## Configuration

The configuration file is TOML. Unknown keys, invalid values, invalid rules, or invalid bindings cause the file to be rejected and Box2430 to use built-in defaults.

### Main options

| Section | Options and defaults |
| --- | --- |
| `[workspaces]` | `count = 9` (`1..32`) |
| `[focus]` | `mode = "click"` (`click`/`sloppy`), `raise_on_focus = false`, `focus_on_map = true`, `raise_on_map = true`, `monocle_fallback = "tab"` (`tab`/`mru`) |
| `[placement]` | `normal = "center"`, `dialog = "center"` (`center`/`client`) |
| `[fullscreen]` | `client_policy = "fake"` (`allow`/`fake`/`deny`) |
| `[appearance.border]` | `width = 2`, focused/unfocused/urgent `#RRGGBB` colors |
| `[appearance.tabs]` | enabled, height, padding, normal/bold Xft fonts, active/inactive/urgent colors and bold flags |
| `[appearance.snap_preview]` | `color = "#89b4fa"`, `width = 2` |
| `[snap]` | `enabled = true`, `edge_zone = 16`, side/corner ratios `= 0.5`, `preview = true` |
| `[bindings]` | `inherit_defaults = true` |

See `config.example.toml` for all appearance values and a complete configuration example.

## Bindings

Keyboard bindings use X11 keysym names and the modifiers `Super`, `Shift`, `Ctrl`, and `Alt`:

```toml
[bindings.keys]
"Super+Return" = "spawn kitty"
"Super+Shift+q" = "wm quit"
```

Mouse bindings use `Button1` through `Button5`. Tab-bar bindings additionally accept `WheelUp` and `WheelDown`.

Setting a binding to `"none"` removes that binding. Setting `inherit_defaults = false` clears all built-in bindings before custom bindings are applied. A custom binding for an existing key/button replaces the built-in one.

### Built-in bindings

| Binding | Command |
| --- | --- |
| `Super+Return` | `spawn kitty` |
| `Super+q` | `window close` |
| `Alt+Tab` | `focus next-mru` |
| `Super+m` | `mode monocle toggle` |
| `Super+j` / `Super+k` | `focus next-tab` / `focus prev-tab` |
| `Super+Left` / `Super+Right` | `snap left` / `snap right` |
| `Super+Up` | `maximize toggle` |
| `Super+f` | `fullscreen toggle` |
| `Super+Ctrl+Left` / `Super+Ctrl+Right` | `monitor prev` / `monitor next` |
| `Super+1..9` | `workspace 1..9` |
| `Super+Shift+1..9` | `window move-workspace 1..9` |
| `Super+Button1` / `Super+Button3` | move / resize window |
| Tab `Button1` / `Button2` | focus / close tab |
| Tab `WheelUp` / `WheelDown` | previous / next tab |

## Rules

Rules are declared with `[[rules]]`. Match fields use shell-style `fnmatch` patterns and are case-sensitive.

Match fields:

- `class`
- `instance`
- `title`
- `window_type`: `normal`, `dialog`, `dock`, `desktop`, or `notification`

Action fields:

- `workspace`: 1-based workspace number
- `monitor`: 1-based monitor number
- `focus_on_map`: boolean
- `raise_on_map`: boolean
- `border`: boolean
- `fullscreen_policy`: `allow`, `fake`, or `deny`
- `placement`: `center` or `client`

Example:

```toml
[[rules]]
class = "Firefox"
workspace = 2
focus_on_map = true

[[rules]]
class = "mpv"
fullscreen_policy = "allow"
```

A rule must contain at least one match field and one action field. All matching rules are applied in declaration order, so later matching rules can override fields set by earlier ones.
