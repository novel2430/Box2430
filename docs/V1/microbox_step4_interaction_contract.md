# Microbox — Step 4 Interaction Contract

**Document Version: V2**

> 本文冻结 microbox 的用户交互语义。
> Step 1 定义产品方向，Step 2 定义状态语义，Step 3 定义技术架构；Step 4 负责定义用户实际如何操作窗口、monitor、workspace、MONOCLE 与 snapping。
>
> 本文锁定的是“动作的含义”，不是最终不可更改的快捷键布局。所有默认键位与鼠标绑定都必须可以被用户覆盖或取消。

---

# 1. Step 4 目标

Step 4 负责回答：

> **用户每天实际怎么操作 microbox？**

本阶段冻结：

- 鼠标 move / resize
- 跨 monitor 拖动语义
- snapping 的鼠标与键盘交互
- MONOCLE Tab Bar 的鼠标交互
- Tab Order 与 MRU 的键盘导航
- selected monitor 的显式导航
- pointer warp
- workspace / monitor command 的作用域
- raise / lower / maximize / fullscreen 的关系
- 默认 keymap 的覆盖原则

## 1.1 Focus Policy

V1 只提供：

```text
click
sloppy
```

默认：

```text
click
```

`click`：pointer enter 不改变 focus；点击 client 才 focus。

`sloppy`：pointer enter client 立即 focus；pointer 进入 root / desktop 空白区域时保留原 focus。

V1 不提供独立 `focus-follows-mouse`。

---

# 2. Mouse Move / Resize：显式 dwm 风格

microbox 使用明确的 modifier-driven mouse operations。

默认语义：

```text
Mod + Left Mouse Drag
→ MOVE

Mod + Right Mouse Drag
→ RESIZE
```

不根据 pointer 在窗口中的位置猜测用户意图。

明确不做：

- implicit edge hit-test
- corner hit-test
- 无 modifier 的 border resize
- invisible resize zone
- 根据 pointer 所在区域选择 resize direction

原则：

> 用户明确调用 move 或 resize；WM 不猜。

---

# 3. Resize：dwm-style Bottom-Right + Pointer Warp

`Mod + Right Mouse` 使用接近 dwm 的行为：

```text
Mod + Right Press
→ pointer warp 到窗口右下角
→ 从 bottom-right resize
```

resize 不根据初始 pointer 位置决定边或角。

因此 V1 只需要一种明确 resize gesture：

```text
bottom-right resize
```

未来可以增加其他 resize commands，但不能改变这一默认交互的确定性。

---

# 4. Mouse Drag 跨 Monitor

Client 使用：

```text
Mod + Left Drag
```

跨 monitor 移动时：

> 拖动过程中不改变 client ownership。

只有 mouse release 时才决定新的 monitor ownership。

---

## 4.1 Monitor 判定方式

释放鼠标时：

```text
window center
```

落在哪个 monitor，就认为 client 被移动到哪个 monitor。

不使用：

- pointer 所在 monitor
- window overlap 面积百分比
- 拖动过程中的实时 monitor switching

---

## 4.2 Ownership 变化

若 window center 最终落到 Monitor B：

```text
client
→ Monitor B 当前 active workspace
```

这与 Step 2 / Command Contract 的普通 `window move-monitor ...` 默认目标一致。

---

## 4.3 Focus

被直接拖动的 client 在整个操作中保持 focus。

因此从 A 拖到 B 并松手后：

```text
selected_monitor = B
focused_client = dragged client
```

这是 direct manipulation 的自然结果，不视为隐式 `--follow` command。

---

# 5. Mouse Snapping

Snapping 是 microbox 的核心 mouse-friendly 能力。

鼠标 snapping 使用：

> **Edge Hover Preview**

---

## 5.1 Edge Zone

当拖动窗口时，pointer 进入 monitor 的 snap edge zone：

```text
→ preview 立即出现
```

V1 不需要 hover delay。

edge zone 是一个小范围，而不是必须精确命中 1px 屏幕边缘。

具体像素阈值由默认配置 / Step 5 acceptance 调整，不在本文锁死。

---

## 5.2 Preview

Preview 使用简单：

```text
outline rectangle
```

表示：

> 松开鼠标后窗口会落到哪里。

不要求：

- compositor
- 半透明整面填充
- blur
- animation

Preview 可以只画实体色边框矩形。

---

## 5.3 Snap Commit

进入 edge zone：

```text
只显示 preview
```

真正 snap 只发生在：

```text
mouse release
```

release 前 client 仍保持正常 drag geometry。

V1 mouse target mapping：

```text
left edge       → left
right edge      → right
top-left        → top-left
top-right       → top-right
bottom-left     → bottom-left
bottom-right    → bottom-right
top edge        → maximize
bottom center   → no snap
```

V1 不提供独立 top-half / bottom-half snapping。

---

# 6. Keyboard Snapping

Snapping 必须同时提供显式 command。

至少包含：

```text
snap left
snap right

snap top-left
snap top-right
snap bottom-left
snap bottom-right

snap maximize
snap none
```

具体 keybinding 不在 semantic contract 中锁死。

用户可以将这些 command 任意绑定。

---

# 7. Tab Bar Mouse Interaction

MONOCLE Tab Bar 默认支持：

```text
Left Click
→ focus + raise target client

Middle Click
→ close target client

Wheel Up
→ previous tab

Wheel Down
→ next tab

Right Click
→ no-op
```

---

## 7.1 Wheel Navigation

Tab wheel navigation：

```text
cyclic
```

例如：

```text
A | B | C
        ^
```

继续 `Wheel Down`：

```text
→ A
```

---

## 7.2 Right Click

V1 不实现 Tab Bar context menu。

原因：

- 不需要 menu framework
- 相关操作已经可以通过 command system 提供
- 避免为一个低价值交互增加 UI complexity

---

# 8. Keyboard Window Navigation

microbox 明确保留两套不同窗口顺序：

```text
tab_order
mru_order
```

因此键盘也提供两套导航语义。

---

## 8.1 Tab Order Navigation

提供：

```text
focus next-tab
focus prev-tab
```

特点：

- 使用稳定 `tab_order`
- 循环
- MONOCLE 下作为主要 previous/next window navigation
- 与 Tab Bar wheel 行为一致

因此：

```text
Tab Bar visual order
Tab wheel
next-tab / prev-tab
```

使用同一套顺序。

---

## 8.2 MRU Navigation

提供：

```text
focus next-mru
focus prev-mru
```

使用：

```text
mru_order
```

典型：

```text
Alt+Tab
```

应走 MRU，而不是 tab order。

V1 的 Alt-Tab 是完整 cycle，不是 recent-two toggle。交互 session：

```text
Alt down + first Tab
→ freeze current mru_order snapshot

Alt held + repeated Tab
→ cyclically walk snapshot
→ intermediate focus 不重排真实 MRU

Alt release
→ end cycle
→ final focused client becomes newest MRU
```

因此每次完整松开 Alt 都结束当前 cycle；下一次 Alt+Tab 会基于新的 MRU 重新开始。

具体是否用 Alt+Tab 是默认 keymap 问题，可配置。

---

# 9. selected_monitor：一等交互对象

V1 冻结：

> Monitor 本身必须可以被用户显式选择，即使该 monitor 当前 workspace 没有任何 client。

因此状态模型使用：

```text
selected_monitor
```

作为一等状态。

允许：

```text
selected_monitor = Monitor B
focused_client = None
```

---

# 10. Monitor Navigation

提供：

```text
monitor next
monitor prev
```

navigation 在当前 monitors 之间：

```text
cyclic
```

---

## 10.1 Empty Monitor Is Valid

即使目标 monitor 当前 workspace 为空：

```text
monitor next
```

仍然必须成功。

不能因为没有可 focus client 就 no-op。

---

## 10.2 Pointer Warp

切换 selected monitor 时：

```text
pointer warp → target monitor center
```

不保持 relative coordinates。

原因：

- deterministic
- 简单
- 对不同 resolution / geometry 都稳定
- 给用户明确视觉反馈

---

## 10.3 Focus Restore

切到目标 monitor 后：

如果其当前 workspace 有合法：

```text
last_focused_client
```

则恢复它：

```text
focused_client = last_focused_client
```

如果目标 workspace 为空：

```text
focused_client = None
```

但：

```text
selected_monitor = target
```

保持成立。

---

# 11. Workspace Commands 的作用域

没有显式指定 monitor 的 workspace command：

```text
workspace N
workspace next
workspace prev
```

全部作用于：

```text
selected_monitor
```

而不是：

```text
focused_client.monitor
pointer_monitor
global desktop
```

---

## 11.1 Workspace Navigation

`workspace next/prev`：

```text
cyclic
```

例如：

```text
1 2 3 4
      ^
```

执行：

```text
workspace next
→ 1
```

---

## 11.2 Workspace Focus Restore

切换 selected monitor 的 workspace 后：

- 有 `last_focused_client` → 恢复
- workspace 为空 → `focused_client = None`
- `selected_monitor` 不变

---

# 12. Move Commands

继续沿用 Step 2 的统一原则：

```text
move
→ 只移动对象

move --follow
→ 移动对象 + 用户视角跟随
```

至少应存在：

```text
window move-workspace N
window move-workspace N --follow

window move-monitor next
window move-monitor next --follow
window move-monitor next --keep-workspace
```

公开 command spelling 已由 V1 Command Vocabulary 冻结，实现不得自行改名。

## 12.1 MONOCLE Geometry Command Policy

当前 selected workspace 为 MONOCLE 时：

```text
mouse move-window
mouse resize-window
snap ...
maximize
maximize toggle
→ no-op
```

这些操作不会暗中改变 FREE geometry，也不会自动退出 MONOCLE。

`fullscreen` 是例外，可正常进入；退出 fullscreen 后回到原 MONOCLE。

---

# 13. Raise / Lower

`raise`：

```text
只修改 stacking order
geometry 不变
```

`lower`：

```text
只修改 stacking order
geometry 不变
```

明确：

> focus != raise

focus 后是否自动 raise 继续由：

```text
raise_on_focus
```

配置决定。

---

# 14. Maximize

microbox 提供正式：

```text
maximize
maximize toggle
```

语义：

```text
占满 selected/current client 所在 monitor 的 workarea
```

workarea 指：

```text
monitor rectangle - dock/panel strut
```

---

## 14.1 `snap maximize`

```text
snap maximize
```

与：

```text
maximize
```

属于同一状态迁移。

它可以理解为：

> maximize 的 command alias / snapping vocabulary 入口。

不维护第二套 maximize state。

---

## 14.2 Maximize State

maximize 属于 geometry base state。

概念：

```text
normal
snapped
maximized
```

恢复真相只有：

```text
normal_geometry
```

因此：

```text
normal → maximize → unmaximize → normal_geometry
snapped → maximize → unmaximize → normal_geometry
```

即 maximize 前即使是 snapped，unmaximize 也不回 snap。

---

# 15. Fullscreen

Fullscreen 是高于：

```text
normal / snapped / maximized
```

的临时 override。

例如：

```text
normal
→ maximize
→ fullscreen
→ exit fullscreen
→ maximize
→ unmaximize
→ normal
```

因此 fullscreen：

> 不破坏 maximize / snap / normal 的恢复状态。

---

## 15.1 Client vs User Fullscreen

继续沿用 Step 1/2：

Client request：

```text
allow
fake
deny
```

User WM command：

```text
fullscreen toggle
```

始终是真 fullscreen。

## 15.2 V1 Minimal Stacking Precedence

用户可见顺序固定为（低 → 高）：

```text
Desktop
Normal clients
MONOCLE Tab Bar / Dock
Fullscreen client
Snap preview / necessary WM overlay
```

所以 fullscreen 会覆盖 Tab Bar 与 panel；必要的短暂 WM overlay 仍可显示在 fullscreen 上。

V1 不提供 `always_on_top`。

---

# 16. Default Keymap Philosophy

microbox 必须有一套：

> **Minimal Safe Defaults**

目的只是保证：

```text
首次启动
配置文件缺失
配置损坏后的 fallback
```

仍然可以基本操作 WM。

配置读取采用 whole-config atomic semantics：

```text
parse whole config
→ validate whole config
→ success: apply entire config
→ failure: report explicit error
           discard entire user config
           start with built-in safe defaults
```

禁止出现“前半份 user config 已生效、后半份失败”的 partial apply。

---

## 16.1 建议的最小默认范围

至少可以默认提供：

```text
Mod + Return
→ spawn terminal

Mod + q
→ close

Mod + 1..N
→ workspace N

Mod + Shift + 1..N
→ move window to workspace N

Mod + Left Mouse
→ move

Mod + Right Mouse
→ resize
```

具体 terminal command、modifier 与数量可以由默认配置决定。

---

## 16.2 Example Config

完整能力，例如：

```text
monocle
snap
monitor navigation
tab navigation
MRU navigation
maximize
fullscreen
raise/lower
```

应在默认/example TOML 中给出推荐 keybindings。

但这些推荐键位不是产品 invariant。

---

# 17. All Bindings Must Be Overrideable

这是 Step 4 的核心配置原则。

所有默认：

```text
keyboard binding
mouse binding
```

都必须支持：

```text
override
rebind
unbind
```

不存在不可更改的 sacred binding。

例如：

```text
Mod+q
```

即使默认是：

```text
close
```

用户也必须能够改成：

```text
spawn something
```

或完全删除。

---

## 17.1 Defaults Are Not Privileged

加载语义应等价于：

```text
built-in fallback bindings
        ↓
user config
        ↓
override / remove defaults
        ↓
final binding table
```

不能出现：

```text
built-in binding
+
user binding
```

同时触发两个 action 的情况。

原则：

> **defaults are policy, not privilege.**

---

# 18. Step 4 Interaction Invariants

以下为正式冻结的交互 invariant。

## I1 — Explicit Mouse Operations

Move / resize 必须由用户显式 modifier gesture 调用。

WM 不通过 pointer hit-test 猜测操作。

---

## I2 — dwm-style Resize

默认 resize：

```text
Mod + Right Mouse
→ pointer warp bottom-right
→ bottom-right resize
```

---

## I3 — Drag Ownership Changes on Release

跨 monitor drag：

```text
拖动过程不改 ownership
release 时按 window center 判定 monitor
```

---

## I4 — Snap Uses Preview Then Commit

Mouse snap：

```text
enter edge zone
→ outline preview

release
→ actual snap
```

---

## I5 — Keyboard Snap Is First-Class

所有核心 snap target 必须有显式 command，不依赖鼠标。

---

## I6 — Tab and MRU Navigation Are Separate

```text
next-tab / prev-tab
```

走 tab order。

```text
next-mru / prev-mru
```

走 MRU。

---

## I7 — selected_monitor Is User-Selectable

即使 monitor 当前 workspace 为空，也必须可以成为 selected monitor。

---

## I8 — Monitor Navigation Warps Pointer

显式 monitor navigation：

```text
pointer → target monitor center
```

---

## I9 — Workspace Commands Target selected_monitor

无显式 monitor 参数的 workspace command，只作用于 selected monitor。

---

## I10 — Raise/Lower Do Not Change Geometry

Z-order 与 geometry operation 保持分离。

---

## I11 — Maximize Is One Geometry State

`maximize` 与 `snap maximize` 共享同一状态。

---

## I12 — Fullscreen Is Temporary Override

fullscreen 不破坏 normal / snap / maximize 的恢复链。

## I13 — MRU Cycle Is Snapshot-Based

一轮 Alt-Tab 在 modifier release 前使用冻结 MRU snapshot；结束后才把最终窗口提交为 newest MRU。

## I14 — MONOCLE Geometry Commands Are No-op

MONOCLE 下 move / resize / snap / maximize 不改变 FREE geometry；fullscreen 例外。

## I15 — Maximize Returns to Normal Geometry

unmaximize 始终恢复 `normal_geometry`，不恢复先前 snap。

## I16 — V1 Stacking Has Minimal Fixed Precedence

```text
Desktop < Normal < TabBar/Dock < Fullscreen < WM overlay
```

V1 不暴露 `always_on_top`。

---

## I17 — All Bindings Are Replaceable

不存在用户无法覆盖、解绑的 keyboard/mouse binding。

---

# 19. Step 4 对 Step 2 的修订

早期 Step 2 曾使用推导式 active-monitor 模型：

```text
active_monitor = focused_client.monitor
```

它不足以表达：

```text
用户选择一个空 monitor
```

因此 Step 2 已正式修订为：

```text
selected_monitor
focused_client
```

两个相关但独立状态。

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

此修订已经同步写回：

```text
microbox_step2_semantic_state_contract.md
```

---

# 20. Step 4 非目标

本阶段不锁死：

- 默认 modifier 最终一定是 Super / Alt
- terminal program
- edge zone 精确像素
- preview border 精确颜色/宽度
- Tab Bar 高度
- snap target 精确比例扩展
- 默认完整 keymap
- mouse cursor theme
- animation
- compositor behavior
- IPC wire protocol
- test automation implementation

这些可以由默认配置、Step 5 Acceptance Contract 或后续版本决定。

---

# 21. Step 4 冻结后的交互模型

```text
Mouse
├── Mod + LMB → move
├── Mod + RMB → dwm-style resize
├── cross-monitor release → center-based ownership
└── edge zone → outline snap preview

Keyboard / Commands
├── workspace N / next / prev
├── monitor next / prev
├── move-workspace / move-monitor
├── next-tab / prev-tab
├── next-mru / prev-mru
├── snap ...
├── maximize
├── fullscreen
├── raise / lower
└── close

Monitor
├── selected_monitor
├── pointer warp on explicit navigation
└── empty monitor can still be selected

Tab Bar
├── left click → select
├── middle click → close
├── wheel → cyclic tab navigation
└── right click → no-op

Bindings
├── minimal safe defaults
├── example configuration
└── everything overrideable / unbindable
```

---

# 22. Step 4 状态

```text
Step 1 — Product Definition
DONE

Step 2 — Semantic / State Contract
DONE
(revised by Step 4 selected_monitor decision)

Step 3 — Technical Architecture Contract
DONE

Step 4 — Interaction Contract
DONE

Step 5 — Acceptance Contract
NEXT

Step 6 — Goal-driven Agent Development
```

Step 4 完成后，未经显式重新决策，不得：

- 将 move/resize 改成 implicit hit-test interaction
- 在 drag 过程中实时迁移 client monitor ownership
- 删除 keyboard snap command surface
- 混用 tab order 与 MRU
- 取消空 monitor 的显式选择能力
- 将 maximize 与 snap-maximize 实现为互不兼容的两套用户状态
- 让 fullscreen 覆盖并丢失原 geometry state
- 引入用户无法 override/unbind 的硬编码快捷键
