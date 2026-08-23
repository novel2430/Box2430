# Box2430 Implementation Style

This document records engineering principles that should remain stable across normal Box2430 feature and maintenance work. It describes how the project prefers to be implemented, not what features a particular release must contain.

## Keep the implementation direct

Box2430 intentionally favors a small, readable C implementation over a framework-heavy design.

Prefer:

- explicit C structures and state transitions;
- small helpers with clear ownership;
- one main X11 event loop;
- thin Xlib boundaries;
- local changes that follow existing code paths;
- independent data structures only when they represent genuinely different semantics.

Avoid introducing generic object systems, callback frameworks, abstraction layers, or dependencies unless they solve a concrete problem that the existing structure cannot solve cleanly.

## Converge inputs on semantic transitions

Different X11/UI inputs that express the same user intent should converge on the
same state transition before authoritative WM state is changed.

For example, monitor selection may originate from a key command or from pointer
hit testing, while workspace activation may originate from a keyboard command or
a workspace label. Do not give each input path its own copy of focus repair,
selected-monitor bookkeeping, or workspace-state cleanup. Put those semantics in
the shared monitor/workspace/client transition and keep only genuinely
source-specific effects, such as pointer warping, near the input/projection path.

Use the Transition / Authority / Projection distinction described in
`docs/ARCHITECTURE.md` as a responsibility guide, not as a reason to introduce a
transaction framework. The desired shape is a small number of explicit semantic
helpers with clear model ownership, followed by correctly ordered X11/UI
projection.

## Follow established X11 practice

X11 clients can disappear between requests, so ordinary lifetime races such as `BadWindow` are expected in a window manager.

Judge X11 error handling against established mature WM practice before adding defensive machinery. Broadly ignoring expected `BadWindow` races can be appropriate; internal bookkeeping bugs should instead be caught through clear invariants, reproducible tests, and focused debugging.

Do not turn every asynchronous X11 operation into a synchronous or heavily scoped error-handling protocol merely for theoretical completeness.

## Preserve semantic distinctions

Do not merge concepts simply because they currently contain similar data or often change together.

In particular, preserve the distinction between:

- semantic state and temporary X presentation;
- focus and stacking;
- monitor geometry and workarea;
- workspace membership, stable client/tab order, and stack order;
- WM-generated unmaps and client withdrawal.

Couple these concepts only when the intended user behavior requires it.

## Prefer observable behavior over internal cleverness

For bug fixes and features, prefer the smallest implementation that produces the intended externally observable behavior and fits the existing architecture.

A useful change usually has:

1. a clear user-visible or protocol-visible behavior;
2. a small state transition or code-path change;
3. a regression test that exercises the real path when practical.

Do not redesign unrelated code while fixing a local issue.

## Test through real X11 paths

When behavior depends on X events, bindings, process inheritance, focus, stacking, mapping, geometry, or ICCCM/EWMH interaction, tests should exercise the actual X11 path whenever practical rather than calling one internal helper in isolation.

Use invariants and lower-level tests where they are useful, but treat end-to-end X11 behavior as the final authority for WM behavior.

Testing and debugging procedures belong in `DEVELOPMENT.md`; this document only records the engineering preference.

## Keep documentation subordinate to the implementation

The current implementation and regression tests define what Box2430 actually does. Documentation should explain that behavior accurately rather than acting as an independent frozen specification.

When code, tests, and documentation disagree, first determine whether the task is changing behavior or correcting stale documentation. Do not modify working current behavior solely to satisfy historical design text.
