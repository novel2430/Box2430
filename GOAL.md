# Microbox — V1 Goal

**Status: Frozen**

> 本文定义 Coding Agent 对 Microbox V1 的最终交付目标。
>
> 它不是新的设计文档，也不重新解释既有语义。
> Microbox V1 的产品行为、状态模型、技术架构、交互语义、公共 Command / Config Surface 与实现风格，均以 `docs/V1/` 下已经冻结的 contracts 为准。
>
> 本文只回答三个问题：
>
> 1. Agent 最终必须交付什么？
> 2. 什么条件下可以宣称 V1 完成？
> 3. Agent 必须提供什么证据证明它真的完成了？

---

# 1. Final Goal

从当前以文档为主的 Microbox repository 出发，实现一个：

```text
buildable
runnable
testable
small
predictable
X11 stacking window manager
```

并完整满足 `docs/V1/` 中已经冻结的 Microbox V1 contracts。

最终产物必须能够：

```text
make
→ build Microbox

run under a real X11 server
→ acquire WM ownership
→ manage ordinary X11 clients
→ expose the frozen V1 behavior

Xvfb / Xephyr
→ verify protocol behavior and visual interaction

user real-session handoff
→ be ready for final X.Org / XLibre smoke testing
```

最终目标不是：

> “大部分功能看起来已经写了。”

而是：

> **V1 contract 中要求的能力真实存在、可执行、可验证，并且有明确证据。**

---

# 2. Authoritative V1 Sources

Agent 必须把以下文档视为 V1 authoritative sources：

```text
docs/V1/microbox_step1_product_definition.md
docs/V1/microbox_step2_semantic_state_contract.md
docs/V1/microbox_step3_technical_architecture_contract.md
docs/V1/microbox_step4_interaction_contract.md
docs/V1/microbox_v1_command_vocabulary_and_default_config.md
docs/V1/microbox_v1_implementation_style_and_economy_contract.md
```

其中：

```text
docs/V1/microbox_post_v1_future_directions.md
```

只用于理解未来方向。

它不是 V1 scope，不得因为该文档存在就顺手实现 Post-V1 feature。

---

# 3. Contract Priority

如果实现过程中出现理解冲突：

```text
frozen user-visible semantics
>
implementation convenience
```

不得为了：

- 少写代码
- 简化数据结构
- 更容易测试
- 更容易兼容传统 EWMH
- 更符合另一个 WM 的实现方式

而修改已经冻结的 Microbox V1 行为。

如果两个 frozen documents 之间出现真实且无法同时满足的冲突：

> Agent 不应私自发明新的产品语义。

应在最终报告中明确指出冲突、涉及文件和实现选择；若任务上下文允许即时反馈，则应优先暴露该问题。

局部 implementation detail 不属于这种冲突，Agent 可以自行拍板。

---

# 4. Definition of Done

Microbox V1 只有在：

```text
implementation
+
verification
+
evidence
```

三者同时达到要求时，才可以宣称完成。

---

## 4.1 Build

必须实际验证：

```text
make
make release
make sanitize
```

至少满足：

```text
debug/developer build succeeds
release build succeeds
sanitize build succeeds
```

不得把“理论上应该能编译”作为完成证据。

---

## 4.2 Required V1 Behavior

所有 frozen V1 behavior 必须真实实现。

包括但不限于这些 capability families：

```text
WM startup / ownership

client manage / unmanage

per-monitor workspaces

selected_monitor

FREE mode

MONOCLE mode

MONOCLE Tab Bar

click / sloppy focus

workspace focus restore

tab order

MRU order and snapshot cycle

stacking order

window move-workspace

window move-monitor

--follow

--keep-workspace

mouse move / resize

cross-monitor drag semantics

mouse snapping preview / commit

keyboard snapping

maximize

fullscreen

client fullscreen allow / fake / deny

window rules

strict TOML config validation

whole-config atomic fallback

command registry

context-sensitive mouse / tab commands

practical ICCCM / EWMH support

urgency propagation

minimal fixed stacking precedence
```

上面的列表是 capability map，不替代 authoritative contracts。

具体语义仍以 `docs/V1/` 为准。

---

## 4.3 No Fake Completion

V1 required behavior 不允许以下形式冒充完成：

```text
dummy handler
placeholder success
accepted-but-ignored config field
registered command with TODO implementation
hard-coded value where config must take effect
skipped required test used to claim PASS
fake state transition
comment saying "implement later"
```

允许存在 TODO 的范围只能是：

```text
explicit Post-V1 work
non-V1 cleanup
non-blocking refactor idea
optional optimization
```

这些 TODO 不得影响 V1 correctness。

---

## 4.4 Sanitizer State

完成时不得存在：

```text
known ASan failure
known UBSan failure
known reproducible memory corruption
known reproducible use-after-free
known reproducible invalid lifetime bug
```

如果 sanitizer 本身因为当前环境或第三方组件产生已确认的外部限制，必须具体记录，不能模糊忽略。

---

## 4.5 X11 Integration

必须使用真实 X server environment 验证需要 X11 的行为。

测试层次：

```text
Pure C
Xvfb
Xephyr
User real-session handoff
```

不得用 fake X11 backend 代替 V1 integration evidence。

---

## 4.6 Xvfb

Agent 必须在环境允许时实际运行 Xvfb integration scenarios。

至少覆盖：

```text
WM startup / ownership
manage / unmanage
focus-related protocol behavior
mapping / visibility
properties / relevant ICCCM-EWMH behavior
command-to-X11 integration
```

具体 scenario 可以随着实现自然组织。

---

## 4.7 Xephyr

对于视觉或真实交互才能充分验证的行为，Agent 必须在环境允许时实际使用 Xephyr。

典型验证对象：

```text
border

initial placement

FREE presentation

MONOCLE presentation

Tab Bar

active / inactive / urgent tab appearance

snapping preview

snapping result

maximize geometry

fullscreen presentation

stacking

multi-monitor presentation where supported
```

Agent 应在有价值时：

```text
launch scenario
→ capture screenshot
→ inspect screenshot
→ compare with expected contract behavior
```

截图可以作为 acceptance evidence。

---

# 5. Real Session Boundary

Agent **不得自行启动或接管用户真实图形 session**。

除非用户针对某次操作明确授权，否则禁止：

```text
startx
xinit
switch virtual terminal
replace active WM
take over the user's active X display
terminate the user's current desktop session
```

真实：

```text
X.Org
XLibre
```

session smoke test 由用户手动执行。

Agent 的责任是：

```text
prepare a clear real-session smoke-test procedure
ensure build/install path is ready
state what the user should verify
hand off without taking over the session
```

这一项不因 Agent 未执行 `startx/xinit` 而视为未完成。

---

# 6. Verification Status

最终 acceptance evidence 只允许以下状态：

```text
PASS
FAIL
UNVERIFIED
USER HANDOFF
```

---

## 6.1 PASS

表示：

> 对应测试或 scenario 已经实际执行，并通过。

不能把代码 review 或“实现看起来正确”标成 PASS。

---

## 6.2 FAIL

表示：

> 已实际执行验证，但结果不满足 contract。

存在未解决 FAIL 时，不得宣称 V1 完成。

---

## 6.3 UNVERIFIED

只允许用于：

> implementation 已存在，但当前执行环境确实无法运行对应验证。

UNVERIFIED 必须同时说明：

```text
why verification is unavailable
where the implementation lives
how to verify it elsewhere
```

禁止使用：

```text
probably works
should work
looks correct
```

代替 verification status。

环境限制只能阻止验证，不能成为跳过实现的理由。

---

## 6.4 USER HANDOFF

专门用于根据本 Goal 明确由用户执行的验证，例如：

```text
real X.Org session
real XLibre session
```

它不同于 UNVERIFIED：

```text
UNVERIFIED
→ environment prevented Agent-side verification

USER HANDOFF
→ verification is intentionally assigned to the user
```

---

# 7. Performance / Economy Evidence

最终交付时必须记录 Implementation Economy baseline。

至少包括：

```text
Microbox-owned production LOC

clean build wall time

incremental one-file rebuild wall time

idle RSS

RSS with fixed N simple clients

idle CPU

stripped release binary size
```

这些不是 hard acceptance limits。

它们的作用是：

> 建立 V1 baseline，并证明实现没有明显违背 Implementation Style & Economy Contract。

最终报告必须记录 measurement environment，例如：

```text
compiler and version
architecture
build profile
X server used
virtual screen geometry
number/type of test clients
measurement method
```

---

# 8. Implementation Quality

完成 V1 不只要求 feature 存在，也必须继续满足 Implementation Style & Economy Contract。

至少应保持：

```text
flat/moderate Small-C structure

transparent core state

explicit WM root

single-owner / borrowed-pointer lifetime discipline

thin X11 boundary

one Command Registry path

no test-driven backend abstraction

small dependency surface

tomlc17 isolated as vendor

no framework inflation

readable code over code golf
```

如果实现通过功能测试，但明显违反 frozen implementation contract：

> 不应直接宣称 V1 完成。

---

# 9. Recommended Checkpoints

以下 checkpoints 是：

> **推荐实现顺序，不是新的 hard development contract。**

Agent 可以根据真实代码依赖调整顺序。

唯一硬约束是最终 Definition of Done。

---

## Checkpoint 1 — Bootstrap

目标：

```text
repository builds

X11 display opens

WM ownership can be acquired

basic event loop runs

ordinary client can be discovered/managed

clean shutdown path exists
```

这一阶段尽快形成：

> **第一个真实可运行 vertical slice。**

不要先搭完整 architecture scaffold 再尝试启动 WM。

---

## Checkpoint 2 — Core WM State

目标：

```text
Monitor
Workspace
Client
WM root state

per-monitor workspaces

selected_monitor

global focus

FREE visibility/state

workspace switching

focus restore/fallback

tab / MRU / stacking relationships
```

重点：

> 先让冻结的核心 semantic model 真实成立。

---

## Checkpoint 3 — Control Surface

目标：

```text
Command Registry

keyboard input

mouse/tab context plumbing

TOML config

strict validation

safe-default fallback

bindings

rules

spawn / lifecycle commands

workspace / monitor / move commands
```

重点：

> 让用户操作都进入同一 public command path。

---

## Checkpoint 4 — Main V1 Experience

目标：

```text
MONOCLE

Tab Bar

tab interaction

MRU cycle

move / resize

snapping + preview

maximize

fullscreen

client fullscreen policy

cross-monitor interaction
```

这一阶段应大量使用 Xephyr 做 visual integration。

---

## Checkpoint 5 — Hardening

目标：

```text
practical ICCCM / EWMH completion

urgency

dock / strut / workarea

stacking precedence

edge cases

config/rule behavior

pure tests

Xvfb integration

Xephyr acceptance scenarios

sanitize

Implementation Economy baseline

real-session user handoff
```

最终逐项完成 acceptance matrix。

---

# 10. Checkpoint Flexibility

Agent 可以：

```text
implement part of Checkpoint 3 before Checkpoint 2 ends
move an ICCCM helper earlier
test Xephyr from the first runnable build
split work into smaller vertical slices
```

只要这样做：

```text
reduces risk
keeps the system runnable
does not change frozen semantics
```

不应为了遵守 checkpoint 编号制造无意义依赖。

---

# 11. Preferred Development Shape

推荐循环：

```text
read relevant contract
↓
choose smallest complete vertical slice
↓
implement
↓
build
↓
run the cheapest relevant test
↓
run real X11 integration when relevant
↓
inspect result
↓
fix
↓
continue
```

不推荐：

```text
create all modules
create all structs
create all command names
create all stubs
→ only much later try to run the WM
```

优先：

> runnable increments over architecture scaffolding.

---

# 12. Scope Discipline

V1 不因为实现方便而新增 public feature。

禁止主动加入未冻结的：

```text
IPC
microboxctl
runtime mutable config
script-init
official bar
Polybar adapter
minimize
scratchpad
named-workspace public API
general stacking layer framework
always-on-top
keyboard FREE geometry
session restore
```

除非另有明确任务指示。

Post-V1 文档不是 V1 backlog。

---

# 13. Dependency Discipline

Agent 不得因为实现方便自行：

```text
replace Xlib with XCB
replace Xinerama monitor model
introduce GTK/Qt/GLib/Cairo/Pango
introduce libevent/libuv
introduce generic collection framework
introduce logging framework
introduce allocator framework
replace tomlc17
```

新增 vendor / linked runtime dependency 必须满足 Implementation Style & Economy Contract 中的 admission rule，并且不能改变当前冻结 architecture。

---

# 14. Acceptance Matrix

Agent 在宣称 V1 DONE 前，必须给出一份轻量 acceptance matrix。

至少覆盖以下 areas：

| Area | Status | Evidence |
|---|---|---|
| Build: developer |  |  |
| Build: release |  |  |
| Build: sanitize |  |  |
| WM startup / ownership |  |  |
| Manage / unmanage |  |  |
| Per-monitor workspaces |  |  |
| selected_monitor |  |  |
| FREE mode |  |  |
| Click focus |  |  |
| Sloppy focus |  |  |
| Workspace focus restore |  |  |
| Tab order |  |  |
| MRU snapshot cycle |  |  |
| Stacking order |  |  |
| Move workspace |  |  |
| Move monitor |  |  |
| `--follow` |  |  |
| `--keep-workspace` |  |  |
| Mouse move |  |  |
| Mouse resize |  |  |
| Cross-monitor drag |  |  |
| Keyboard snap |  |  |
| Mouse snap preview/commit |  |  |
| Maximize / restore |  |  |
| MONOCLE |  |  |
| MONOCLE geometry no-op |  |  |
| Tab Bar interaction |  |  |
| User fullscreen |  |  |
| Client fullscreen allow |  |  |
| Client fullscreen fake |  |  |
| Client fullscreen deny |  |  |
| Window rules |  |  |
| Hidden rule destination |  |  |
| Config strict schema |  |  |
| Config atomic fallback |  |  |
| Binding override / unbind |  |  |
| Command context validation |  |  |
| ICCCM practical subset |  |  |
| EWMH practical subset |  |  |
| Urgency |  |  |
| Dock / strut / workarea |  |  |
| Minimal stacking precedence |  |  |
| Xvfb integration suite |  |  |
| Xephyr visual scenarios |  |  |
| ASan / UBSan |  |  |
| Owned LOC baseline |  |  |
| Build-time baseline |  |  |
| RSS / idle CPU baseline |  |  |
| Binary-size baseline |  |  |
| X.Org real-session smoke test | USER HANDOFF |  |
| XLibre real-session smoke test | USER HANDOFF |  |

Agent may add rows when useful.

The matrix is intentionally lightweight:

> it is evidence bookkeeping, not a requirements-management subsystem.

---

# 15. Evidence Quality

Acceptable evidence includes:

```text
test executable + result

integration scenario script + result

Xvfb scenario result

Xephyr screenshot path

property/state inspection

sanitizer output

measurement output

clear manual observation from an actually executed scenario
```

Weak/non-evidence:

```text
"code looks correct"

"should work"

"implemented according to docs"

"probably compatible"

"not tested but straightforward"
```

---

# 16. Final Delivery Report

At the end, Agent must provide a concise final report containing:

```text
1. implementation summary

2. repository structure summary

3. build commands actually executed

4. tests/scenarios actually executed

5. acceptance matrix

6. Implementation Economy baseline

7. known limitations

8. UNVERIFIED items, if any

9. exact user real-session smoke-test procedure

10. any remaining Post-V1/non-blocking TODOs
```

Known limitations must distinguish:

```text
V1 correctness issue
vs
Post-V1/non-goal limitation
```

A known unresolved V1 correctness issue means the project is not DONE.

---

# 17. Completion Rule

Agent may declare:

```text
MICROBOX V1 IMPLEMENTATION COMPLETE
```

only if:

```text
all required V1 implementation exists

all executable required verification in the current environment is PASS

there are no unresolved FAIL rows

there are no knowingly skipped V1 behaviors

there are no known sanitizer correctness failures

all UNVERIFIED rows are caused by genuine environment limitations

all intentional real-session checks are clearly marked USER HANDOFF

the acceptance matrix and final report are complete
```

---

# 18. Final Principle

The project should reach V1 by repeatedly producing:

```text
small
real
runnable
verifiable
```

increments.

The final success criterion is not:

> how much code the Agent produced.

It is:

> **whether the frozen Microbox V1 exists as a small, readable, working X11 WM and the Agent can show concrete evidence that it does.**
