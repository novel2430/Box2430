# Box2430 — 顶层需求与产品定义（V1 Frozen）

> 项目正式名为 **Box2430**；程序、配置目录与 companion tool 使用小写 `box2430` 命名。
> 本文冻结 V1 顶层产品定位、窗口管理语义与架构约束，不进入具体实现细节。
> 细节语义以 Step 2 / Step 4 / V1 Command & Config Contract 为准；这些文档不得反向改变本文的产品边界。

---

## 1. 项目定位

box2430 是一个面向 X11 的轻量级 stacking window manager。

核心目标不是重新实现一个完整 Openbox，也不是开发另一个 tiling WM，而是：

> 保留 Openbox 式自由堆叠窗口的直觉体验，加入更符合个人使用习惯的 workspace、monocle、tab、snapping 与规则系统，同时删除不需要的复杂度。

整体设计参考：

- Openbox：stacking-first、自由窗口管理
- dwm：极简实现、monocle、强 WM 控制权
- i3：tabbed 模式的窗口可见性反馈
- bspwm / river：未来 IPC 与 script-init 配置理念

box2430 应保持：

- 小
- 可预测
- 用户主导
- 机制简单
- 不替 client 做过多猜测

---

# 2. 非目标

box2430 **不是桌面环境**。

不计划承担：

- compositor
- notification daemon
- application launcher
- wallpaper manager
- panel
- system tray
- session manager
- 完整主题系统

这些职责交给：

- picom
- dunst
- rofi
- polybar
- feh
- 其他独立工具

同时，box2430 不追求：

- 完整 tiling layout system
- BSP / layout tree
- dwm tag system
- 完整 SSD/titlebar/theme framework
- 高度智能化的窗口 placement
- 完整覆盖所有 EWMH 边缘行为

---

# 3. 核心设计原则

## 3.1 Stacking First

普通 workspace 默认是自由堆叠桌面。

窗口拥有自己的：

- position
- size
- stacking order

窗口允许：

- overlap
- mouse move
- mouse resize
- raise / lower
- snapping

box2430 不使用 layout tree 管理普通窗口。

---

## 3.2 User Owns Geometry

窗口 geometry 的最终控制权属于 WM / 用户，而不是 client。

普通 managed window 默认：

- 忽略 client 请求的初始 position
- 由 WM 决定初始 placement
- 不主动补偿 client CSD 的透明边缘、shadow 或 visual extents

原则：

> WM 只管理真实 X11 window geometry，不猜测 client 内部视觉内容的位置。

---

# 4. Monitor 与 Workspace 模型

每个 monitor 拥有自己独立的一组 workspace。

概念模型：

```text
Monitor A
├── Workspace 1
├── Workspace 2
├── Workspace 3
└── Workspace 4

Monitor B
├── Workspace 1
├── Workspace 2
├── Workspace 3
└── Workspace 4
```

因此：

```text
Monitor A → Workspace 2
Monitor B → Workspace 4
```

是完全合法且常见的状态。

在 A 切换 workspace：

```text
A: 2 → 3
```

不会影响 B。

这与 Openbox 的共享 workspace 模型不同，也不采用 dwm tag 模型。

用户心智模型保持为传统：

```text
Workspace 1
Workspace 2
Workspace 3
...
```

但这些 workspace 是 **monitor-local** 的。

---

# 5. Workspace Mode

每个 workspace 有一个独立 mode：

```text
FREE
MONOCLE
```

## FREE

标准 stacking desktop。

所有窗口保持自己的位置、大小与 stacking order。

## MONOCLE

当前 workspace 的窗口以单窗口方式显示。

同一时间只显示当前 active window。

进入 MONOCLE：

- 不破坏原有 geometry
- 不修改 FREE 模式下的窗口布局

退出 MONOCLE：

- 所有窗口恢复原本 position / size / stacking state

因此 FREE ↔ MONOCLE 必须是无损切换。

---

# 6. Monocle Tab Bar

MONOCLE 模式提供顶部 Tab Bar。

例如：

```text
[ Firefox ][ Emacs ][ kitty ][ mpv ]
```

其作用是：

- 显示当前 workspace 的窗口
- 显示 active window
- 快速切换窗口

Tab Bar 属于 **Workspace UI**，不是 Window Decoration。

它与 client 是否使用 CSD 完全无关。

## Tab 顺序

Tab 顺序采用稳定顺序：

- 新窗口默认追加到末尾
- focus 改变不重新排序
- raise / lower 不重新排序

三种顺序必须独立：

```text
tab order
MRU order
stacking order
```

未来可以支持手动重新排列 tab。

---

# 7. Focus 模型

V1 只支持两种明确 policy：

```text
click
sloppy
```

默认：

```text
click
```

语义：

```text
click
→ pointer enter 不改变 focus
→ 点击 client 才 focus

sloppy
→ pointer enter client 立即 focus
→ pointer 进入 root / desktop 空白区域时保留原 focus
```

V1 **不提供 `focus-follows-mouse`**；避免暴露与 sloppy 高度重叠、但边界又不同的第三种模式。

`raise_on_focus` 是独立设置，不与 focus policy 强绑定。

例如：

```toml
focus_mode = "sloppy"
raise_on_focus = false
```

---

# 8. 新窗口 Placement

普通 managed window 默认使用 strict WM placement。

默认 destination：

```text
new normal window
→ selected_monitor
→ selected_monitor.active_workspace
→ selected_monitor workarea center
```

Window Rule 可以在 initial-manage 时覆盖 monitor / workspace destination。

默认忽略 client 请求的初始 position。

Dialog / transient 也遵守相同的简单心智模型：

```text
→ selected_monitor.active_workspace
→ selected_monitor workarea center
```

不因为 parent geometry 改变 placement。

特殊窗口，例如：

- launcher
- notification
- override-redirect window
- 某些特殊 EWMH types

不强制套用普通窗口 placement。

未来 Window Rules 可以允许特定 client 使用其他策略。

---

# 9. Window Snapping

Snapping 是 box2430 的核心交互能力之一。

V1 snapping target：

```text
left
right
top-left
top-right
bottom-left
bottom-right
maximize
```

即支持：

- 左半屏
- 右半屏
- 最大化
- 四角 1/4

V1 不单独定义 `snap top` / `snap bottom`。

Snapping 应同时适用于：

- mouse gesture
- keyboard command

## Snap State

Snapping 是有状态的快捷 placement。

窗口保留：

```text
normal_geometry
snap_state
```

例如：

```text
normal_geometry = 1200x800 @ (300,180)
snap_state = left
```

从 snap 状态拖出时，可以恢复之前的 normal geometry。

但：

> 任意手动 move 或 resize 都立即解除 snap state。

解除后：

```text
current geometry
→ new normal_geometry
```

Snap 不形成持续 layout constraint。

这些 geometry manipulation 只属于 FREE mode。当前 workspace 为 MONOCLE 时：

```text
move / resize / snap / maximize
→ no-op
```

不会暗中修改 FREE geometry，也不会自动退出 MONOCLE。

`fullscreen` 不属于 geometry edit；它仍可在 MONOCLE 上作为 temporary override 正常进入和退出。

box2430 不因此演化成 tiling WM。

---

# 10. 跨 Monitor 移动

提供两种不同语义。

## 普通 `window move-monitor`

```text
window → target monitor
```

窗口进入：

> 目标 monitor 当前正在显示的 workspace。

例如：

```text
A currently on WS2
B currently on WS4

Firefox @ A:2
→ window move-monitor next
→ target = B
→ Firefox @ B:4
```

这是默认行为。

## Keep Workspace

另提供显式操作：

```text
window move-monitor next --keep-workspace
```

例如：

```text
A:2
→ B:2
```

即保持 workspace index。

两个操作表达不同用户意图，不混在一起。

---

# 11. Window Decoration

box2430 默认不实现传统完整 SSD。

V1 只提供：

```text
WM border
```

例如：

- focused border
- unfocused border
- configurable border width
- fullscreen 时 border = 0

不实现：

- WM titlebar
- close button
- maximize button
- minimize button
- SSD theme
- frame theme engine

GTK / Qt / Electron 等 client 自己提供的 CSD 保持原样。

如果 CSD 自带：

- shadow
- transparent padding
- invisible margin

box2430 不进行 visual-geometry 修正。

这属于 client 自己的表现问题。

---

# 12. Fullscreen Policy

box2430 将：

```text
client fullscreen request
```

与：

```text
user / WM fullscreen command
```

视为两个不同概念。

## Client Fullscreen Policy

client 请求 fullscreen 时，可配置：

```text
allow
fake
deny
```

### allow

```text
client believes fullscreen = yes
actual monitor fullscreen = yes
```

WM 真正将窗口铺满 monitor。

### fake

```text
client believes fullscreen = yes
window geometry = unchanged
```

应用可以进入 fullscreen presentation，例如浏览器隐藏 UI、视频填满 client area，但 WM 不让它夺取整个 monitor。

### deny

```text
client believes fullscreen = no
geometry = unchanged
```

WM 直接拒绝 fullscreen 状态。

默认倾向：

```text
fake
```

核心思想：

> Client 可以请求 fullscreen，但 geometry authority 属于 WM。

---

## User Fullscreen

用户主动执行 WM command：

```text
fullscreen
```

则始终是真 fullscreen。

用户拥有比 client 更高的权限。

---

## Fullscreen 与 Workspace Mode

Fullscreen 不修改：

```text
FREE / MONOCLE
```

状态。

例如：

```text
MONOCLE
→ Firefox fullscreen
→ exit fullscreen
→ 回到原本 MONOCLE
```

Fullscreen 是 window state，不是 workspace mode。

---

# 13. 特殊窗口与 EWMH

box2430 采用：

> practical EWMH support

而不是完整追求所有 EWMH 行为。

重点支持：

```text
NORMAL
DIALOG / TRANSIENT
FULLSCREEN
DOCK
DESKTOP
NOTIFICATION
```

大致行为：

### NORMAL

正常管理。

### DIALOG / TRANSIENT

正常 managed，但默认 placement 仍是 selected monitor workarea center。

### FULLSCREEN

进入前述 fullscreen policy。

### DOCK

识别 panel/dock。

尊重合理的 workarea / reserved area。

### DESKTOP

作为特殊低层窗口处理，不进入普通窗口 workflow。

### NOTIFICATION

不作为普通窗口参与：

- workspace tab
- Alt-Tab
- normal placement

---

# 14. Window Rules

V1 应拥有实用但简单的 one-shot rule system。

## Match

至少支持：

```text
class
instance
title
window_type
```

## Action

至少支持：

```text
workspace
monitor
focus_on_map
raise_on_map
border
fullscreen_policy
placement
```

V1 **不提供 `always_on_top`**。该能力需要正式的 stacking-layer contract，统一延后到 Post-V1。

例如：

```toml
[[rule]]
class = "Firefox"
fullscreen_policy = "fake"

[[rule]]
class = "mpv"
fullscreen_policy = "allow"
workspace = 3
```

Rule 默认：

> 只在 window 首次被 manage 时应用。

若 rule 将新窗口送到当前不可见的 workspace：

```text
focus_on_map = true
→ 不切换 workspace
→ 不 focus 该 hidden client
```

若最终 destination workspace 当前已经可见，则 `focus_on_map = true` 正常生效；若 destination 位于另一块 monitor，focus 该 client 同时更新 `selected_monitor`。

后续：

- title 改变
- state 改变
- content 改变

不会触发完整重新匹配。

因此 box2430 不做 reactive policy engine。

---

# 15. Alt-Tab / MRU

窗口切换应维护独立 MRU 顺序。

默认：

```text
Alt+Tab
→ current workspace full MRU cycle
```

一轮 MRU cycle 从按住 Alt 开始，到释放 Alt 结束：

```text
start
→ snapshot current mru_order
→ repeated Tab walks snapshot cyclically
→ intermediate focus changes do not reorder the snapshot
→ Alt release commits final client to real MRU
```

因此单独重复“按下并松开 Alt+Tab”自然在最近两个窗口间快速切换；要继续翻到更旧窗口，则按住 Alt 连续 Tab。

MRU 与：

```text
tab order
stacking order
```

相互独立。

后续可以扩展：

- current monitor all workspaces
- all monitors

但不是 V1 核心要求。

---

# 16. V1 最小 Stacking Precedence

V1 不实现一般化 stacking-layer framework，也不提供 `always_on_top`。

但必须冻结最小可见顺序（低 → 高）：

```text
Desktop
Normal clients
MONOCLE Tab Bar / Dock
Fullscreen client
Snap preview / necessary WM overlay
```

因此 fullscreen 会覆盖普通 client、Dock 与 MONOCLE Tab Bar；必要的短暂 WM overlay 可以位于 fullscreen 之上。

完整的 `ABOVE` / always-on-top / notification 等 layer policy 留到 Post-V1。

---

# 17. 配置系统

V1 使用：

```text
TOML
```

不使用：

- XML
- compile-time configuration

Lua 暂不考虑，以避免引入 scripting runtime 的额外复杂度。

配置内容包括：

- general behavior
- focus
- border
- snapping
- workspace
- rules
- keybindings
- fullscreen policy

---

# 18. Command Architecture

虽然 V1 可以只有 TOML，但从第一天开始必须设计统一 Command Dispatcher。

例如：

```text
workspace 2
window close
window move-workspace 3
window move-monitor next
snap left
fullscreen toggle
mode monocle toggle
focus next-tab
focus next-mru
spawn kitty
```

所有用户操作都应经过：

```text
Command
    ↓
Command Dispatcher
    ↓
WM State / Action
```

Keybinding 不应直接操作内部 WM state。

---

# 19. Future IPC

IPC 不是 V1 必须完成的功能。

但内部结构必须允许未来自然加入：

```text
box2430ctl
    ↓
Unix Socket
    ↓
Command Parser
    ↓
Command Dispatcher
```

IPC 与 keybinding 使用相同 command vocabulary。

未来 IPC 可以逐步加入：

- action command
- query
- state inspection
- event subscription

但这些都属于后续阶段。

---

# 20. Future Script-Init Configuration

长期希望支持类似 bspwm / river 的配置方式：

```sh
box2430ctl set ...
box2430ctl bind ...
box2430ctl rule ...
```

用户可以使用：

```text
~/.config/box2430/init
```

或类似 shell script 完成完整配置。

届时：

```text
script init
→ IPC
→ Command Dispatcher
```

TOML 仍可以保留。

两种方式可以同时存在。

这种设计也天然提供类似 hot reload 的效果：

> 重新执行 init script 即重新 apply configuration。

因此不需要专门实现复杂的 config file watcher。

---

# 21. V1 核心能力摘要

box2430 V1 的产品核心可以压缩为：

```text
X11
Stacking First

Per-Monitor Workspaces

FREE
MONOCLE + Tab Bar

Mouse Move / Resize

Window Snapping

Click Focus
+ Sloppy

Strict WM-Controlled Placement

Border-Only Decoration

Client CSD Left Alone

allow / fake / deny Fullscreen Policy

Practical EWMH Support

One-Shot Window Rules

TOML Configuration

Unified Command Dispatcher

Future IPC Ready
```

---

# 22. 产品哲学总结

box2430 最重要的设计思想不是“功能少”，而是：

> **只实现真正影响窗口管理体验的机制，并让这些机制保持简单、可预测、由用户控制。**

核心职责：

```text
focus
stacking
workspace
geometry
snap
monocle
window navigation
policy
```

非核心职责尽量交给外部程序。

最终体验应接近：

> **Openbox 的自由 stacking 桌面，配上 dwm 的克制与 monocle、i3 的 tab 反馈，以及 bspwm 式未来可脚本化的控制能力。**

同时避免引入这些系统各自不符合需求的复杂部分。

box2430 不试图成为一个“什么都能做”的 WM。

它首先应该成为一个：

> **自己愿意每天使用的 X11 window manager。**
