# Box2430

Box2430 is a small, non-reparenting X11 stacking window manager written in C with Xlib.
It aims to keep the direct, mouse-friendly feel of a traditional stacking WM while adding a compact set of keyboard-driven features.

## Features

* Per-monitor workspaces
* FREE and MONOCLE workspace modes
* MONOCLE tab bar
* Click-to-focus and sloppy-focus modes
* Stable client-order focus cycling
* Edge/corner snapping, maximize, and fullscreen
* Configurable keyboard, mouse, and tab-bar bindings
* Application spawning from bindings
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

Command-line options:

```text
box2430 [-d display] [-c config.toml]
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
* `DEVELOPMENT.md` — build, testing, debugging, and verification
* `AGENTS.md` — repository instructions for coding agents

