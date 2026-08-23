# Box2430

Box2430 is a small, non-reparenting X11 stacking window manager written in C with Xlib.
It keeps a traditional mouse-friendly stacking model while providing per-monitor
workspaces, a MONOCLE mode, a lightweight native UI, snapping, configurable
bindings, and practical ICCCM/EWMH compatibility.

![img](https://github.com/novel2430/Box2430/blob/main/res/01.png?raw=true)
![img](https://github.com/novel2430/Box2430/blob/main/res/02.png?raw=true)

## Current state

The repository is an actively developed working baseline rather than a frozen
release branch. The checked-in implementation and regression tests define the
current behavior.

The main runtime pieces are already integrated: window management, per-monitor
workspaces, FREE/MONOCLE modes, native bars and MONOCLE tabs, XEmbed system tray,
focus/stacking policy, snapping/maximize/fullscreen, rules, startup discovery,
and multi-monitor topology reconciliation.

## Features

* Non-reparenting X11 stacking window management
* Per-monitor workspaces
* FREE and MONOCLE workspace modes
* Configurable FREE/MONOCLE client borders
* Per-monitor native bar with workspace, mode, title, status, clock, and tray widgets
* Configurable top/bottom MONOCLE tab bar
* XEmbed system tray integrated with the selected monitor's bar
* Click-to-focus and sloppy-focus modes
* Stable client-order focus cycling with separate focus history and stack order
* Edge/corner snapping, maximize, and fullscreen
* Configurable keyboard, client-mouse, tab-bar, and workspace-bar bindings
* Direct program spawning and shell-backed commands from bindings
* Window rules for placement, monitor/workspace assignment, borders, focus/raise,
  and client fullscreen policy
* Cold-start root background and optional one-shot autostart executable
* Startup discovery of existing windows
* Practical ICCCM/EWMH support for focus, window state, docks/struts, active window,
  client lists, maximize/fullscreen, and special window types
* RandR 1.5 logical-monitor discovery and topology reconciliation

Box2430 intentionally has no minimize workflow and does not model workspaces as a
single global EWMH desktop set. Workspaces belong to monitors.

## Build

Required development packages:

* C11 compiler (GCC or Clang)
* GNU Make
* `pkg-config`
* X11
* XRandR 1.5 or newer
* Xft

Build a debug binary:

```sh
make
```

Build and install a release binary:

```sh
make release
sudo make install
```

The installed binary is `box2430`.

## Run

Start Box2430 as the window manager for an X11 session, for example from
`.xinitrc`:

```sh
exec box2430
```

To run one executable once after the WM initializes and scans existing windows:

```sh
exec box2430 --autostart ~/.config/box2430/autostart.sh
```

The autostart file must be executable and should provide its own shebang. It is
not rerun by `wm restart`.

Command-line options:

```text
box2430 [-d display] [-c config.toml] [-a path|--autostart path]
```

Without `-c`, Box2430 looks for:

```text
$XDG_CONFIG_HOME/box2430/config.toml
```

or, if `XDG_CONFIG_HOME` is unset:

```text
~/.config/box2430/config.toml
```

If no configuration file is present, built-in defaults are used. Invalid
configuration is rejected as a whole and Box2430 falls back to those defaults.

`appearance.background` sets the root fallback color only for a fresh WM
session. Box2430 does not continuously own the background, so wallpaper tools
such as `feh` can replace it normally. `wm restart` does not repaint it.

## Default controls

A few useful built-in bindings:

| Binding | Action |
| --- | --- |
| `Super+Return` | Spawn `kitty` |
| `Super+q` | Close focused window |
| `Super+1` … `Super+9` | Switch workspace |
| `Super+Shift+1` … `Super+Shift+9` | Move focused window to workspace |
| `Alt+Tab` | Cycle windows in stable client order |
| `Super+j` / `Super+k` | Focus next / previous client |
| `Super+m` | Toggle MONOCLE mode |
| `Super+Left` / `Super+Right` | Snap left / right |
| `Super+Up` | Toggle maximize |
| `Super+f` | Toggle user fullscreen |
| `Super+Ctrl+Left` / `Super+Ctrl+Right` | Select previous / next monitor |
| `Super+Button1` | Move window |
| `Super+Button3` | Resize window |
| Root background `Button1` | Select monitor under pointer |
| MONOCLE tab `Button1` / `Button2` | Focus / close tab |
| Workspace label `Button1` | Activate that workspace |

See `config.example.toml` for a complete configuration example and
`docs/REFERENCE.md` for the command/configuration reference.

## Documentation


* [`REFERENCE.md`](https://github.com/novel2430/Box2430/blob/main/docs/REFERENCE.md) — commands, configuration, bindings, widgets, and rules
* [`ARCHITECTURE.md`](https://github.com/novel2430/Box2430/blob/main/docs/ARCHITECTURE.md) — runtime state model, X11 behavior, and architectural invariants
* [`IMPLEMENTATION_STYLE.md`](https://github.com/novel2430/Box2430/blob/main/docs/IMPLEMENTATION_STYLE.md) — long-lived engineering principles
* [`DEVELOPMENT.md`](https://github.com/novel2430/Box2430/blob/main/DEVELOPMENT.md) — build, testing, debugging, and verification
* [`AGENTS.md`](https://github.com/novel2430/Box2430/blob/main/AGENTS.md) — repository instructions for coding agents
