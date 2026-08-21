# Box2430

Box2430 is a small, non-reparenting X11 stacking window manager written in C with Xlib.
It aims to keep the direct, mouse-friendly feel of a traditional stacking WM while adding a compact set of keyboard-driven features.

## Status

Box2430 V1.5 is the frozen stable baseline. V1.5 is in maintenance mode: bug
fixes and concrete compatibility fixes are in scope, while new features and
architectural expansion belong to later development.

## Features

* Per-monitor workspaces
* FREE and MONOCLE workspace modes
* MONOCLE tab bar
* Click-to-focus and sloppy-focus modes
* Stable client-order focus cycling
* Edge/corner snapping, maximize, and fullscreen
* Configurable keyboard, mouse, and tab-bar bindings
* Application spawning from bindings
* Cold-start root background and optional autostart executable
* Window rules
* Practical ICCCM/EWMH support, including docks and struts

## Build

Required development packages:

* C11 compiler (GCC or Clang)
* GNU Make
* `pkg-config`
* X11
* Xinerama
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

Start Box2430 as the window manager for an X11 session, for example from `.xinitrc`:

```sh
exec box2430
```

To run one executable once when the WM session starts:

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

If no configuration file is present, built-in defaults are used.

`appearance.background` sets the root fallback color on a fresh WM session.
Box2430 does not maintain it afterward, so wallpaper tools such as `feh` can
replace it normally. A WM restart does not repaint the root background.

## Default controls

A few useful built-in bindings:

| Binding                           | Action                     |
| --------------------------------- | -------------------------- |
| `Super+Return`                    | Spawn `kitty`              |
| `Super+q`                         | Close focused window       |
| `Super+1` … `Super+9`             | Switch workspace           |
| `Super+Shift+1` … `Super+Shift+9` | Move window to workspace   |
| `Alt+Tab`                         | Cycle windows in client order |
| `Super+m`                         | Toggle MONOCLE mode        |
| `Super+Button1`                   | Move window                |
| `Super+Button3`                   | Resize window              |

See `config.example.toml` for a configuration example.

## Documentation

* `docs/REFERENCE.md` — commands, configuration, bindings, and rules
* `docs/ARCHITECTURE.md` — internal state model and implementation structure
* `docs/IMPLEMENTATION_STYLE.md` — long-lived engineering principles
* `DEVELOPMENT.md` — build, testing, debugging, and verification
* `AGENTS.md` — repository instructions for coding agents

