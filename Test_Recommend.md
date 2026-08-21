# Box2430 Test Recommendations

This file records confirmed testing behavior and environment limitations. Use it
to avoid repeatedly investigating failures that are not caused by Box2430.

## Exercise the real behavior path

When testing an input-driven bug, trigger it through the same path used by the
user:

- Keybind behavior should be generated as X keyboard input, for example with
  `xdotool`, instead of calling the command handler directly.
- Spawn tests should cover keybind dispatch, `fork`/`exec`, and the resulting
  managed window together.
- Held-modifier MRU tests should send separate modifier and Tab press/release
  events. Race tests may pause the WM, queue the complete sequence in the X
  server, and then resume the WM.

A lower-level unit test can supplement these scenarios, but it does not replace
the real X11 input path.

## Run X11 server tests outside the sandbox

Run Xvfb and Xephyr integration tests outside the filesystem/process sandbox.
Inside the sandbox, X servers may be unable to create or use their UNIX socket.
Known symptoms include:

```text
Xvfb did not start
Cannot establish any listening sockets
Owner of /tmp/.X11-unix should be set to root
```

When these messages occur in the sandbox, do not keep changing display numbers
or debug Box2430 code. Re-run the same test outside the sandbox with an unused
display number.

Compilation, static checks, and pure non-X11 tests can still run inside the
sandbox.

## Valgrind and the Xft/fontconfig baseline

A minimal Xft-only program, without Box2430 code, reproduces the same Valgrind
leak seen while starting Box2430:

```text
320 bytes total
256 bytes direct + 64 bytes indirect
allocation path: libfontconfig -> libexpat
```

This matching leak is external library/process-teardown behavior and is not a
Box2430 leak. Do not add Box2430 cleanup workarounds, replace dependencies, or
change product behavior to suppress it.

This baseline does not make all Valgrind findings ignorable. Investigate any:

- invalid read or write;
- use of uninitialized data;
- leak whose allocation stack includes Box2430 code;
- leak with a different size or allocation path;
- growth that repeats with normal window-manager operations.

When leak results are ambiguous, compare them against the same minimal Xft
baseline in the same environment.

