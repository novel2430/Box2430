# Box2430 — AGENTS.md

> This file defines how Coding Agents should work inside the Box2430 repository.
>
> Keep this file operational and short.
> Product behavior, state semantics, architecture, interaction details, public commands/config, implementation style, and final acceptance are defined elsewhere.
>
> The primary delivery contract is:
>
> ```text
> GOAL.md
> ```
>
> The frozen V1 design contracts are under:
>
> ```text
> docs/V1/
> ```

---

# 1. Read Order

Before substantial implementation work, read:

```text
1. GOAL.md
2. AGENTS.md
3. relevant files under docs/V1/
```

For first-time repository work, the main V1 contracts are:

```text
docs/V1/box2430_step1_product_definition.md
docs/V1/box2430_step2_semantic_state_contract.md
docs/V1/box2430_step3_technical_architecture_contract.md
docs/V1/box2430_step4_interaction_contract.md
docs/V1/box2430_v1_command_vocabulary_and_default_config.md
docs/V1/box2430_v1_implementation_style_and_economy_contract.md
```

`box2430_post_v1_future_directions.md` is background only.

Do not treat Post-V1 ideas as V1 work.

---

# 2. Authority Boundary

The macro design is frozen.

The Agent has implementation freedom inside that boundary.

## Agent may decide

The Agent may choose:

```text
final .c file split / merge
helper names
local struct layout details
exact thin-X11 wrapper boundaries
test script organization
implementation order
small local defensive handling
small local refactors required by the current slice
```

These decisions must remain consistent with the frozen contracts.

## Agent may not decide

Do not independently:

```text
change frozen user-visible semantics
remove required V1 behavior
add new public commands
add new public config fields
rename frozen public command spelling
replace Xlib as the V1 X11 API
replace the Xinerama-first monitor model
replace the single-threaded fd-driven architecture
replace the frozen ownership model
replace the Command Registry architecture
introduce framework-scale dependencies
pull Post-V1 features into V1
```

Do not redesign a frozen behavior merely because another implementation would be easier.

---

# 3. Contract Conflicts

If two frozen contracts appear genuinely incompatible:

```text
do not invent a third semantic
do not silently choose a new product behavior
```

First verify that the conflict is real and not an implementation misunderstanding.

If it is real, surface it explicitly with:

```text
the conflicting files/sections
the exact incompatible requirements
the implementation point being blocked
```

Local implementation details that are not specified by the contracts are yours to decide.

---

# 4. Default Work Loop

Prefer vertical slices over architecture scaffolding.

Default loop:

```text
read only the relevant contracts
↓
choose the smallest complete runnable slice
↓
implement it
↓
build
↓
run the cheapest relevant verification
↓
use Xvfb / Xephyr when X11 behavior is involved
↓
fix failures
↓
continue
```

Prefer working increments over large batches of placeholders.

Do not create the whole architecture as empty modules before the first runnable WM.

Do not wait until the end of V1 to first run Box2430 under a real X server.

---

# 5. Keep the Repository Runnable

During development, prefer to keep the repository:

```text
buildable
runnable
testable
```

as often as practical.

Do not knowingly stack unrelated new work on top of an existing regression.

If the current slice breaks an existing test or behavior:

```text
fix the regression
or explicitly establish that the old test is invalid under the frozen contract
```

before continuing unrelated feature work.

---

# 6. Testing Discipline

Use the cheapest test that can genuinely verify the behavior.

```text
pure logic
→ plain C tests

headless X11 behavior
→ Xvfb

visual / interaction behavior
→ Xephyr

real X.Org / XLibre session
→ user handoff
```

Do not build fake X11 backends or dependency-injection infrastructure for tests.

If the behavior depends on real X11 semantics, test against a real X server.

Use Xephyr early when implementing visual interaction.

Screenshots are valid evidence when visual state matters.

---

# 7. Real Session Safety

Do not take over the user's real graphical session.

Unless explicitly authorized for that exact operation, never:

```text
run startx
run xinit
switch virtual terminals
replace the active window manager
take ownership of the user's active display
terminate or replace the user's desktop session
```

Real X.Org / XLibre session smoke testing is intentionally a user handoff task.

Prepare instructions; do not perform the takeover yourself.

---

# 8. Scope Discipline

Make changes that are:

```text
required by GOAL.md
or
directly necessary to make the current V1 slice correct
```

Avoid opportunistic expansion.

Do not:

```text
implement Post-V1 features "while here"
perform broad unrelated rewrites
modernize working architecture without need
replace a dependency because another one looks nicer
add generic infrastructure for hypothetical future use
```

A nearby cleanup is acceptable when it directly lowers risk or complexity of the current change.

Keep it local.

---

# 9. Dependencies and Vendors

Use the frozen dependency policy.

Prefer:

```text
libc / POSIX / Xlib / Xft / Xinerama
↓
small local helper
↓
approved small vendor
```

The TOML parser is already selected:

```text
tomlc17
```

Use the vendored copy.

Do not re-evaluate or replace it unless explicitly instructed.

Any additional vendor or linked runtime dependency must satisfy the Vendor Admission Rule in:

```text
docs/V1/box2430_v1_implementation_style_and_economy_contract.md
```

Do not let vendor-specific types spread into Box2430 core state or public internal boundaries.

---

# 10. Implementation Style

Follow the frozen Implementation Style & Economy Contract.

In particular:

```text
Readable Small-C
flat/moderate modularity
transparent core state
explicit WM * root
single clear owner / borrow by default
thin X11 boundary
canonical state mutation operations
small error-handling surface
standard facilities first
no framework inflation
```

Do not optimize for `wc -l` at the cost of readability.

Do not hide major state mutation behind clever macros.

Do not introduce generic abstractions solely to reduce repetition.

---

# 11. Errors

Keep error handling small.

Distinguish:

```text
internal invariant bug
user/config/command error
expected X11 race
fatal WM/infrastructure failure
```

Do not turn ordinary external failure into a process crash.

Do not turn error handling into a subsystem.

Use `assert` only for programmer invariants.

---

# 12. Evidence per Slice

After a meaningful implementation slice, leave the smallest useful evidence that it works.

Examples:

```text
successful build
specific unit test
Xvfb scenario
Xephyr scenario
screenshot
sanitizer result
property/state inspection
```

Do not claim something was tested if it was only reviewed.

If the environment genuinely prevents verification, report it as `UNVERIFIED` according to `GOAL.md`.

---

# 13. No Fake Completion

Never use these to claim required V1 work is done:

```text
stub handlers
TODO implementations
accepted-but-ignored config
placeholder success
hard-coded fake behavior
skipped required verification
```

Required V1 behavior must be real.

---

# 14. Final Delivery

Completion is governed by:

```text
GOAL.md
```

Before declaring V1 complete:

```text
run the required builds
run all available required verification
resolve all known FAIL items
complete the acceptance matrix
record Implementation Economy baseline
list genuine UNVERIFIED items
prepare the user real-session smoke-test handoff
```

Do not declare completion merely because the codebase is large or most features appear implemented.

---

# 15. Working Principle

When uncertain between two valid implementations, prefer the one with:

```text
less owned code
less hidden state
shorter call paths
clearer lifetime
smaller dependency surface
easier real-world verification
```

provided it preserves the frozen semantics.

The goal is not to build the cleverest WM.

The goal is to build the frozen Box2430 V1:

> small, readable, predictable, runnable, and demonstrably correct.
