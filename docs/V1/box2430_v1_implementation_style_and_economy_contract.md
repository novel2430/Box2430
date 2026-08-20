# Box2430 — V1 Implementation Style & Economy Contract

**Document Version: V1 — Frozen**

> 本文冻结 Box2430 V1 的实现风格与实现经济性。
>
> 它不新增产品能力，也不重新定义窗口管理语义。  
> Step 1–4、Semantic / State Contract、Technical Architecture Contract、V1 Command Vocabulary and Default Config 已经定义的产品行为、状态语义、交互语义与公共接口始终优先。
>
> 本文负责回答：
>
> **在不改变既有语义的前提下，Box2430 的源码应该怎样组织、怎样拥有资源、怎样处理错误、怎样测试，以及怎样控制 LOC / build time / runtime / RSS / binary size。**

---

# 1. 总体目标

Box2430 的实现目标不是单纯追求最少 LOC，而是同时追求：

```text
small codebase
low reading overhead
explicit state
short call paths
clear ownership
small dependency surface
fast edit-build-test loop
predictable runtime behavior
low idle resource cost
```

核心判断：

> **在实现相同冻结语义的前提下，优先选择 owned LOC 更少、状态更直接、调用链更短、长期维护心智负担更低的方案。**

Box2430 不采用 dwm 式以极高代码密度换取最小源码体积，也不采用大型工程项目式的分层、框架与抽象。

总体风格：

```text
Small-C
+
flat/moderate modularity
+
explicit state
+
thin boundaries
+
measured implementation economy
```

---

# 2. 与其他 V1 Contract 的关系

本文不得反向改变：

- Product Definition
- Semantic / State Contract
- Technical Architecture Contract
- Interaction Contract
- V1 Command Vocabulary
- V1 Default Config Surface

如果实现风格与已冻结语义冲突：

```text
existing semantic / architecture / interaction contract
> implementation convenience
```

例如：

- 不得为了少写代码而合并 `tab_order / mru_order / stacking_order`
- 不得为了减少状态字段而破坏 `normal_geometry`
- 不得为了简化 command handler 而让 input 绕过 Command Registry
- 不得为了 EWMH 兼容改变 per-monitor workspace 语义

---

# 3. Source Layout

采用：

> **扁平、适度模块化的 Small-C。**

不采用单一巨大 `box2430.c`，也不建立多层 directory architecture。

概念粒度可以类似：

```text
src/
├── main.c
├── wm.c
├── client.c
├── workspace.c
├── monitor.c
├── event.c
├── input.c
├── command.c
├── config.c
├── rules.c
├── x11.c
└── draw.c
```

以上名称不是逐文件硬性 contract；真正冻结的是模块粒度与拆分原则。

一个 `.c` 应在以下情况才拆出：

> 它已经承担一块完整、容易描述的职责，并且拆出后能降低阅读负担。

避免：

```text
one giant multi-thousand-line source file
```

也避免：

```text
dozens of tiny 30–80 line modules
requiring constant file hopping
```

普通 feature 的主要阅读路径应尽量控制在：

```text
1–3 main modules
```

---

# 4. Header Policy

不机械采用：

```text
foo.c + foo.h
bar.c + bar.h
```

一一对应。

Header 只用于：

> **真正存在的跨 translation-unit interface。**

模块内部 helper 默认：

```c
static
```

因此：

> 看到一个 header，应当意味着这里确实存在一个 shared boundary，而不是单纯因为存在同名 `.c`。

不要求建立独立 `include/` hierarchy；项目规模没有产生这种需求前，保持扁平。

---

# 5. Core State

核心状态采用：

> **集中、透明的 shared structs。**

主要运行时对象继续直接对应 V1 semantic model：

```text
WM
Monitor
Workspace
Client
```

不采用 opaque-object 风格，也不通过大量 getter / setter 隐藏核心状态。

允许直接读取：

```c
c->workspace
wm->focused_client
m->active_workspace
```

因为这些关系本身就是 Box2430 最重要、最应该被读者直接理解的状态。

但重要 mutation 必须通过 canonical semantic operation。

例如：

```c
client_focus(...)
client_move_to_workspace(...)
workspace_activate(...)
client_set_fullscreen(...)
```

原则：

```text
state reading
→ direct

state transition with invariants
→ canonical operation
```

禁止让多个无关模块各自复制一套跨对象状态迁移逻辑。

---

# 6. Root State and Globals

核心 mutable state 统一收进：

```c
WM *wm
```

并在需要访问核心 runtime state 的函数之间显式传递。

例如：

```c
client_focus(WM *wm, Client *c);
workspace_activate(WM *wm, Monitor *m, Workspace *ws);
```

不采用 dwm 式大量 mutable globals。

允许：

- file-local `static` helper
- immutable/static lookup table
- compile-time constant
- truly process-global immutable metadata

但不把核心运行状态隐藏为 global variables。

纯 helper 不机械接受 `WM *`：

```c
Rect rect_intersection(Rect a, Rect b);
```

因此阅读时可以形成有用直觉：

```text
has WM *
→ may read or mutate WM runtime state

no WM *
→ normally local / pure calculation
```

---

# 7. Ownership and Lifetime

采用：

> **single clear owner + borrowed pointers by default。**

核心原则：

```text
multiple pointers to one object
!=
multiple owners
```

V1 不引入：

- generic reference counting
- ownership wrapper
- arena framework
- custom allocator framework
- memory pool framework

默认使用：

```text
malloc / calloc / realloc / free
+
Xlib/Xft 对应资源释放 API
```

函数参数中的 pointer 默认 borrowed。

若发生 ownership transfer，必须由函数语义、命名或紧邻注释明确表达。

---

## 7.1 Client Lifetime

推荐心智模型：

```text
WM owns Client lifetime

Workspace / focus / MRU / tab / stacking
hold semantic or borrowed references
```

因此：

```text
Client moves WS1 → WS2
→ membership changes
→ lifetime owner does not
```

`focused_client`、`last_focused_client`、tab/MRU links 指向同一个 Client 时，不构成 shared ownership。

Client unmanage / destroy 必须沿一个明确 teardown path 清理所有会成为 dangling reference 的关系。

---

## 7.2 Subordinate Resources

自然附属于对象的资源跟随该对象生命周期。

例如 Client-owned metadata：

```text
title
class
instance
other copied metadata
```

随 Client teardown 释放。

Config-owned binding/rule data 随 Config lifetime 释放。

---

## 7.3 Xlib / External Memory

Xlib 返回的临时资源尽量：

```text
acquire
→ inspect / copy
→ XFree nearby
```

不要把 Xlib-owned temporary data 的 lifetime 扩散到整个程序。

读取第三方或 Xlib 数据后，如果运行期需要长期持有，应转换为 Box2430-owned representation。

---

## 7.4 Allocation Failure

对于 Box2430 这种常驻小型 WM：

> **无法满足核心 runtime allocation 的 OOM 通常视为不可恢复基础设施失败。**

不建立复杂 OOM recovery subsystem。

可选、非关键 UI 临时资源如果存在自然降级路径，可以局部失败；否则明确诊断并退出。

---

# 8. Xlib Boundary

采用：

> **thin X11 boundary。**

不完整包裹 Xlib，也不建立 platform backend abstraction。

适合封装的内容：

```text
ICCCM / EWMH protocol knowledge
Atom/property boilerplate
Xlib-owned temporary resources
repeated multi-step X11 operations
known X11 error-sensitive operations
```

例如：

```c
x11_read_window_type(...)
x11_read_class(...)
x11_request_close(...)
x11_set_wm_state(...)
```

简单、透明、没有额外协议知识的调用可以直接出现：

```c
XRaiseWindow(...);
```

判断标准：

> wrapper 必须减少调用者需要掌握的知识；如果只是把 `XFoo()` 重命名成 `x11_foo()`，通常不值得存在。

明确不建立：

```text
WindowBackendOps
generic platform interface
Wayland backend abstraction
fake X11 backend
```

---

# 9. Input / Command / Core Operation

公共控制路径保持：

```text
Input
↓
Command
↓
Core operation
↓
X11 side effect
```

`input.c` 类代码负责：

- keyboard event
- mouse event
- tabbar event
- MRU held-modifier lifecycle
- 构造 `CommandContext`

`command.c` 类代码负责：

- public argv grammar
- argument parsing
- command validation
- context validation
- 调用 core operation

Command handler 保持薄。

例如：

```c
cmd_workspace(...)
{
    /* parse / validate */
    return workspace_activate(...);
}
```

真正维护 invariant 的是 core operation。

禁止继续扩张为：

```text
Controller
Service
UseCase
Repository
State Manager
Backend Service
```

TOML binding、未来 IPC / `box2430ctl` 必须继续复用相同 Command Registry / handlers。

---

# 10. Third-Party Dependency Policy

正式核心系统依赖继续保持：

```text
libX11
libXinerama
libXft
```

小型第三方实现可以 vendor，但必须通过本文 Vendor Admission Rule。

---

## 10.1 TOML Parser

V1 TOML parser 固定使用：

```text
cktan/tomlc17
```

以 vendored source 纳入仓库。

概念结构：

```text
vendor/
└── tomlc17/
    ├── tomlc17.c
    ├── tomlc17.h
    └── LICENSE
```

必须 pin 明确 upstream version / commit。

Vendor 源码原则上保持上游原样，不在其中混入 Box2430-specific patch；若未来确实必须 patch，应单独记录理由。

---

## 10.2 TOML Lifetime Boundary

Parser 只负责 syntax：

```text
TOML
↓
temporary parse result
```

Box2430 `config.c` 自己负责：

```text
schema
value validation
command validation
context validation
whole-config atomic validation
defaults
```

完成 config load 后：

```text
parse
→ validate
→ copy into Box2430-owned Config
→ free parser result
```

Parser tree 不成为长期 runtime state。

---

## 10.3 Vendor Isolation

Vendor 应被视为：

> **sealed implementation island。**

禁止让 vendor-specific types 大面积泄漏进入：

- core state structs
- shared Box2430 headers
- command APIs
- workspace/client interfaces

理想边界：

```text
vendor
↓
small adapter / one local subsystem
↓
Box2430-owned representation
```

Vendor code 自身不需要符合 Box2430 coding style，也不能反过来改变 Box2430 coding style。

---

## 10.4 Build-Time Dependency Discipline

构建过程不得在 `make` 时自动：

- clone Git repository
- download vendor source
- bootstrap package manager
- run code generator from the network

需要的 vendor source 应已经存在仓库中。

---

# 11. Standard Facilities First

实现优先级：

```text
libc / POSIX / Xlib / Xft / Xinerama
↓
tiny local helper
↓
small vendor
↓
self-built subsystem
```

这是 `prefer`，不是盲目的 `must`。

如果标准 API 明显更晦涩、可读性更差或不能忠实满足语义，可以不用。

典型优先复用方向：

```text
fnmatch()
→ simple rule glob matching

getopt()/getopt_long()
→ CLI parsing when needed

poll()
→ fd-driven event loop

fork()/execvp()
→ spawn

getenv()
→ XDG/config path

Xlib ICCCM helpers
→ where they correctly express required protocol behavior
```

不要为了几十行代码重复实现已经成熟且足够清楚的标准设施。

---

# 12. Generic Collection Policy

默认不 vendor：

- uthash
- klib collections
- generic list framework
- generic vector framework
- generic string framework

Box2430 的 client / workspace 数量很小，purpose-built linked relationship / small array 通常更容易读。

只有在未来出现真实规模、复杂度或 correctness 问题时才重新评估。

不为了把：

```text
O(n)
```

机械优化成：

```text
O(1)
```

而增加新的数据结构与 mental model。

---

# 13. Error Handling

总体实现规模：

> **对齐 bspwm，而不是 i3 / Openbox 的完整 error/logging subsystem。**

但 X11 error 单独分类，吸收 dwm / Openbox 对 expected X11 race 的处理思想。

---

## 13.1 Error Classes

```text
Internal invariant violation
→ assert

Unrecoverable startup / WM infrastructure failure
→ fatal
→ stderr diagnostic
→ exit non-zero

User / config / command failure
→ explicit local error
→ WM continues

Expected X11 runtime race
→ recognize
→ contain / ignore locally
→ optional diagnostic

Unexpected serious X11 / infrastructure failure
→ diagnose
→ exit only when correct WM operation can no longer continue
```

---

## 13.2 Assert Policy

`assert` 只用于：

> **如果失败，就说明 Box2430 implementation 本身有 bug。**

例如核心 invariant 被破坏。

绝不使用 assert 验证：

- TOML / user input
- command argv
- client-supplied property
- normal X11 race
- ordinary system-call failure

---

## 13.3 Logging Surface

保持极小。

概念上最多：

```c
warn(...);
err(...);   /* fatal */
```

以及 command/config 所需的简单 diagnostic。

正常运行默认安静。

不引入：

- Logger object
- log sink
- full log-level hierarchy
- error object hierarchy
- exception-like propagation
- GUI error reporter
- nagbar-like subsystem

System-call failure diagnostic 应尽量带上足够定位问题的信息，例如操作名与系统错误文本，但不因此建立通用 error framework。

---

## 13.4 Command Failure

Command failure 是正常控制流。

例如：

```text
invalid argument
invalid context
unknown target
no focused client
```

应返回一个小而明确的 status，由调用方决定是否向用户/IPC 输出错误。

不因为一个 command 失败终止 WM process。

不建立 heap-allocated Result/Error object。

---

## 13.5 X11 Error Handling

X11 error 属于独立类别。

已知 destroyed-window race、合法 `BadWindow` / 特定 `BadMatch` 等，不应自动升级成 fatal。

允许：

- centralized small X error handler
- 对已知 race 的明确白名单
- scoped tolerate/ignore 某段已知可能出现 race 的 X11 operation

但禁止演化为 general recovery framework。

---

# 14. C Coding Style

总体原则：

> **Readable Small-C, not compressed C。**

---

## 14.1 Naming

普通函数使用清楚、可搜索的 `snake_case`：

```c
client_focus(...)
client_move_to_workspace(...)
workspace_activate(...)
```

避免大量项目私有缩写。

核心 typedef 可使用：

```c
Client
Workspace
Monitor
WM
Geometry
SnapState
```

enum value 保持显式：

```c
WORKSPACE_FREE
WORKSPACE_MONOCLE

SNAP_NONE
SNAP_LEFT
SNAP_TOP_LEFT
```

---

## 14.2 Visibility

模块内部函数默认：

```c
static
```

只有真实跨模块入口才进入 shared header。

不为了测试方便扩大 production symbol visibility。

---

## 14.3 Function Size

不规定机械的函数最大行数。

标准是：

> 一个函数能否用一句清楚的话描述其完整操作。

允许一个完整 initial-manage flow 自然较长。

不为了压行数拆成：

```text
step1()
step2()
helper3()
```

这种没有独立语义的函数。

---

## 14.4 `goto`

允许并推荐：

```text
goto cleanup
goto fail
```

用于 C-style resource unwind。

不使用 `goto` 代替普通 loop / ordinary control flow。

---

## 14.5 Macro

Macro 极度克制。

允许：

- compile-time constant
- 极小 compile-time helper
- 清楚且无隐藏副作用的 utility macro

不允许 macro 隐藏复杂 state mutation。

如果读者看到一个 macro invocation 后无法直接判断它会改哪些核心状态，该 macro 通常不应该存在。

---

## 14.6 Bool / Enum / Bitmask

优先：

```text
bool
enum
explicit field
```

bitmask 只在概念天然是 flag set 时使用，例如：

```text
allowed command contexts
```

不为了节省几个 bytes 把普通 runtime state 压成难读的 bitfield/flag word。

---

## 14.7 `const`

对 borrowed 且只读的数据，在不制造繁琐类型转换的前提下优先使用 `const`。

目的：

```text
make mutation intent visible
```

而不是追求形式主义 const-correctness。

---

## 14.8 Comments

注释主要解释：

```text
why
invariant
non-obvious X11 requirement
important semantic trap
```

不逐句翻译显然的代码。

对容易被未来重构误伤的 invariant，应在最接近 mutation 的地方留下简短说明。

---

# 15. C Language / Portability Baseline

V1 production code 使用：

```text
C11
```

作为语言 baseline。

理由：

- 足够现代
- 普遍可用
- 不要求 GNU-only language mode
- 与当前依赖及 vendored parser 兼容

默认避免无必要 GNU C extension。

POSIX / X11 API 可以正常使用；Box2430 本来就是 Linux/X11 window manager，不追求 ISO C-only portability。

---

# 16. Build System

继续使用：

```text
Makefile
```

目标保持：

```sh
make
sudo make install
```

不引入：

- CMake
- Meson
- Python build helper
- code generator
- package-manager bootstrap

---

## 16.1 Translation Units

Vendor 作为独立 translation unit 编译。

例如：

```text
tomlc17.c
→ tomlc17.o
```

禁止通过：

```c
#include "tomlc17.c"
```

把 vendor source 塞入 Box2430 translation unit。

目的：

- 保持 incremental build 快
- 保持 boundary 清楚
- vendor 修改不污染主要依赖关系

---

## 16.2 Incremental Build

Makefile 应只重建发生变化的 translation unit 及其真实依赖。

允许使用 compiler-generated dependency files 等简单机制，但不引入额外 build framework。

---

# 17. Build Profiles

只保留两个正式 profile：

```text
debug
release
```

以及一个开发检查 target：

```text
sanitize
```

---

## 17.1 Default Developer Build

```sh
make
```

是快速 debug-ish build。

推荐方向：

```text
-Og
-g
-Wall
-Wextra
-Wpedantic
assert enabled
```

可根据真实 compiler noise 增加少量高价值 warning，例如：

```text
-Wshadow
-Wformat=2
```

不默认：

```text
-Werror
-Weverything
-Wconversion
```

避免 compiler 升级把普通用户 build 变成 failure。

---

## 17.2 Sanitizer Build

```sh
make sanitize
```

用于：

```text
ASan
UBSan
debug symbols
```

不作为日常默认 build。

---

## 17.3 Release Build

```sh
make release
```

推荐：

```text
-O2
-DNDEBUG
no sanitizer
```

不默认：

- `-Ofast`
- LTO
- profile-guided optimization
- aggressive size tricks

只有实际 benchmark 证明有收益时再重新评估。

---

# 18. LOC Budget

LOC 使用：

> **soft budget, not hard acceptance criterion。**

重点统计：

```text
Box2430-owned production LOC
```

而不是整个 repository 总 LOC。

分类：

```text
src/
→ owned production LOC

vendor/
→ separately recorded vendor LOC
→ not counted into owned budget

tests/
→ separately recorded test LOC
```

V1 初始目标：

```text
owned production LOC target
≈ 5,000–8,000

architecture review threshold
≈ 10,000
```

超过约 10k 不自动视为失败。

它表示：

> 应重新检查是否出现 abstraction inflation、重复实现成熟设施、过度拆层或 scope creep。

LOC 不允许成为 code-golf incentive。

不得为了压 LOC：

- 缩写到难读
- 合并独立状态
- 使用复杂 macro
- 把多个 semantic operation 塞进一个 expression
- 牺牲 error handling clarity

每次正式记录 LOC 时，应同时记录采用的统计方法，避免不同工具口径造成假比较。

---

# 19. Implementation Economy

Box2430 的实现经济性关注：

```text
1. owned LOC
2. reading / mental cost
3. runtime predictability
4. RSS
5. build time
6. binary size
```

不追求理论上的极限 throughput。

真实 WM workload 通常只有个位数到几十个 client，因此：

```text
simple O(n)
```

通常优先于更复杂的数据结构。

---

# 20. Runtime Performance Policy

优先优化真正有现实成本的东西：

```text
X11 synchronous round-trips
unnecessary XSync
repeated property queries
unnecessary redraw
event storms
repeated font/color allocation
duplicate parsing
```

不优先优化：

```text
10 clients 上的 O(n) lookup
几十纳秒级 command dispatch
微小 struct field layout
```

---

## 20.1 Event-Driven Cached State

运行期优先：

```text
internal state as truth
+
event-driven updates
```

例如：

```text
Atoms
→ initialize/cache once

Xft fonts
→ load and keep while config is active

Xft colors
→ allocate when config is applied

Client metadata
→ update when relevant property changes

Geometry
→ WM state is authoritative

Monitor topology
→ refresh on actual topology change
```

避免 query-driven design：

```text
need value
→ ask X server again
```

如果 Box2430 已经知道答案，就读取内部 state。

---

# 21. RSS Policy

RSS 优化首先依赖：

```text
small runtime dependency set
```

而不是 struct packing。

避免大型 runtime：

- GTK
- Qt
- GLib
- Cairo
- Pango
- libevent
- libuv

不为了节省几十/几百 bytes：

- 使用难读 bitfield
- 手写 allocator
- 引入 object pool
- 压缩普通 pointer/state representation

主要关注：

```text
no unnecessary long-lived parser state
release Xlib temporary data
avoid large caches
avoid duplicate metadata
free inactive/replaced config-owned resources
```

---

# 22. Binary Size Policy

Binary size 记录为 baseline 指标，但不是首要目标。

默认 release 不为了 binary size 引入复杂 compile/link flags。

未来可以基于真实 measurement 评估：

```text
-Os
LTO
section GC
other linker options
```

但只能在：

```text
measured gain
>
build/complexity cost
```

时加入。

---

# 23. Performance Baseline

建立一个非常小的 macro baseline。

它用于：

> **regression detection，而不是 microbenchmark competition。**

不建立复杂 benchmark framework。

概念上可提供：

```text
tools/measure.sh
```

或等价简单入口。

至少记录：

```text
Build
├── clean build wall time
└── incremental one-file rebuild wall time

Runtime
├── idle RSS
├── RSS with fixed N test clients
└── idle CPU

Binary
└── stripped release binary size
```

不为这些指标预先冻结硬限制。

---

## 23.1 Baseline Reproducibility

每份正式 baseline 至少记录：

```text
compiler / version
build profile
host architecture
test X server
virtual screen geometry
number/type of test clients
measurement command/method
```

否则 before/after 数字不应被视为严格可比。

---

# 24. No Microbenchmark Suite

默认不建立：

```text
client lookup ns/op
rule matcher millions/sec
command dispatch ns/op
```

这类 benchmark。

如果未来 profiling 证明某个函数真的是实际 hot path，再针对真实问题增加局部 benchmark。

---

# 25. Testing Philosophy

原则：

> **需要 X11 的测试就使用真正的 X server，不为了测试 mock Xlib。**

形成四层测试：

```text
Level 1 — Pure C Logic
Level 2 — Xvfb Headless Integration
Level 3 — Xephyr Visual Integration
Level 4 — Real X Session Smoke Test
```

---

# 26. Level 1 — Pure Logic

使用普通 C 测试。

适合：

- command parsing / validation
- rule matching
- config validation
- geometry
- MRU
- tab order
- core state transitions

不引入大型 test framework。

可以拥有极薄 test helper / assertion macro。

不为了测试而把本来 `static` 的 implementation detail 强行公开。

---

# 27. Level 2 — Xvfb

Xvfb 用于：

```text
headless integration
CI
Agent automated testing
real X11 protocol interactions
```

适合：

- manage / unmanage
- property handling
- focus
- map/unmap
- command/X11 integration
- protocol-level acceptance

---

# 28. Level 3 — Xephyr

Xephyr 用于真正的视觉/交互 integration。

典型 Agent 流程：

```text
launch Xephyr
→ launch box2430
→ launch real X clients
→ perform interaction
→ capture screenshot
→ agent/human inspect
```

适合验证：

- border
- placement
- MONOCLE tab bar
- snapping preview
- snapping result
- fullscreen
- stacking
- multi-monitor presentation

Xephyr visual testing 是正式开发验证手段，不只是人工 demo。

---

# 29. Level 4 — Real X Session

最终在真实：

```text
X.Org
XLibre
```

session 做 smoke test。

可通过：

```text
xinit / startx
```

进入真实 WM session。

它用于最终确认：

> Box2430 在真实 session 中确实能作为 WM 工作。

不把 `startx` 作为 Coding Agent 日常开发测试方式。

---

# 30. Tests Must Not Shape Production Architecture

明确禁止为了测试方便建立：

```text
X11Interface
MockX11Backend
DependencyInjection framework
fake window backend
```

测试适应 production architecture。

production architecture 不为了 mocking 变复杂。

Debug/sanitize tooling 属于开发设施，不是 runtime dependency。

---

# 31. Vendor Admission Rule

任何新的 vendor 都必须回答：

```text
1. 它减少多少 Box2430-owned LOC / edge cases？

2. 它暴露给 Box2430 的 API surface 有多大？

3. 它是否引入新的 runtime architecture？
   thread / event loop / allocator / callback framework 等

4. 它对 build / RSS / binary / runtime 有什么实际代价？

5. 如果未来移除或替换它，
   Box2430 core state / command / architecture 是否基本不需要改变？
```

---

## 31.1 GOOD Vendor

满足：

```text
large reduction in boring owned LOC
eliminates real edge cases
small API boundary
no new runtime architecture
vendor types stay local
easy replacement boundary
```

可以引入。

---

## 31.2 QUESTIONABLE Vendor

例如：

```text
saves only 50–200 LOC
but adds macro DSL
or new mental model
or significant adapter code
```

默认不引入。

---

## 31.3 REJECT Vendor

如果要求 Box2430 采用它的：

- event loop
- object model
- allocator model
- thread model
- generic state/container architecture
- large callback framework

即使能减少 LOC，也默认拒绝。

---

## 31.4 New Linked Runtime Dependencies

相比 vendored parser-like code：

> **新增 linked runtime/system dependency 的门槛更高。**

因为它直接影响：

- installation surface
- RSS
- dynamic loader/runtime behavior
- distribution packaging
- dependency availability

除现有正式依赖外，新增系统 runtime library 应先经过显式 architecture review。

---

# 32. Development Loop

Box2430 的日常开发循环应保持：

```text
edit
→ make
→ test
```

快速、直接。

常见 Agent loop：

```text
edit
→ make
→ pure test / Xvfb
→ Xephyr scenario
→ screenshot / inspect
```

不要求每轮：

- full clean rebuild
- sanitizer
- real startx session
- complete acceptance suite

复杂测试按变更风险选择。

---

# 33. Final Implementation Heuristics

以后实现一个 feature / helper / dependency 时，依序问：

```text
1. 既有 libc/POSIX/Xlib 能不能简单完成？
2. 能不能用一个小而清楚的 local helper 完成？
3. 如果自己写，会产生多少 boring LOC / edge cases？
4. vendor 是否能用一个很小 boundary 吃掉这些复杂度？
5. 它会不会泄漏新的 mental model 到 core？
6. 是否真的需要优化 runtime，还是只是理论复杂度更漂亮？
7. 有没有 measurement 支持这个优化？
```

优先：

```text
simple
measured
replaceable
explicit
```

谨慎：

```text
generic
clever
framework-driven
micro-optimized
implicit
```

---

# 34. V1 Implementation Invariants

以下规则正式冻结。

## IMPL1 — Flat Small-C

项目采用扁平、适度模块化 Small-C，不采用 giant-file extreme，也不采用多层 framework architecture。

## IMPL2 — Headers Represent Real Boundaries

`.c/.h` 不机械一一对应；内部 helper 默认 `static`。

## IMPL3 — Core State Is Transparent

`WM / Monitor / Workspace / Client` 核心状态直接可读，不建立 getter/setter object layer。

## IMPL4 — Canonical State Mutation

重要跨对象 state transition 必须通过明确 core operation。

## IMPL5 — Explicit WM Root

核心 mutable runtime state 进入 `WM *wm`，不使用大量 hidden mutable globals。

## IMPL6 — Single Owner, Borrow by Default

普通 pointer 默认 borrowed；V1 不引入 generic refcount / arena / ownership framework。

## IMPL7 — Thin X11 Boundary

只封装真正隐藏协议知识或重复复杂模式的 X11 操作。

## IMPL8 — One Command Path

Input / TOML / future IPC 不绕过 Command Registry；handler parse/validate，core operation 维护 invariant。

## IMPL9 — Standard Facilities First

优先 libc/POSIX/Xlib/Xft/Xinerama，再考虑 local helper/vendor。

## IMPL10 — Vendor Must Stay Local

Vendor type/API 不得侵入 core architecture。

## IMPL11 — bspwm-scale Error Handling

错误设施保持极小；internal bug / user error / X11 race / fatal infrastructure failure 明确分开。

## IMPL12 — Readable C Over Dense C

命名、control flow、state mutation 以可读性为优先，不进行 code golf。

## IMPL13 — C11 Baseline

V1 使用 C11，默认避免无必要 GNU-only language extension。

## IMPL14 — Simple Make Build

保持 Makefile 与快速 incremental build；vendor 单独编译。

## IMPL15 — Small Build Surface

正式 build profile 只有 debug / release；sanitize 是开发检查 target。

## IMPL16 — LOC Is a Soft Signal

owned production LOC 目标约 5k–8k；约 10k 触发 architecture review，不构成硬失败。

## IMPL17 — Measure Macro Cost, Not Toy Throughput

正式 baseline 关注 build / RSS / idle CPU / binary size，不建立无现实意义 microbenchmark suite。

## IMPL18 — Real X Servers for Integration

Xvfb / Xephyr / real X session 承担 X11 integration；不为测试创建 fake backend。

## IMPL19 — No Test-Driven Architecture Inflation

测试不得迫使 production code 引入 DI/backend/interface framework。

## IMPL20 — Optimization Requires Value

任何降低 LOC、build time、runtime、RSS、binary size 的技巧，如果增加的阅读/架构成本大于实际收益，则不采用。

---

# 35. Current Chosen Vendor

V1 当前确认：

```text
tomlc17
→ YES
→ vendored
→ config syntax only
```

当前默认不引入：

```text
generic hashmap library
generic collection framework
logging library
allocator / arena library
event-loop library
string framework
JSON library
```

未来只有真实 feature 产生需求后，才按 Vendor Admission Rule 单独评估。

---

# 36. Final Design Summary

Box2430 V1 的实现人格可以压缩成：

```text
Readable Small-C
│
├── flat source tree
├── transparent core state
├── explicit WM root
├── obvious lifetime
├── thin X11 protocol boundary
├── one command path
├── standard facilities first
├── carefully isolated vendor code
├── bspwm-scale error handling
├── real-X11 testing
├── soft LOC budget
├── fast incremental build
└── measured, non-dogmatic optimization
```

最重要的原则：

> **代码少是目标，但“让读者少想”比“让 `wc -l` 少算”更重要。**

以及：

> **Vendor 可以替我们承担复杂度，但不能把它自己的架构带进 Box2430。**

最终期望得到的不是“最短的 X11 WM”，而是：

> **一个规模小、结构直观、依赖克制、能够被人和 Coding Agent 都快速理解、修改、编译和真实验证的 X11 window manager。**

---

# 37. Status

```text
V1 Implementation Style & Economy Contract
→ FROZEN
```

未经显式重新决策，不应：

- 将项目重构为多层 framework architecture
- 将核心 state opaque 化
- 引入大量 mutable globals
- 为少量 LOC 引入 generic container/runtime framework
- 使用 vendor 改写 core architecture
- 为测试建立 fake backend / DI
- 用硬 LOC 上限推动 code golf
- 为理论性能牺牲可读性
- 在无 measurement 的情况下加入复杂优化
