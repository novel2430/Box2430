# Box2430 V1.7 TAP Refactor — Goal

## Mission

Complete the **behavior-preserving TAP refactor** of Box2430 V1.7 from Goal 0
through Goal 5.

The highest-priority rule is:

> **Existing V1.7 behavior is the specification.**

This task must make semantic transitions, authoritative state, and
X11/EWMH/native-UI projections easier to reason about **without intentionally
changing how the WM behaves**.

This is a refactor, not a redesign.

---

## Required reading

Read these files before implementation:

1. `AGENTS.md`
2. `DEVELOPMENT.md`
3. `docs/ARCHITECTURE.md`
4. `docs/IMPLEMENTATION_STYLE.md`
5. `docs/V1_7_TAP_REFACTOR.md`
6. `docs/V1_7_TAP_IMPLEMENTATION_GUIDE.md`

Use:

- `docs/V1_7_TAP_REFACTOR.md` as the architectural/behavioral contract;
- `docs/V1_7_TAP_IMPLEMENTATION_GUIDE.md` as the detailed Goal 0–5 execution
  plan.

Do not substitute architectural preference for current Box2430 behavior.

Before restructuring a complex path, inspect its callers, current tests, and
existing side-effect ordering.

---

## Reference implementations

`ref/` is read-only reference material. **Never modify it.**

Use references with these roles:

- `ref/bspwm` — primary reference for monitor/workspace/client activation,
  remembered focus, interaction state, history/stack separation, and
  IPC-to-semantic-operation structure.
- `ref/i3` — primary architectural reference for authoritative model versus X
  realization/projection.
- `ref/openbox` — selective reference for client-request policy, focus
  compatibility, and lifecycle edge cases.
- `ref/dwm` — simplicity constraint and compact X11 lifecycle/focus/stacking
  reference.

Do not import their data models wholesale. Box2430 behavior and style take
precedence.

---

## TAP

Reason about changed paths as:

```text
input / observation
        |
        v
interpretation + existing policy
        |
        v
Transition: T(A)
        |
        v
Authority: A'
        |
        v
Projection: P(A')
        |
        +----> X11
        +----> ICCCM / EWMH
        +----> native UI
```

For every refactored path, be able to answer:

1. **Transition** — what semantic operation is occurring?
2. **Authority** — what Box2430 state does it own, and what invariants must hold
   when it completes?
3. **Projection** — what X11/EWMH/UI effects realize that state, and does their
   ordering matter?

TAP is a responsibility model, **not** a requirement to build a generic
transaction system or centralized renderer.

Existing X11 protocol/paint ordering must be preserved.

---

## Hard constraints

Do not introduce intentional behavior changes.

Do not add:

- IPC;
- multi-seat support;
- a generic event bus/router;
- a generic transaction framework;
- a new object system;
- an i3-style universal tree or full renderer;
- unrelated features.

Do not redesign working geometry, topology, stacking, native UI, tray,
fullscreen, snap, maximize, workspace, monitor, focus, startup, restart, or
special-window behavior merely for TAP symmetry.

Do not split `handle_event()` just to make it smaller.

Do not create `Seat`, `Transition`, or other runtime objects merely because the
TAP vocabulary contains those concepts.

Prefer direct C helpers, predicates, explicit state ownership, and traceable
control flow.

When architecture and established behavior appear to conflict:

> **Characterize and preserve the behavior first.**

A behavior that looks unusual is not permission to fix it during this task.

---

## Execution order

Complete these goals in order, using
`docs/V1_7_TAP_IMPLEMENTATION_GUIDE.md` for their full scope and exit criteria:

```text
Goal 0 — Characterize and freeze current behavior
   |
Goal 1 — Make semantic invariants executable
   |
Goal 2 — Make interaction/seat Authority explicit
   |
Goal 3 — Consolidate monitor/workspace/client activation transitions
   |
Goal 4 — Refactor client ownership transfer, visibility, and lifecycle glue
   |
Goal 5 — Projection/input convergence and final audit
```

Do **not** stop after an intermediate goal merely to report progress or ask
whether to continue.

Do not collapse the goals into one large cross-cutting rewrite.

---

## Main refactor priorities

Concentrate on existing hidden coupling around:

- selected monitor / active workspace / semantic focused client;
- workspace-local remembered/preferred focus;
- monitor/workspace/client activation;
- `client_move_to_workspace()` and ownership transfer;
- semantic visibility versus actual X mappedness;
- manage/unmanage and asynchronous X-event causality;
- focus observation/recovery versus true semantic focus transitions;
- duplicate semantic logic in input/event paths.

Treat these existing areas primarily as local reference-quality implementations,
not rewrite targets unless a concrete dependency requires touching them:

- semantic/presented geometry and `materialize_client_geometry()`;
- monitor topology planning/reconciliation;
- semantic stack order and `enforce_stacking()`;
- the existing command-dispatch concept.

---

## Autonomous working rules

Work autonomously until Goal 0 through Goal 5 are complete.

Do not stop for:

- progress updates;
- routine or reversible engineering choices;
- compile failures;
- ordinary test failures;
- a busy X display;
- Xvfb/Xephyr startup failures that can be diagnosed;
- questions answerable from the repository, tests, TAP documents, or `ref/`.

Use this loop:

```text
inspect
 -> characterize
 -> strengthen/add behavior-preserving tests if needed
 -> make a bounded change
 -> compile
 -> run focused tests
 -> diagnose
 -> fix
 -> rerun
 -> inspect diff
 -> continue
```

A failing test is a **debugging signal**, not a stopping condition.

Never make a failing test pass by weakening, deleting, or skipping valid
behavior-preservation coverage.

When a decision is reversible and testable, make the best behavior-preserving
choice and continue.

---

## Execution environment

This run is expected to have the permissions required for the real Box2430 test
suite.

Do not request shell-command approvals or permission escalation.

Run the repository's normal tests directly, including tests using:

- Xvfb;
- Xephyr;
- child processes and real X clients;
- X11 sockets/lock files;
- dynamically allocated X display numbers.

Changing from displays such as `:99` to `:100` or another available display is
normal test behavior and does not require user intervention.

Do not skip X11 integration verification merely because it uses real X server
processes.

The run is expected to occur in a disposable Git worktree. Stay within that
worktree except for normal temporary/runtime artifacts created by the existing
test suite.

Do not modify `ref/`, the original source worktree, system configuration, or
unrelated files.

Do not install system packages or perform destructive/external actions merely
to continue this refactor.

Do not push or publish repository changes.

---

## Behavior-preservation discipline

Use evidence in this order:

1. current documented behavioral contracts;
2. current regression/integration tests;
3. current V1.7 implementation and callers;
4. local mature references;
5. TAP architectural preference.

Passing existing tests is necessary but not sufficient. Add characterization
tests where a complex established behavior is not safely covered.

Preserve important stable-state and transition invariants documented in
`docs/V1_7_TAP_REFACTOR.md`, especially:

- client/workspace/monitor ownership coherence;
- distinct membership, stable/tab, stack, and focus-history meanings;
- selected-monitor / active-workspace / focused-client coherence;
- meaningful selected monitor even when no client is focused;
- semantic visibility versus temporary X mappedness;
- focus does not normally imply raise;
- latent geometry and existing presentation precedence;
- workspace-switch paint ordering;
- WM-generated unmap causality;
- focus fallback ordering during destructive lifecycle changes;
- client requests remain requests subject to existing policy;
- X observations do not automatically become semantic Authority.

Implement/strengthen a debug/test-only semantic invariant checker where useful,
as described in the implementation guide.

---

## Verification

After every meaningful structural change:

1. compile normally with warnings enabled;
2. run focused tests for the changed path;
3. run semantic invariant checks where applicable;
4. inspect the diff before widening the change.

At the end of each goal, run the verification required by the implementation
guide.

Before declaring the full task complete:

- run the normal full build/test suite from `DEVELOPMENT.md`;
- run relevant Xvfb integration tests;
- run relevant automatable Xephyr tests;
- inspect warnings and failures;
- inspect `git diff` and `git status`;
- check for accidental duplicate semantic logic or scattered authoritative
  mutations;
- confirm `ref/` was not modified;
- confirm no unrelated feature work entered the diff.

Never report a test as passing if it was not executed.

---

## Stop conditions

Stop early only if one of these is genuinely true:

1. progress is technically impossible with the available repository/environment;
2. required behavioral information genuinely does not exist and incompatible
   interpretations would change established behavior;
3. continuing requires an irreversible/destructive action outside the task;
4. a required external dependency cannot be exercised without changing the
   machine/environment outside scope.

Before stopping, exhaust:

- repository documentation;
- existing tests;
- current callers/implementation;
- relevant local reference implementations.

Do not stop merely because a test or implementation attempt failed.

---

## Definition of done

The task is complete when Goal 0 through Goal 5 are complete and:

- established V1.7 observable behavior is preserved;
- semantic transitions are easier to identify;
- authoritative state mutations are more explicit and bounded;
- interaction/seat coherence no longer depends on accidental side effects;
- monitor/workspace/client activation has clear semantic ownership;
- client movement/visibility/lifecycle paths have clearer contracts;
- X11/EWMH/native-UI effects are identifiable as projection/protocol behavior;
- request versus observation authority is clearer;
- healthy geometry/topology/stacking behavior remains intact;
- no unnecessary framework or feature expansion was introduced;
- relevant tests pass, except for precisely documented genuine environmental
  limitations;
- the final diff remains focused on this refactor.

The goal is not architectural purity.

> **The goal is a behaviorally identical V1.7 whose current semantics are
> harder to accidentally violate.**

---

## Final report

Do not stop between goals for approval. When the full task is complete, provide
one concise final report containing:

- Goal 0–5 completion status;
- behavior characterized/preserved;
- main semantic boundaries introduced or clarified;
- important invariants made explicit/executable;
- Box2430 and `ref/` code consulted;
- tests added or strengthened;
- verification actually run;
- surprising existing behavior intentionally preserved;
- remaining known issues or genuine environmental limitations;
- final diff summary.

Do not claim architectural success merely because code moved or helpers were
renamed.
