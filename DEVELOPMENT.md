# Box2430 Development Environment

## Required for implementation

- C11 compiler: GCC or Clang
- GNU Make
- pkg-config
- X11 development libraries:
  - x11
  - xinerama
  - xft

Verify with:

```sh
pkg-config --exists x11 xinerama xft
```

## X11 test environment

Preferred tools:

* Xvfb — headless X11 integration tests
* Xephyr — nested visual/interaction tests
* xdpyinfo
* xprop
* xwininfo
* xterm or xmessage

Useful optional tools:

* xdotool
* xwd
* ImageMagick
* gdb
* strace

Missing optional tools are not V1 blockers.

## Sanitizers

V1 requires usable AddressSanitizer + UndefinedBehaviorSanitizer verification.

The implementation must not assume a specific compiler provides them.

If the default compiler's sanitizer runtime is unavailable, use another installed
compiler such as Clang.

LeakSanitizer is not a V1 requirement.

## Environment preflight

On a new machine, run a preflight before implementation or substantial testing.

Verify actual usability, not only executable presence:

* required pkg-config packages
* Xvfb startup + xdpyinfo connection
* Xephyr startup when host-display access is available
* ASan + UBSan compile/run

Environment limitations affect verification status, not V1 semantics.

If Xephyr cannot access the host display, continue with Xvfb and report visual
verification as UNVERIFIED where appropriate.

Never run startx/xinit or replace the user's active window manager as part of
automated development.
