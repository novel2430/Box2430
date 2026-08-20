# Microbox — Post-V1 Future Directions

**Document Version: V1**

> 本文记录 microbox 在 V1 之后可能继续发展的方向。
>
> 它不是 V1 acceptance contract，也不是必须立即实现的功能列表。
>
> 本文的作用是：
>
> - 记录已经讨论并形成明显倾向的后续方向
> - 防止未来实现时重新从零讨论
> - 保留扩展空间，同时明确增长边界
> - 避免 microbox 随时间失去最初的产品人格
>
> 总原则：
>
> **KISS，但不把极简本身当成目的。**
>
> microbox 应保持机制简单、接口清楚、行为可预测，但不追求 suckless 式“为了减少代码或依赖而拒绝合理用户体验”。

---

# 1. Post-V1 的基本哲学

V1 的目标是先得到一个：

```text
small
predictable
mouse-friendly
stacking-first
per-monitor-workspace
X11 window manager
```

Post-V1 不应把 microbox 变成：

```text
another desktop environment
another tiling framework
another policy engine
another giant configurable UI toolkit
```

未来扩展优先满足两个条件之一：

```text
1. 它直接改善 WM core 的窗口管理能力
2. 它是一个非常薄的 microbox-specific companion client
```

如果两者都不是，则默认交给外部生态。

---

# 2. 第一梯队：现有架构的自然延伸

第一梯队属于：

> V1 设计成熟后，很自然会出现的能力。

这些方向与当前架构高度兼容，不需要重新定义 microbox 的产品身份。

---

# 3. IPC Read Model

未来 IPC 不直接暴露内部 C data structure。

对外提供：

> **稳定、用户可理解的 read model。**

采用“实用型 read model”。

---

## 3.1 Monitor Read Model

至少可查询：

```text
monitor id
geometry
active workspace
selected
```

未来可扩展，但不暴露：

```text
raw pointer
Xlib internal object
intrusive list node
internal cache
```

---

## 3.2 Workspace Read Model

至少可查询：

```text
monitor
index
name / display label
mode
active
selected
occupied
urgent
client_count
```

其中：

```text
active
selected
occupied
urgent
```

沿用 V1 Step 2 的 derived-state 定义。

---

## 3.3 Window Read Model

至少可查询：

```text
stable/public window id
XID
class
instance
title
monitor
workspace
geometry
focused
urgent
fullscreen
snap_state
```

原则：

> public read model 描述用户能理解的 WM state，而不是内部实现结构。

---

# 4. IPC Query / Subscribe / Report

未来 IPC 不采用单一“大消息”解决所有 consumer。

采用三层模型：

```text
Query
Subscribe Events
Subscribe Report
```

---

## 4.1 Query

用途：

> 获取完整、稳定的当前状态。

概念上：

```text
query monitors
query workspaces
query windows
query focused
```

Query 返回 read model，而不是内部 C struct dump。

---

## 4.2 Subscribe Events

普通 subscription 使用轻量 domain event。

例如：

```text
workspace-focus
monitor-selected
window-added
window-removed
window-title
window-urgent
focus-changed
mode-changed
```

Event payload 原则：

```text
event type
+
object identity
+
this-change-relevant data
```

不要求每个 event 都携带完整 WM snapshot。

例如概念上：

```text
workspace-focus monitor=1 workspace=3
window-title xid=0x123
window-urgent xid=0x123 on
monitor-selected monitor=2
```

需要完整状态的 consumer 再使用 Query。

---

## 4.3 Subscribe Report

为 bar / status consumer 提供专门的：

```text
subscribe report
```

Report 不是底层事件，而是：

> 当前 presentation state snapshot。

特别适合表达：

```text
monitor
workspace
active
selected
occupied
urgent
mode
focused title
```

这样官方 bar 或 Polybar adapter 可以：

```text
subscribe report
→ render
```

而不用：

```text
event
→ query many objects
→ rebuild presentation state
```

这一路线主要吸收 bspwm `subscribe report` 的优点，同时保留普通轻量 events。

---

# 5. Runtime Configuration

V1：

```text
TOML
→ startup-only
```

Post-V1 的方向不是长期维持两套一等配置系统，而是：

```text
B → C
```

即：

```text
先实现足够完整的 runtime configuration
→ 再让 script-init 成为 canonical configuration
→ TOML 最终可以退出
```

---

## 5.1 Runtime Mutable Config

未来大部分用户配置应能 runtime 修改：

```text
appearance
focus
placement
fullscreen policy
snap
bindings
rules
```

例如概念上：

```text
microboxctl config focus.mode click
microboxctl config appearance.border.focused "#ff0000"

microboxctl bind Super+Return spawn kitty
microboxctl unbind Super+q
```

Rules 也应可通过 runtime command 管理。

---

## 5.2 Canonical Truth

未来配置的 canonical truth 应是：

> **running WM state**

而不是 TOML file。

init script 的作用是：

> 把刚启动的 microbox 配置成用户想要的 runtime state。

最终目标：

```text
script-init
→ IPC / config / command registry
→ runtime state
```

而不是：

```text
TOML parser
→ one config implementation

IPC
→ another config implementation
```

---

# 6. Script Init

未来默认支持：

```text
$XDG_CONFIG_HOME/microbox/init
```

fallback：

```text
~/.config/microbox/init
```

该文件只要求：

```text
executable
```

不限定必须是 shell。

---

## 6.1 Startup Order

采用：

> bspwm-style auto-init + River-style ready-before-init

启动顺序：

```text
microbox starts
↓
X connection / WM ownership ready
↓
internal core state ready
↓
IPC socket ready
↓
execute microbox/init
↓
normal event loop
```

这样 init 中的：

```text
microboxctl ...
```

不存在 socket-not-ready race。

---

## 6.2 Not a Session Manager

microbox 只负责：

```text
execute init
```

不负责：

```text
supervise daemons
restart daemons
manage service lifecycle
kill entire init process group
act as session manager
```

如果用户在 init 中启动：

```text
dunst
picom
polybar
```

其生命周期由用户脚本 / session 环境自行处理。

---

# 7. Official Bar

未来可以提供官方：

```text
microbox-bar
```

但它不是 WM core 的一部分。

架构：

```text
same repo
same Makefile
same package
separate optional process
```

用户仍只需要：

```text
make
make install
```

即可获得：

```text
microbox
microboxctl
microbox-bar
```

不要求维护两个独立项目。

---

## 7.1 Runtime Optional

允许：

```text
microbox only

microbox + microbox-bar

microbox + polybar

microbox + another external bar
```

core 不依赖 bar 是否存在。

---

## 7.2 Official Bar Scope

官方 bar 采用极简方案：

```text
WM-native information
+
external stdin/status text
```

至少可显示：

```text
workspace state
selected monitor
workspace mode
focused window title
stdin status text
```

Workspace presentation 至少包含：

```text
active
selected
occupied
urgent
```

支持基本：

```text
workspace click
```

---

## 7.3 Explicit Non-Goals

官方 bar 不做：

```text
system tray
CPU module
RAM module
battery module
network module
audio module
JSON plugin framework
complex module engine
notification system
launcher
```

它不是 Polybar competitor。

外部状态可通过：

```text
script → stdin → microbox-bar
```

提供。

例如：

```text
date / battery / network / audio
```

均属于外部 status producer。

---

# 8. Polybar Compatibility

Post-V1 希望：

> 尽可能在不 patch Polybar 的情况下获得自然支持。

候选方向包括：

```text
bspwm-compatible report / adapter
generic custom module
dedicated microbox adapter
```

优先研究：

```text
bspwm-style monitor/workspace presentation compatibility
```

原因是 bspwm 对：

```text
per-monitor desktops/workspaces
```

的模型与 microbox 比传统 global EWMH desktop 更接近。

但不要求为了 Polybar 兼容而实现半套 bspwm core。

原则：

> compatibility layer 可以存在，但不能反过来扭曲 microbox 的内部语义。

---

# 9. Keyboard FREE Geometry

Post-V1 暂不计划：

```text
window move left 20
window resize right 20
move mode
resize mode
```

原因：

V1 已经覆盖真正需要的 keyboard navigation：

```text
focus next-tab
focus prev-tab
focus next-mru
focus prev-mru

workspace N / next / prev
monitor next / prev

window move-workspace ...
window move-monitor ...
```

FREE geometry manipulation 继续保持：

```text
mouse-first
```

如果未来出现真实使用需求，再重新讨论。

不为了 API completeness 提前设计。

---

# 10. 第二梯队：可能改变产品性格的功能

第二梯队属于：

> 可以考虑，但一旦引入，会改变 core semantic 或用户心智模型。

因此默认更谨慎。

---

# 11. Scratchpad

当前决定：

```text
not planned
not forbidden
```

用户当前没有实际 scratchpad 使用习惯。

因此不为了“很多 dwm 用户喜欢”就提前引入特殊 client state。

未来策略：

```text
先通过 IPC / script experiment
↓
如果真实使用证明有价值
↓
再决定是否升格为 core feature
```

不提前加入：

```text
Client.scratchpad
hidden global workspace
special visibility state
```

---

# 12. Minimize

明确：

> **不计划支持 minimize。**

原因：

microbox 的 workspace model 已足够承担：

```text
把窗口移出当前视野
切换工作上下文
```

不新增：

```text
Client.minimized
window minimize
window restore
```

因此避免处理：

```text
minimized 是否 occupied
MONOCLE 是否显示 minimized tab
MRU 是否包含 minimized
urgent + minimized
workspace restore 是否自动 unminimize
```

这也符合 microbox 不追求传统 desktop WM feature checklist 的原则。

---

# 13. Named Workspace

未来支持：

```text
optional workspace display name
```

但：

```text
workspace index
```

始终是 canonical identity。

例如：

```text
index = 2
name = "code"
```

Command / rule / IPC identity 仍然使用：

```text
workspace 2
window move-workspace 2
```

bar 可以显示：

```text
code
```

改名不会破坏：

```text
binding
rule
IPC identity
monitor/workspace ownership
```

原则：

> name is label, not identity.

---

# 14. Restart Persistence

Post-V1 希望支持：

> **同一个 X session 内的透明 WM restart。**

目标：

```text
microbox restart
→ 用户桌面尽量保持原样
```

尽量保留：

```text
client monitor/workspace ownership
FREE / MONOCLE mode
geometry
normal_geometry
snap state
maximize state
fullscreen state
tab order
MRU order
last_focused_client
selected_monitor
```

---

## 14.1 Explicit Boundary

不做：

```text
logout 后恢复 app
reboot 后恢复 desktop session
自动重新启动程序
cross-login session restore
```

这是 session manager 的职责。

原则：

> restart persistence belongs to WM; session restoration does not.

---

# 15. Future Rules

Rules 可以继续扩展 matcher/action vocabulary。

例如未来可以考虑：

```text
matcher:
role
parent/transient metadata
more window types

action:
initial geometry
snap
maximize
```

但必须永远坚持：

> **one-shot initial-manage**

流程：

```text
client first managed
→ evaluate rules once
→ apply
→ done
```

不演化为 reactive policy engine。

---

## 15.1 Explicit Non-Goal

不做：

```text
title changed
→ rule reruns
→ window moves automatically

class changes
→ policy reruns

state changes
→ rule engine continuously reacts
```

microbox Rules 是：

```text
initial placement/policy
```

不是后台自动化引擎。

---

# 16. Stacking Layers

未来可能需要正式整理 stacking layer。

候选概念：

```text
DESKTOP
NORMAL
ABOVE
DOCK
FULLSCREEN
WM_OVERLAY
```

其中：

```text
WM_OVERLAY
```

可能用于：

```text
MONOCLE Tab Bar
snap preview
```

V1 已经冻结一套**最小** precedence：

```text
Desktop
Normal
Tab Bar / Dock
Fullscreen
necessary WM overlay
```

但这不是完整 layer framework。

Post-V1 在引入 `ABOVE` 时，才正式加入 public `always_on_top` rule/state，并重新冻结更完整的 layer ordering。

未来设计时应重点明确：

```text
always-on-top / ABOVE
fullscreen
dock
notification
tab bar
snap preview
```

彼此的 stacking priority。V1 不应为了未来 `always_on_top` 预埋无效 config/action。

---

# 17. Companion Tools

允许同 repo 提供：

```text
microboxctl
microbox-bar
```

未来如有明确价值，也可增加非常薄的 microbox-specific tool。

判断标准：

> 它是否直接消费或控制 microbox 的 WM state？

如果答案是 yes，则可能适合进入 repo。

---

# 18. Growth Boundary

microbox 的长期边界：

> **可以扩展窗口管理机制和直接 WM clients，但不扩张成桌面环境。**

明确不进入 core/repo 发展主线的功能：

```text
notification daemon
launcher
wallpaper manager
compositor
settings daemon
file manager
audio daemon
network daemon
battery daemon
session manager
```

即使某些功能实现并不困难，也不因为“顺手”而纳入。

---

# 19. KISS, But Not Extremist

microbox 的 KISS 表示：

```text
simple state model
small public API
predictable behavior
few hidden heuristics
limited dependencies
clear process boundaries
```

它不表示：

```text
功能越少越正确
依赖越少越正确
用户体验可以为了代码短而牺牲
所有便利功能都必须 patch
所有 config 都必须编译进源码
```

因此可以接受：

```text
Xft
TOML in V1
IPC
runtime configuration
official bar
restart persistence
```

只要这些能力：

```text
有明确需求
边界清楚
实现简单
不会破坏核心模型
```

---

# 20. Future Decision Heuristic

以后每个新 feature 可以先问四个问题：

```text
1. 这是现实使用需求，还是 feature checklist？
2. 它能否建立在现有 state / command / IPC 上？
3. 它是否要求引入新的特殊 mutable state？
4. 它会不会把 microbox 推向 DE / policy engine / framework？
```

优先接受：

```text
high practical value
low semantic cost
small implementation surface
good composability
```

谨慎接受：

```text
special state
reactive policy
duplicated truth
large UI framework
session-level responsibility
```

---

# 21. Current Post-V1 Direction Summary

```text
IPC
├── practical read model
├── query
├── lightweight events
└── presentation report

Configuration
├── runtime mutable config
├── script-init
├── auto-init after IPC ready
└── TOML eventually removable

Official ecosystem
├── microboxctl
├── microbox-bar
└── Polybar compatibility / adapter

Bar
├── workspace state
├── mode
├── focused title
├── stdin status
└── no module ecosystem

Workspace
├── optional display name
└── index remains identity

Restart
├── preserve state inside same X session
└── no cross-session restore

Rules
├── vocabulary may grow
└── always one-shot

Not planned
├── keyboard FREE geometry
├── scratchpad
└── minimize

Growth boundary
├── WM mechanisms
├── thin WM-specific clients
└── never become a desktop environment
```

---

# 22. Status

```text
V1
→ defined by Step 1–4
→ plus V1 Command Vocabulary and Default Config

Post-V1
→ directions recorded here
→ not part of V1 acceptance
→ implementation order remains open
```

本文用于保留未来方向，不应被解释成：

```text
所有条目都必须立即实现
```

真正进入后续版本前，仍应针对具体 feature 单独冻结 semantic / architecture / acceptance contract。
