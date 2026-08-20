# Microbox — Step 2 Semantic / State Contract

**Document Version: V3**

> 本文定义 microbox 的核心状态模型、状态归属关系与主要状态迁移语义。
> 本阶段不约束具体语言、Xlib/XCB 选择、代码组织方式或底层实现机制。
> 目标是确保后续实现无论采用何种技术路线，都不会改变用户可感知的窗口管理语义。
>
> **Revision:** Interaction Contract 已确认 `selected_monitor` 必须是一等状态，因此本文不再使用由 focused client 推导 active monitor 的旧模型。
>
> **V2 revision:** 新增 workspace derived/presentation state：`active / selected / occupied / urgent`；其中 urgency 由 `Client.urgent` 推导，并仅在对应 client 真正获得 keyboard focus 后清除。
>
> **V3 revision:** 对齐 V1 freeze：只保留 click/sloppy focus；新窗口默认 destination 为 `selected_monitor.active_workspace`；hidden workspace client 不可见且不可正常获得 keyboard focus；补充 MRU cycle snapshot session、MONOCLE geometry no-op、maximize 恢复到唯一 `normal_geometry`、rule destination/focus 语义与 V1 最小 stacking precedence。

---

## 1. Step 2 目标

Step 2 负责回答：

> **microbox 在任意事件发生前后，系统状态应该是什么？**

本阶段重点锁定：

- Monitor / Workspace / Client 的归属关系
- Focus 与 selected monitor
- Workspace focus history
- MRU / Tab / Stacking 顺序
- 新窗口进入系统后的状态
- Client 消失后的 focus fallback
- Workspace / Monitor 间移动语义
- FREE / MONOCLE 状态关系
- Snap / Fullscreen / Geometry 的状态边界
- 系统级 invariants

---

# 2. 核心状态模型

概念模型：

```text
WM
├── monitors[]
│   ├── workspaces[]
│   │   ├── mode
│   │   ├── clients[]
│   │   ├── tab_order[]
│   │   ├── mru_order[]
│   │   └── last_focused_client
│   │
│   └── active_workspace
│
├── selected_monitor
└── focused_client
```

Client 概念状态：

```text
Client
├── xid
├── workspace
├── geometry
├── normal_geometry
├── snap_state
├── maximized
├── fullscreen_state
├── urgent
├── metadata
└── stacking state
```

以上仅表示状态语义，不要求实际代码必须采用相同 struct/class 划分。

---

# 3. Client / Workspace / Monitor 唯一归属

## 3.1 Client 只属于一个 Workspace

任何普通 managed client 在任意时刻：

```text
Client
→ exactly one Workspace
```

microbox 不采用 dwm tag 模型。

不存在：

```text
Firefox ∈ Workspace 1 + Workspace 3
```

普通 Client 也不存在跨 workspace 同时可见的 sticky 语义。

---

## 3.2 Workspace 只属于一个 Monitor

Workspace 是 monitor-local entity。

```text
Monitor A
├── WS1
├── WS2
└── WS3

Monitor B
├── WS1
├── WS2
└── WS3
```

其中：

```text
A:WS2 != B:WS2
```

两者只是拥有相同 local index。

Workspace 不在 monitors 间共享。

---

## 3.3 每个 Monitor 同时只有一个 Active Workspace

例如：

```text
Monitor A:
active_workspace = WS2
```

不支持：

```text
WS2 + WS3 simultaneous view
```

这也是 microbox 与 tag/view 系统的重要区别。

---

# 4. Focus Model

## 4.1 全局只有一个真实 Keyboard Focus

整个 X session：

```text
focused_client = Client | None
```

不能存在：

```text
Monitor A focused_client = Firefox
Monitor B focused_client = kitty
```

这种两个真实 keyboard focus 并存的状态。

---

## 4.2 Workspace 保存 Focus History

每个 workspace 保存：

```text
last_focused_client
```

该状态表示：

> 用户最后一次离开此 workspace 时，正在使用哪个 client。

切换 workspace 不会主动破坏该 workspace 的 focus history。

---

# 5. Workspace 切换后的 Focus 恢复

假设：

```text
WS2:
Firefox
Emacs      ← focused
kitty
```

用户在当前 `selected_monitor` 上切到 WS3，再回到 WS2。

默认：

```text
Emacs ← focused
```

也就是说：

```text
workspace switch on selected_monitor
→ restore workspace.last_focused_client
```

如果该 client 已：

- closed
- unmanaged
- moved away

则使用该 workspace 当前 mode 对应的 focus fallback。

如果 workspace 已经为空：

```text
focused_client = None
```

不跨 workspace 或 monitor 强行寻找其他 client。

---

# 6. Selected Monitor

microbox 将：

```text
selected_monitor
```

作为一等 WM 状态。

它表示：

> 当前用户正在操作哪一块 monitor。

这与：

```text
focused_client
```

是两个相关但不同的概念。

因此以下状态是合法的：

```text
selected_monitor = Monitor B
focused_client = None
```

例如用户主动切换到一块当前 workspace 为空的 Monitor B 时，B 仍然可以被正常选中。

---

## 6.1 Focus 与 Selected Monitor 的一致性

如果存在真正 focused client，则它必须位于 selected monitor：

```text
focused_client != None
→ focused_client.monitor == selected_monitor
```

因此 focus 一个 client 时：

```text
focus client @ Monitor A
→ selected_monitor = Monitor A
→ focused_client = client
```

但反方向不成立：

```text
selected_monitor = Monitor B
```

并不要求 B 当前必须存在 focused client。

---

## 6.2 Monitor Navigation

用户可以显式切换 selected monitor：

```text
monitor next
monitor prev
```

Monitor navigation：

- 在现有 monitors 间循环
- 总是允许选择空 monitor / 空 workspace
- 将 pointer warp 到目标 monitor 中心
- 若目标 monitor 当前 workspace 有合法 `last_focused_client`，恢复其 focus
- 若目标 workspace 为空，则 `focused_client = None`

概念：

```text
monitor next
→ selected_monitor = target
→ pointer = target monitor center
→ restore target workspace last focus if available
```

pointer warp 使用目标 monitor 中心，而不是保持相对坐标。

---

## 6.3 Workspace 命令作用域

没有显式指定 monitor 的 workspace 操作作用于：

```text
selected_monitor
```

例如：

```text
workspace 3
```

表示：

> 切换 selected monitor 的 Workspace 3。

如果目标 workspace 有 `last_focused_client`，恢复它；否则：

```text
focused_client = None
```

selected monitor 本身保持不变。

---

## 6.4 Focus Mode 与 Pointer

### Click-to-focus

单纯把 pointer 移到另一块 monitor：

```text
selected_monitor 不自动改变
focused_client 不自动改变
```

只有显式 monitor navigation 或实际 focus 某个 client 时才改变 selected monitor。

### Sloppy

pointer 进入 managed client 时，该 client 立即获得 focus：

```text
pointer enters client
→ focus client
→ selected_monitor = focused_client.monitor
```

pointer 从 client 进入 root / desktop 空白区域时：

```text
focused_client 保持不变
selected_monitor 保持不变
```

V1 不提供独立的 `focus-follows-mouse` mode。

---

# 7. 新窗口出现

普通 managed client 的默认 destination：

```text
manage
→ selected_monitor.active_workspace
→ place in selected_monitor workarea
```

随后 initial-manage rule 可以覆盖：

```text
monitor
workspace
placement
focus_on_map
raise_on_map
```

默认仍为：

```text
focus_on_map = true
raise_on_map = true
```

若最终 destination workspace 当前可见：

```text
raise_on_map = true → raise in that workspace
focus_on_map = true → focus client
                      → selected_monitor = client.monitor
```

若最终 destination workspace 当前不可见：

```text
client remains hidden
focus_on_map = true → focus request suppressed
                     → do not switch workspace
                     → do not change selected_monitor
```

`raise_on_map` 仍可更新 hidden workspace 内部的 stacking order，但不会让 client 变得可见。

这里的 `map` 表示 client 被 WM initial-manage；它不保证最终 destination 当前处于可见 workspace。

---

## 7.1 Rule Override

Window Rule 可以覆盖两个行为：

```text
focus_on_map
raise_on_map
```

因此可以表达：

### 默认行为 A

```text
focus_on_map = true
raise_on_map = true
```

### 只显示到前面，但不抢键盘 B

```text
focus_on_map = false
raise_on_map = true
```

### 完全不打扰当前工作 C

```text
focus_on_map = false
raise_on_map = false
```

microbox 不尝试通过复杂 heuristic 判断：

> “这个窗口是不是用户主动 spawn 出来的？”

默认行为保持 deterministic，例外交给 explicit rule。

关键边界：rule 决定“窗口去哪”，`focus_on_map` 只决定“目标当前可见时是否 focus”；V1 不允许 rule 因为 focus 请求而隐式切换到 hidden workspace。

---

# 8. Focus Fallback

当 focused client：

- closed
- exits
- becomes unmanaged
- is moved away from current workspace

必须选择新的 focus。

FREE 与 MONOCLE 使用不同默认策略。

---

## 8.1 FREE Mode

默认：

```text
focus_fallback = MRU
```

MRU = Most Recently Used。

也就是在当前 workspace 中选择：

> 最近使用过、目前仍合法存在的 client。

---

## 8.2 MONOCLE Mode

默认：

```text
focus_fallback = TAB_NEIGHBOR
```

但可以配置成：

```text
MRU
```

### TAB_NEIGHBOR 规则

假设：

```text
A | B | C | D
        ^
```

关闭 C：

```text
A | B | D
        ^
```

规则：

1. 优先选择原位置右侧 tab
2. 若右侧不存在，选择左侧 tab
3. 若 workspace 已空，则 focus = None

这提供类似浏览器 tab 的连续操作体验。

---

# 9. 三种 Window Order 独立存在

microbox 明确区分：

```text
tab_order
mru_order
stacking_order
```

三者不能因为实现方便而共用。

---

## 9.1 Tab Order

表示稳定的视觉顺序。

特点：

- 新窗口默认追加
- focus 不重排
- raise / lower 不重排
- 未来允许手动调整

---

## 9.2 MRU Order

表示窗口使用历史。

主要用于：

- FREE focus fallback
- Alt-Tab
- 可选 MONOCLE fallback

普通确认 focus 会把对应 client 更新为最新 MRU。

Alt-Tab 类完整 MRU cycle 则必须使用独立 navigation session：

```text
cycle start
→ snapshot = current mru_order
→ cursor starts at current client

while initiating modifier is held
→ next/prev walks snapshot cyclically
→ focused_client may change
→ real mru_order is not reordered

cycle commit / modifier release
→ final focused client becomes newest real MRU
→ discard snapshot
```

这样 `A → B → C → D` 可以在同一轮中完整遍历，不会因为中间 focus B 后立刻重排成 `B → A ...` 而退化成 A/B toggle。

没有 cycle-capable input context 的 standalone MRU command，可视为“一步 snapshot + 立即 commit”。

---

## 9.3 Stacking Order

表示 X11 Z-order。

主要决定：

```text
谁覆盖在谁上面
```

raise / lower 修改 stacking order，但不改变 tab order。

---

# 10. Move-to-Workspace

假设：

```text
current = A:WS1

Firefox ← focused
Emacs
```

执行：

```text
move Firefox to WS2
```

默认语义：

> **只移动对象，不移动用户视角。**

结果：

```text
A:WS1
Emacs ← focus fallback

A:WS2
Firefox
```

当前仍然是 WS1。

---

## 10.1 Follow Variant

提供显式：

```text
window move-workspace 2 --follow
```

结果：

```text
selected monitor current workspace = WS2
Firefox ← focused
```

统一原则：

```text
move ...
→ move object only

move ... --follow
→ move object + user perspective
```

---

# 11. Move-to-Monitor

默认将 client 移入：

> 目标 monitor 当前正在显示的 workspace。

例如：

```text
A current = WS2
B current = WS4

Firefox @ A:WS2
```

执行（概念目标为 B；public command 通过 next/prev 选择目标）：

```text
window move-monitor next
```

结果：

```text
Firefox @ B:WS4
```

---

## 11.1 默认不 Follow

默认：

```text
window move-monitor next
```

只把 client 送过去。

原 workspace 使用自己的 focus fallback。

用户仍然留在原 monitor。

---

## 11.2 Follow Variant

```text
window move-monitor next --follow
```

则：

```text
Firefox → B current workspace
Firefox remains focused
```

因此：

```text
selected_monitor = B
focused_client = Firefox
```

---

## 11.3 Keep Workspace Variant

另有独立语义：

```text
window move-monitor next --keep-workspace
```

例如：

```text
Firefox @ A:WS2
→ Firefox @ B:WS2
```

而不是进入 B 当前正在显示的 WS4。

V1 允许与 `--follow` 组合；公开 spelling 以 `microbox_v1_command_vocabulary_and_default_config.md` 为准。

---

## 11.4 Workspace Visibility Invariant

对每个 monitor：

```text
active_workspace
→ 普通 clients 可参与该 monitor 的可见 presentation

inactive workspaces
→ 普通 clients 不可见
→ 不可获得正常 keyboard focus
```

这是用户语义 invariant。底层实现可以选择 unmap 或其他 X11 机制，但不能让 inactive workspace client 泄漏到屏幕或成为正常 focus target。

---

# 12. FREE Mode

FREE 是普通 stacking mode。

状态语义：

```text
each client:
    own geometry
    own stacking position
```

窗口允许：

- overlap
- raise
- lower
- move
- resize

该 monitor 的 active workspace 中，普通 client 按 FREE stacking semantics 呈现；是否底层保持 mapped 由实现决定，但用户必须能看到其正常 stacking 结果。

---

# 13. MONOCLE Mode

MONOCLE 是独立 Workspace Mode。

进入 MONOCLE：

```text
workspace.mode = MONOCLE
```

所有 client：

- 保持 mapped
- 使用 monocle geometry
- 不通过 unmap 隐藏非 active client

Active Client：

```text
raise → top of stack
```

因此概念上：

```text
Firefox   mapped
kitty     mapped
Emacs     mapped + top
```

用户视觉上只看到 Emacs。

---

## 13.1 Tab Switch

在 MONOCLE 下切换 tab，本质主要是：

```text
new active client
→ focus
→ raise
```

不进行频繁 map/unmap。

---

## 13.2 MONOCLE Geometry Operations

MONOCLE 是 workspace presentation mode，不是隐藏修改 FREE geometry 的入口。

因此当前 workspace 为 MONOCLE 时：

```text
manual move
manual resize
snap ...
maximize / maximize toggle
→ no-op
```

这些操作：

- 不修改 `geometry`
- 不修改 `normal_geometry`
- 不修改 `snap_state`
- 不修改 `maximized`
- 不自动退出 MONOCLE

`fullscreen` 是例外：它属于更高一层 temporary override，可在 MONOCLE 中正常进入；退出 fullscreen 后回到原 MONOCLE presentation。

---

## 13.3 FREE ↔ MONOCLE 无损

进入 MONOCLE 不能破坏原本 FREE 状态。

必须满足：

```text
FREE
→ MONOCLE
→ FREE
```

后：

- position 恢复
- size 恢复
- normal layout 不丢失

MONOCLE 不应覆盖正常 geometry 真相。

---

# 14. Geometry State

Client 至少存在：

```text
geometry
normal_geometry
snap_state
maximized
```

其中：

### normal_geometry

表示用户正常自由窗口状态下应恢复到的唯一长期 geometry truth。

### geometry

表示当前实际 geometry。

### snap_state

表示：

```text
none
left
right
top-left
top-right
bottom-left
bottom-right
```

### maximized

表示当前是否使用 monitor workarea 的 maximize presentation。

V1 约束：

```text
maximized == true
→ snap_state == none
```

`snap` 与 `maximize` 都从同一个 `normal_geometry` 派生，不形成层层嵌套的 geometry restore stack。

---

# 15. Snap State

Snap 是状态化快捷 placement，但不是持续 layout constraint。

进入 snap：

```text
normal_geometry = previous normal geometry
snap_state = target
geometry = snapped geometry
```

---

## 15.1 Manual Move / Resize

任何手动：

```text
move
resize
```

立即：

```text
snap_state = none
```

并使当前 geometry 成为新的 normal geometry。

Snap 不继续约束窗口。

---

## 15.2 Maximize Restore

进入 maximize：

```text
normal/snapped
→ snap_state = none
→ maximized = true
→ geometry = monitor workarea
```

退出 maximize：

```text
maximized
→ maximized = false
→ snap_state = none
→ geometry = normal_geometry
```

即使 maximize 前是 snapped，也**不恢复 snap**；统一恢复唯一 `normal_geometry`。

---

# 16. Fullscreen State

Fullscreen 是 Window State。

不是 Workspace Mode。

因此：

```text
workspace.mode = MONOCLE
client fullscreen
exit fullscreen
```

之后：

```text
workspace.mode == MONOCLE
```

仍保持。

---

## 16.1 Presentation State 不覆盖 Normal Layout

系统必须支持：

```text
FREE geometry
    ↓
MONOCLE
    ↓
fullscreen
    ↓
exit fullscreen
    ↓
MONOCLE
    ↓
exit MONOCLE
    ↓
original FREE geometry
```

正常布局必须完整恢复。

---

# 17. Client Fullscreen Policy

Client fullscreen request 可根据规则得到：

```text
allow
fake
deny
```

### allow

```text
fullscreen semantic = true
geometry = actual monitor fullscreen
```

### fake

```text
client fullscreen presentation = true
WM geometry unchanged
```

### deny

```text
fullscreen request rejected
geometry unchanged
```

用户主动执行的 WM fullscreen command 不受 client policy 限制。

---

## 17.1 V1 Minimal Stacking Precedence

V1 不引入完整 layer abstraction，但用户可见的最小顺序必须固定（低 → 高）：

```text
Desktop
Normal clients
MONOCLE Tab Bar / Dock
Fullscreen client
Snap preview / necessary WM overlay
```

V1 不存在 public `always_on_top` state/action。

---

# 18. “移动”行为统一原则

整个 microbox 采用：

> **对象操作默认只修改对象，不偷偷修改用户当前视角。**

例如：

```text
window move-workspace ...
window move-monitor ...
```

均只移动 client。

只有显式：

```text
--follow
```

才同时改变用户视角相关状态，例如：

- selected monitor
- selected monitor 的 current workspace
- focused client

这一原则后续应延续到 Command System。

---


# 19. Workspace Derived / Presentation State

Workspace 除了 authoritative state 外，还需要向 WM UI、未来 IPC、bar 等消费者暴露一组稳定可推导的 presentation state：

```text
active
selected
occupied
urgent
```

这些不是为了某个 bar 临时增加的 UI flag，而是由 WM core 的真实状态推导出的 workspace facts。

---

## 19.1 Active

```text
active =
    workspace == workspace.monitor.active_workspace
```

因此每个 monitor 都恰好有一个 active workspace。

多 monitor 情况下可以同时存在多个 active workspace：

```text
Monitor A → A:WS2 active
Monitor B → B:WS4 active
```

这与单一 global `_NET_CURRENT_DESKTOP` 模型不同。

---

## 19.2 Selected

```text
selected =
    workspace.monitor == selected_monitor
    &&
    workspace == selected_monitor.active_workspace
```

所以：

```text
active != selected
```

是合法且常见的。

例如：

```text
Monitor A: WS2 active
Monitor B: WS4 active

selected_monitor = Monitor B
```

则：

```text
A:WS2 → active=true, selected=false
B:WS4 → active=true, selected=true
```

`selected` 表示当前用户操作目标 monitor 的 active workspace。

---

## 19.3 Occupied

```text
occupied =
    workspace.clients.length > 0
```

不额外维护一个可独立修改的：

```text
workspace.occupied
```

原则：

> 能从 authoritative state 推导出的 presentation state，不复制保存。

这样 client move / unmanage / destroy 后不会产生重复状态不同步。

---

## 19.4 Urgent

Workspace urgency 从 clients 推导：

```text
urgent =
    any(client.urgent for client in workspace.clients)
```

其中：

```text
Client.urgent
```

来自 ICCCM `WM_HINTS` urgency flag。

当某个 client 真正获得 keyboard focus 时：

```text
client.urgent = false
```

然后 workspace urgency 自然重新推导。

不能仅因为 workspace 被切换为 active，就无条件清空整个 workspace 的 urgency。

原因是 FREE 模式下 urgent client 仍可能被其他窗口遮挡；真正 focus 到对应 client 才表示用户已经处理它。

---

## 19.5 Derived-State Principle

概念上可以向消费者暴露：

```text
WorkspaceViewState
├── active
├── selected
├── occupied
└── urgent
```

但它是 read model / derived view，不是另一套 authoritative mutable state。

未来：

```text
microbox-bar
Polybar adapter
IPC query / subscribe
```

只能消费这些事实，不应反过来拥有或定义 workspace state。

---

# 20. Core Invariants

以下为 Step 2 正式锁定的系统 invariant。

## I1 — Client Unique Ownership

```text
每个 managed client 恰好属于一个 workspace
```

## I2 — Workspace Unique Ownership

```text
每个 workspace 恰好属于一个 monitor
```

## I3 — One Active Workspace Per Monitor

```text
每个 monitor 同时只有一个 active workspace
```

## I4 — Workspace Owns Its History

每个 workspace 自己保存：

```text
mode
clients
tab_order
mru_order
last_focused_client
```

切换 workspace 不破坏这些状态。

## I5 — Presentation State Cannot Destroy Normal Geometry

```text
MONOCLE
fullscreen
snap presentation
```

均不能意外覆盖用户正常布局所需的恢复状态。

## I6 — Single Global Keyboard Focus

```text
整个 WM 同时最多只有一个真正 focused client
```

## I7 — Selected Monitor Is First-Class State

```text
selected_monitor
```

是独立的一等状态。

必须满足：

```text
focused_client != None
→ focused_client.monitor == selected_monitor
```

但允许：

```text
selected_monitor != None
focused_client == None
```

因此空 monitor / 空 workspace 仍然可以成为当前操作目标。

## I8 — Ordering Domains Are Independent

```text
tab_order != mru_order != stacking_order
```

三个概念不可混用。

## I9 — Move Does Not Imply Follow

```text
move
```

默认仅改变对象归属。

```text
move --follow
```

才改变用户视角。

## I10 — Inactive Workspace Clients Are Not Focusable

```text
workspace != workspace.monitor.active_workspace
→ ordinary client is not visible
→ ordinary client cannot hold normal keyboard focus
```

## I11 — MONOCLE Does Not Mutate FREE Geometry

MONOCLE 下 move / resize / snap / maximize 为 no-op；fullscreen 仍可作为 temporary override。

## I12 — Maximize Restores Normal Geometry

```text
maximized → unmaximize
→ geometry = normal_geometry
→ snap_state = none
```

## I13 — Hidden Rule Destination Does Not Pull View

rule 将 client 放到 hidden workspace 时，`focus_on_map=true` 不切换 workspace、不 focus hidden client。

## I14 — MRU Cycle Uses Snapshot

一轮 MRU navigation 内不因中间 focus 重排 snapshot；结束时才提交最终 MRU。

---


## I15 — Workspace Presentation State Is Derived

```text
active
selected
occupied
urgent
```

必须由 authoritative WM state 推导。

不得维护另一套可独立修改、可能与真实 client / monitor state 失同步的 workspace presentation flags。

# 21. Step 2 完成后的语义模型

最终可以压缩为：

```text
WM
│
├── Selected Monitor
├── Global Focus
│
├── Monitor A
│   ├── active workspace
│   ├── WS1
│   ├── WS2
│   └── WS3
│
├── Monitor B
│   ├── active workspace
│   ├── WS1
│   ├── WS2
│   └── WS3
│
└── Clients
    ├── exactly one workspace
    ├── geometry / normal_geometry
    ├── snap / maximized state
    ├── fullscreen state
    └── urgent state
```

Workspace：

```text
Workspace
├── mode: FREE | MONOCLE
├── clients
├── last focus
├── tab order
├── MRU order
├── stacking relationship
└── derived view
    ├── active
    ├── selected
    ├── occupied
    └── urgent
```

其中：

```text
FREE
→ spatial stacking semantics

MONOCLE
→ mapped clients + common monocle geometry + active client raised
```

---

# 22. Step 2 非目标

本阶段不决定：

- C / Rust / C++
- Xlib / XCB
- event loop 具体实现
- XRandR API 使用方式
- EWMH / ICCCM 精确 atom subset
- config parser library
- Unix socket protocol
- tabbar 如何创建 X window
- mouse binding 具体键位
- snap detection threshold
- monitor hotplug 实现
- test framework

以上进入 Step 3–5。

---

# 23. Step 2 状态

```text
Step 1 — Product Definition
DONE

Step 2 — Semantic / State Contract
DONE

Step 3 — Technical Architecture Contract
NEXT

Step 4 — Interaction Contract

Step 5 — Acceptance Contract

Step 6 — Goal-driven Agent Development
```

Step 2 完成后，后续实现不得在未经显式重新决策的情况下改变本文定义的状态语义与 invariant。
