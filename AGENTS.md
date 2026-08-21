# Box2430 Agent Instructions

This file defines how coding agents should work inside the Box2430 repository.
Keep it operational and compact. Product behavior and implementation details belong in the documentation referenced below.

## Read order

Before substantial work, read only what is relevant to the task:

1. `README.md` — project overview and user-facing entry point
2. `docs/REFERENCE.md` — commands, configuration, bindings, and rules
3. `docs/ARCHITECTURE.md` — runtime model, state relationships, and architectural invariants
4. `docs/IMPLEMENTATION_STYLE.md` — long-lived engineering principles
5. `DEVELOPMENT.md` — build, test, debugging, and verification procedures

For a small local task, do not reread every document unnecessarily. Read the affected code and the relevant documentation first.

## Source of truth

The current implementation and regression tests define what Box2430 actually does.

Documentation explains that behavior and should remain consistent with it, but historical design notes are not frozen contracts.

When code, tests, and documentation disagree:

1. inspect the current implementation and relevant tests;
2. determine whether the task intends to change behavior or fix stale documentation;
3. do not silently invent a third behavior;
4. do not change working current behavior solely to satisfy obsolete text.

An explicit user/task requirement overrides the current behavior when the task is intentionally changing the product.

## V1.5 maintenance baseline

V1.5 is feature-frozen and serves as the stable baseline for later development.
Maintenance work should preserve its public behavior and architectural invariants
unless a concrete bug or compatibility problem requires a change.

For V1.5 maintenance:

* prefer bug and compatibility fixes backed by a concrete reproduction;
* keep fixes narrow and add a regression test when the failure is practical to automate;
* do not add minimize semantics, a global EWMH desktop model, RandR output identity,
  or other compatibility machinery merely for theoretical completeness;
* keep new product features and architectural expansion outside the V1.5 baseline.

## Scope discipline

Prefer the smallest complete change that solves the requested problem.

Do not:

* perform unrelated rewrites;
* add speculative features while fixing another issue;
* introduce framework-scale abstractions for a local problem;
* add dependencies without a concrete need;
* rename or alter public commands/configuration unless the task requires it;
* change architectural semantics as an incidental refactor.

If a local fix exposes a larger architectural problem, surface it separately rather than expanding the task automatically.

## Implementation expectations

Follow `docs/IMPLEMENTATION_STYLE.md`.

In particular:

* keep the implementation direct and readable;
* prefer explicit C state transitions over generic frameworks;
* preserve semantic distinctions such as focus vs. stacking, geometry vs. workarea, and client/tab order vs. stack order;
* keep Xlib boundaries thin;
* follow established mature X11 WM practice before adding defensive machinery;
* treat ordinary client-lifetime `BadWindow` races as expected X11 behavior when appropriate;
* use invariants and regression tests to catch Box2430 bookkeeping bugs.

Do not redesign code merely to make it more theoretically defensive.

## Work loop

For code changes, use a short vertical loop:

```text
inspect the relevant path
    -> make the smallest coherent change
    -> build
    -> run the cheapest meaningful verification
    -> exercise real X11 behavior when relevant
    -> fix regressions
```

Keep the repository buildable and testable as often as practical.

Do not stack unrelated new work on top of a known regression.

## Testing

Follow `DEVELOPMENT.md` for commands and environment details.

Use the cheapest test that genuinely verifies the behavior:

```text
pure/local logic         -> focused test or build check
headless X11 behavior    -> Xvfb
visual/topology behavior -> Xephyr
real-session behavior    -> user-controlled X session
```

When a bug depends on X events, bindings, focus, stacking, mapping, process inheritance, geometry, or ICCCM/EWMH interaction, prefer a regression test that exercises the real path instead of only calling an internal helper.

A bug fix should normally leave a regression test when the failure is reproducible and practical to automate.

Never report a test as passing unless it was actually executed successfully. Distinguish environment limitations from product failures.

## Real session safety

Do not take over the user's active graphical session unless explicitly authorized for that exact operation.

Do not automatically:

* run `startx` or `xinit` against the user's session;
* replace the active window manager;
* switch virtual terminals;
* terminate or replace the desktop session;
* claim ownership of the user's active X display.

Prepare real-session smoke-test instructions when needed and leave execution to the user.

## Documentation changes

Keep documentation synchronized with intentional behavior changes.

Update the relevant document when changing:

* user-facing capabilities or startup basics -> `README.md`;
* commands, configuration, bindings, or rules -> `docs/REFERENCE.md`;
* runtime model, state relationships, or architectural invariants -> `docs/ARCHITECTURE.md`;
* stable engineering principles -> `docs/IMPLEMENTATION_STYLE.md`;
* build, test, debugging, or verification workflow -> `DEVELOPMENT.md`.

Do not duplicate detailed documentation inside this file. `AGENTS.md` should remain a short operational guide for future agents.

## Task completion

Before finishing a code task:

1. review the diff for unrelated changes;
2. build the affected profile when possible;
3. run the relevant regression/integration tests;
4. update affected documentation if public or architectural behavior changed;
5. report what was actually verified and what remains unverified.

For documentation-only tasks, do not modify code or tests unless explicitly requested.
