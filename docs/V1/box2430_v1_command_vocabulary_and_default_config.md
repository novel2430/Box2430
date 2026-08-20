# Box2430 — V1 Command Vocabulary and Default Config

**Document Version: V2**

> 本文冻结 box2430 V1 的公共命令词汇表（Command Vocabulary）与默认配置表面（Default Config Surface）。
>
> 它不属于某一个单独的 Step，而是一份横跨 Product / State / Architecture / Interaction 的公共接口契约。
>
> TOML keybinding、未来 `box2430ctl`、未来 IPC，以及其他可能的 command source，都必须复用同一套命令语义。
>
> 本文只列入 V1 真正计划实现且会实际生效的 command 与 config。禁止为了未来扩展先加入 dummy command / dummy config。
>
> **V2 alignment:** 对齐最终 V1 freeze：focus 仅 click/sloppy；MRU cycle 使用 snapshot session；MONOCLE geometry commands no-op；maximize 统一恢复 `normal_geometry`；hidden rule destination 不拉动视角；删除 V1 `always_on_top`；配置验证失败整份 fallback 到 safe defaults。

---

# 1. 核心原则

box2430 的 command surface 是稳定公共接口。

例如：

```toml
"Super+m" = "mode monocle toggle"
```

未来 IPC 出现后，应继续使用同一语义：

```bash
box2430ctl mode monocle toggle
```

不能出现：

```text
TOML 使用一套命名
IPC 使用另一套命名
内部 handler 再使用第三套命名
```

统一模型：

```text
argv-like command
        ↓
Command Registry
        ↓
per-command handler
        ↓
WM core state transition
```

---

# 2. 命名原则

优先使用：

```text
<domain> <verb-or-target> [arguments] [flags]
```

例如：

```text
window move-monitor next --follow
window move-workspace 3
focus next-tab
workspace 2
```

避免：

```text
send-to-next-monitor-and-follow
move-window-to-workspace-three
```

这类不断增长的组合式 command name。

---

# 3. V1 Command Domains

V1 公开以下 command domains：

```text
wm
spawn
workspace
monitor
window
focus
mode
snap
maximize
fullscreen
mouse
tab
```

其中：

```text
mouse
tab
```

属于 contextual command domain，只能在对应 input context 中使用。

---

# 4. WM Commands

```text
wm quit
wm restart
```

## 4.1 `wm quit`

退出 box2430。

```text
wm quit
```

必须走正常 WM shutdown 流程。

---

## 4.2 `wm restart`

重新启动 box2430。

```text
wm restart
```

V1 的具体 restart implementation 可由实现决定，但用户语义必须是：

> 重新加载 box2430 process 与 startup config，而不是单纯重新绘制。

---

# 5. Spawn

```text
spawn <program> [args...]
```

例如：

```text
spawn kitty
spawn firefox --private-window
```

Command Registry 接收 argv 边界，不要求 handler 再解析 shell string。

原则：

```text
program + argv
```

而不是：

```text
交给 /bin/sh -c 重新解释整条字符串
```

除非用户显式 spawn shell。

---

# 6. Workspace Commands

```text
workspace <N>
workspace next
workspace prev
```

---

## 6.1 `workspace <N>`

切换：

```text
selected_monitor
```

的 local workspace。

例如：

```text
workspace 3
```

表示：

> selected monitor 切换到自己的 Workspace 3。

它不是 global EWMH desktop switching。

---

## 6.2 `workspace next`

切换到 selected monitor 的下一个 local workspace。

```text
workspace next
```

使用循环语义。

---

## 6.3 `workspace prev`

切换到 selected monitor 的上一个 local workspace。

```text
workspace prev
```

使用循环语义。

---

## 6.4 Workspace Focus Restore

切换 workspace 后：

```text
if target.last_focused_client exists:
    restore focus
else:
    focused_client = None
```

`selected_monitor` 保持不变。

---

# 7. Monitor Commands

```text
monitor next
monitor prev
```

---

## 7.1 `monitor next`

选择下一个 monitor。

```text
monitor next
```

行为：

```text
target = next monitor
selected_monitor = target
pointer → target monitor center
```

如果 target 当前 workspace 有合法：

```text
last_focused_client
```

则恢复 focus。

否则：

```text
focused_client = None
```

---

## 7.2 `monitor prev`

与 `monitor next` 对称，选择上一个 monitor。

Monitor navigation：

```text
cyclic
```

---

## 7.3 Empty Monitor Is Valid

以下情况必须合法：

```text
selected_monitor = Monitor B
focused_client = None
```

因此：

```text
monitor next
```

不能因为目标 workspace 为空而 no-op。

---

# 8. Window Lifecycle / Stacking

```text
window close
window raise
window lower
```

---

## 8.1 `window close`

关闭当前 focused client。

```text
window close
```

应优先遵循 ICCCM：

```text
WM_DELETE_WINDOW
```

若 client 不支持，再由 WM 按实现策略处理。

---

## 8.2 `window raise`

```text
window raise
```

只改变 stacking order。

必须：

```text
geometry unchanged
```

---

## 8.3 `window lower`

```text
window lower
```

只改变 stacking order。

必须：

```text
geometry unchanged
```

---

## 8.4 Focus Is Not Raise

Command semantics 保持：

```text
focus != raise
```

是否 focus 时自动 raise，由：

```text
raise_on_focus
```

policy 决定。

---

# 9. Window → Workspace Movement

```text
window move-workspace <N>
window move-workspace <N> --follow
```

---

## 9.1 Default

```text
window move-workspace 3
```

表示：

> 将 focused client 移动到当前 monitor 的 Workspace 3，但用户留在原 workspace。

原则：

```text
move does not imply follow
```

---

## 9.2 `--follow`

```text
window move-workspace 3 --follow
```

表示：

> 移动 client，并同时跟随到目标 workspace。

follow 会更新用户当前视角与 focus。

---

# 10. Window → Monitor Movement

基础形式：

```text
window move-monitor next
window move-monitor prev
```

可组合：

```text
--follow
--keep-workspace
```

---

## 10.1 Default Target Workspace

```text
window move-monitor next
```

默认将 client 移动到：

```text
target_monitor.current_workspace
```

用户留在原处。

---

## 10.2 `--follow`

```text
window move-monitor next --follow
```

移动 client，并将用户视角跟随到目标 monitor。

---

## 10.3 `--keep-workspace`

```text
window move-monitor next --keep-workspace
```

目标 workspace 使用：

> client 当前 local workspace index 在目标 monitor 上的对应 workspace。

例如：

```text
Monitor A : WS3
client @ A:WS3
```

执行：

```text
window move-monitor next --keep-workspace
```

目标：

```text
Monitor B : WS3
```

---

## 10.4 Flag Combination

允许：

```text
window move-monitor next --keep-workspace --follow
window move-monitor prev --keep-workspace --follow
```

flag 顺序不应改变语义。

---

# 11. Focus Commands

```text
focus next-tab
focus prev-tab
focus next-mru
focus prev-mru
```

---

## 11.1 Tab Order

```text
focus next-tab
focus prev-tab
```

使用：

```text
tab_order
```

特点：

- 稳定顺序
- focus 不重排
- raise/lower 不重排
- cyclic
- 与 MONOCLE Tab Bar visual order 一致

---

## 11.2 MRU Order

```text
focus next-mru
focus prev-mru
```

使用：

```text
mru_order
```

典型用途：

```text
Alt+Tab
```

MRU command 支持 cycle-capable input context：

```text
cycle start
→ freeze mru_order snapshot

repeat next-mru / prev-mru in same held-modifier session
→ walk snapshot cyclically
→ do not reorder real MRU on intermediate focus

cycle end / modifier release
→ commit final focused client as newest MRU
```

若 command source 没有可维持的 cycle context，则一次 `focus next-mru` / `focus prev-mru` 视为一步 cycle 并立即 commit。

具体 keybinding 不属于 command semantic，但 binding layer 必须能把 held-modifier lifecycle 作为 command context 传入。

---

## 11.3 Ordering Independence

以下顺序必须独立：

```text
tab_order
mru_order
stacking_order
```

任何 focus command 都不能隐式把三者合并为同一种 order。

---

# 12. Workspace Mode

```text
mode free
mode monocle
mode monocle toggle
```

---

## 12.1 `mode free`

确保当前 selected monitor 的 active workspace 进入：

```text
FREE
```

若已经 FREE，保持不变。

---

## 12.2 `mode monocle`

确保当前 workspace 进入：

```text
MONOCLE
```

若已经 MONOCLE，保持不变。

---

## 12.3 `mode monocle toggle`

```text
FREE ↔ MONOCLE
```

切换必须保持 geometry/state 可恢复性。

## 12.4 Geometry Commands While MONOCLE

当前 selected workspace 为 MONOCLE 时：

```text
mouse move-window
mouse resize-window
snap ...
maximize
maximize toggle
→ valid command, no-op result
```

它们不改变 FREE geometry，也不自动切回 FREE。

`fullscreen` / `fullscreen toggle` 不受此限制。

---

# 13. Snap Commands

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

---

## 13.1 Side Snap

```text
snap left
snap right
```

将 focused client 放置到对应 workarea 区域。

---

## 13.2 Corner Snap

```text
snap top-left
snap top-right
snap bottom-left
snap bottom-right
```

将 focused client 放置到对应 corner 区域。

---

## 13.3 `snap maximize`

```text
snap maximize
```

与：

```text
maximize
```

共享同一 maximize state transition。

禁止实现为两套互相独立的 maximize state。

---

## 13.4 `snap none`

```text
snap none
```

退出当前 snap/maximize geometry state，并恢复对应 normal geometry。

---

# 14. Maximize

```text
maximize
maximize toggle
```

---

## 14.1 `maximize`

在 FREE workspace 中，确保 focused client 进入 maximized state。

MONOCLE 中遵循 12.4：command 合法但 no-op。

如果 FREE 中已经 maximized：

```text
remain maximized
```

即：

> `maximize` 是 idempotent command。

---

## 14.2 `maximize toggle`

FREE 中：

```text
normal/snapped → maximized
maximized → normal_geometry
```

MONOCLE 中 no-op。

若 maximize 前为 snapped，unmaximize 仍恢复唯一 `normal_geometry`，不会回到 snap state。

---

## 14.3 Workarea

maximize 使用：

```text
monitor workarea
```

即扣除 DOCK / strut 后的可用区域。

---

# 15. Fullscreen

```text
fullscreen
fullscreen toggle
```

---

## 15.1 `fullscreen`

用户显式 WM command：

```text
fullscreen
```

始终表示真正进入 fullscreen。

如果已经 fullscreen：

```text
remain fullscreen
```

它是 idempotent command。

---

## 15.2 `fullscreen toggle`

切换真正的 WM fullscreen。

Client 自己发出的 fullscreen request 仍然遵循独立 policy：

```text
allow
fake
deny
```

Command `fullscreen` 不受 `client_policy` 限制。

---

## 15.3 Fullscreen State Restoration

fullscreen 是：

```text
normal / snapped / maximized
```

之上的 temporary override。

因此：

```text
maximize
→ fullscreen
→ fullscreen toggle
```

应回到：

```text
maximized
```

而不是丢失原 state。

---

# 16. Mouse Context Commands

```text
mouse move-window
mouse resize-window
```

这些不是普通 global command。

它们要求：

```text
mouse press context
target client
pointer state
```

---

## 16.1 `mouse move-window`

典型绑定：

```toml
[bindings.mouse]
"Super+Button1" = "mouse move-window"
```

启动 explicit mouse move operation。

---

## 16.2 `mouse resize-window`

典型绑定：

```toml
"Super+Button3" = "mouse resize-window"
```

启动 dwm-style bottom-right resize：

```text
pointer warp → client bottom-right
→ resize
```

---

## 16.3 Context Validation

以下配置必须被 validator 拒绝：

```toml
[bindings.keys]
"Super+x" = "mouse move-window"
```

因为：

```text
mouse move-window
```

缺少 mouse context。

---

# 17. Tab Bar Context Commands

```text
tab focus
tab close
```

只允许在 Tab Bar input context 使用。

---

## 17.1 `tab focus`

需要：

```text
clicked tab target
```

然后：

```text
focus + raise target client
```

---

## 17.2 `tab close`

需要：

```text
clicked tab target
```

关闭该 client。

---

## 17.3 Context Validation

以下语义无效：

```toml
[bindings.keys]
"Super+x" = "tab close"
```

因为 keyboard binding 没有 clicked tab target。

Config loader 必须明确报错，而不是静默 no-op。

---

# 18. Command Context Metadata

Command Registry 应允许记录 command 可接受的 context。

概念：

```c
struct CommandContext;

typedef int (*CommandFn)(const struct CommandContext *ctx,
                         int argc,
                         const char **argv);

struct CommandDef {
    const char *name;
    CommandFn fn;
    unsigned allowed_contexts;
};
```

`CommandContext` 只承载 invocation metadata，不改变 argv-like public command vocabulary。

validation class 至少区分：

```text
NORMAL
MOUSE
TABBAR
```

invocation metadata 还应允许表达：

```text
mouse target / pointer state
clicked tab target
MRU cycle session identity
MRU cycle phase: start / repeat / commit
```

其中 MRU cycle session 由 keyboard/input layer 随 modifier press/release 生命周期维护；它不是新的 public command。没有 held-modifier lifecycle 的 IPC/standalone invocation 直接执行一步并立即 commit。

具体 struct / enum / bitmask 命名属于实现细节。

重点是：

> command 的合法使用环境必须可验证，而不是让 handler 在运行中随机失败。

---

# 19. TOML 与 Future IPC 必须共享 Handler

例如：

```toml
"Super+3" = "workspace 3"
```

Startup 时：

```text
parse once
→ resolve command
→ store handler + argv
```

keypress 时直接调用 resolved command。

不要求每次 keypress 重新 tokenize string。

未来：

```bash
box2430ctl workspace 3
```

则：

```text
Unix IPC
→ argv ["workspace", "3"]
→ same command registry
→ same handler
```

不能另写一套 IPC-specific workspace implementation。

---

# 20. V1 明确不提供的 Commands

以下 command 不属于 V1 vocabulary：

```text
window move left 10
window move right 10

window resize +10 +10
window resize-edge ...

window center
window set-geometry ...

window opacity ...
window sticky ...
window minimize ...
window shade ...

workspace rename ...

monitor focus <N>

query ...
subscribe ...
config ...
```

原因不是这些功能永远不需要，而是：

> Step 1–4 尚未为它们冻结 V1 用户语义。

因此不能为了 API 看起来完整而提前加入 dummy handler。

---

# 21. Future Extension Rule

未来新增 command 时，应继续遵循：

```text
1. 先有明确用户语义
2. 再加入 command vocabulary
3. 实现真实 state transition
4. TOML / IPC / scripts 共同复用
```

禁止：

```text
先把名字放进去
handler 返回 success
实际什么都没发生
```

---

# 22. V1 Command Tree

```text
wm
├── quit
└── restart

spawn
└── <program> [args...]

workspace
├── <N>
├── next
└── prev

monitor
├── next
└── prev

window
├── close
├── raise
├── lower
├── move-workspace <N> [--follow]
└── move-monitor <next|prev> [--follow] [--keep-workspace]

focus
├── next-tab
├── prev-tab
├── next-mru
└── prev-mru

mode
├── free
├── monocle
└── monocle toggle

snap
├── left
├── right
├── top-left
├── top-right
├── bottom-left
├── bottom-right
├── maximize
└── none

maximize
└── [toggle]

fullscreen
└── [toggle]

mouse
├── move-window
└── resize-window

tab
├── focus
└── close
```

---

# 23. V1 Command Invariants

## C1 — One Vocabulary

TOML、未来 IPC、未来 CLI 使用同一套 command vocabulary。

---

## C2 — One Handler Path

同一 command 不能因为来源不同而走不同 state transition。

---

## C3 — No Dummy Commands

V1 vocabulary 中出现的 command 必须真正生效。

---

## C4 — Unknown Commands Fail Clearly

未知 command：

```text
→ explicit config / IPC error
```

不得静默忽略。

---

## C5 — Invalid Arguments Fail Clearly

例如：

```text
workspace potato
```

必须报错。

---

## C6 — Invalid Context Fails Clearly

例如 keyboard binding 使用：

```text
mouse move-window
```

必须在配置加载阶段或命令验证阶段明确拒绝。

---

## C7 — Idempotent Commands Stay Idempotent

```text
maximize
fullscreen
mode free
mode monocle
```

是 ensure-state command。

其中 `maximize` 的 ensure-state 语义只在 FREE 中成立；MONOCLE 下按 C11 no-op。

它们不等于 toggle。

---

## C8 — Move Does Not Imply Follow

只有显式：

```text
--follow
```

才改变用户视角。

---

## C9 — Snap Maximize Equals Maximize

```text
snap maximize
```

与：

```text
maximize
```

共享同一 state transition。

---

## C10 — Command Names Are Public API

一旦 V1 冻结，不应为了内部重构随意改名。

内部函数名可以改变，公开 command spelling 不随意改变。

## C11 — MONOCLE Geometry Commands Are No-op

MONOCLE 下 move / resize / snap / maximize 不改变 FREE geometry；fullscreen 例外。

## C12 — MRU Cycle Does Not Reorder Mid-cycle

同一 cycle context 内的中间 focus 不更新真实 MRU；cycle 结束才提交最终窗口。

---

# 24. Status

本文前半部分冻结：

```text
box2430 V1 Command Vocabulary
```

后半部分继续冻结：

```text
box2430 V1 Default Config Surface
```

两者独立于 Step 1–4 编号体系，但共同属于 V1 public interface contract。


---

# 25. Default Config Contract

本节冻结 box2430 V1 的默认配置表面。

核心原则：

> **config surface = implemented surface**

也就是说：

```text
配置文件中出现的每一个字段
→ V1 都必须真的改变运行行为
```

禁止：

```text
先把未来字段写进 schema
实际 handler / state transition 尚未实现
```

例如 V1 尚未实现 IPC，则默认配置中不能提前出现：

```toml
[ipc]
enabled = true
```

同理也不提前出现：

```toml
[bar]
[animation]
[session]
[compositor]
```

---

# 26. Config Path and Loading

默认配置路径：

```text
$XDG_CONFIG_HOME/box2430/config.toml
```

若：

```text
XDG_CONFIG_HOME
```

未设置，则：

```text
~/.config/box2430/config.toml
```

V1 还应支持显式指定：

```bash
box2430 -c ./test.toml
```

方便：

- 临时配置
- Xephyr 测试
- acceptance testing
- 调试不同 profile

---

## 26.1 Startup-only Loading

V1：

```text
config is read once at startup
```

不要求：

- file watcher
- live reload
- runtime TOML reparse

修改配置后：

```text
restart box2430
```

即可。

未来 IPC / script-init 出现后，再引入 runtime configuration。

---

# 27. Strict Schema

配置解析必须 strict。

未知字段：

```text
→ explicit error
```

不能静默忽略。

例如：

```toml
[appearance.tabs]
magical_blur = true
```

如果 V1 不支持：

```text
config.toml: unknown option "appearance.tabs.magical_blur"
```

用户必须能够区分：

```text
配置写错了
```

和：

```text
配置已经生效
```

V1 采用 whole-config atomic validation：

```text
parse entire file
→ validate schema + values + all commands
→ success: apply entire user config
→ any failure: print explicit diagnostics
               discard entire user config
               continue startup with built-in safe defaults
```

禁止 partial apply。显式 `-c` 指定的配置同样遵循该语义。

---

# 28. V1 Maximum Config Surface

下面这份配置代表 V1 合理的“最大集合”。

所有字段必须是真实 effect，不得 dummy。

```toml
# ~/.config/box2430/config.toml


# ============================================================
# Workspaces
# ============================================================

[workspaces]

# 每个 monitor 都有自己独立的 1..count local workspaces
count = 9


# ============================================================
# Focus
# ============================================================

[focus]

# "click"  = click client to focus
# "sloppy" = pointer enter client to focus; root enter keeps old focus
mode = "click"

# focus client 时是否自动 raise
raise_on_focus = false

# 新普通 managed window 出现时是否 focus
focus_on_map = true

# 新普通 managed window 出现时是否 raise
raise_on_map = true

# MONOCLE 当前 client 消失后的 fallback：
# "tab" = 右邻居优先，再左邻居
# "mru" = 最近使用窗口
monocle_fallback = "tab"


# ============================================================
# Placement
# ============================================================

[placement]

# "center"
# "client"
normal = "center"

# dialog / transient
dialog = "center"


# ============================================================
# Fullscreen
# ============================================================

[fullscreen]

# client 自己请求 fullscreen：
# "allow"
# "fake"
# "deny"
client_policy = "fake"


# ============================================================
# Border Appearance
# ============================================================

[appearance.border]

width = 2

focused   = "#89b4fa"
unfocused = "#45475a"
urgent    = "#f38ba8"


# ============================================================
# MONOCLE Tab Bar Appearance
# ============================================================

[appearance.tabs]

enabled = true

height  = 24
padding = 8

font      = "monospace:size=10"
font_bold = "monospace:style=Bold:size=10"

active_fg = "#ffffff"
active_bg = "#3b4252"

inactive_fg = "#aaaaaa"
inactive_bg = "#222222"

urgent_fg = "#ffffff"
urgent_bg = "#bf616a"

active_bold   = true
inactive_bold = false
urgent_bold   = true


# ============================================================
# Snap Preview Appearance
# ============================================================

[appearance.snap_preview]

color = "#89b4fa"
width = 2


# ============================================================
# Snapping
# ============================================================

[snap]

enabled = true

# pointer 离 monitor edge 多近时出现 snap preview
edge_zone = 16

# left / right snap 占 workarea 的比例
side_ratio = 0.5

# corner snap 的宽高比例
corner_width_ratio  = 0.5
corner_height_ratio = 0.5

# mouse snapping 是否显示 outline preview
preview = true


# ============================================================
# Bindings
# ============================================================

[bindings]

# true:
#   load box2430 safe defaults
#   then user config overrides/removes them
#
# false:
#   start from an empty binding table
inherit_defaults = true


# ------------------------------------------------------------
# Keyboard bindings
# ------------------------------------------------------------

[bindings.keys]

"Super+Return" = "spawn kitty"

"Super+q" = "window close"

"Super+1" = "workspace 1"
"Super+2" = "workspace 2"
"Super+3" = "workspace 3"

"Super+Shift+1" = "window move-workspace 1"
"Super+Shift+2" = "window move-workspace 2"
"Super+Shift+3" = "window move-workspace 3"

"Super+m" = "mode monocle toggle"

"Super+j" = "focus next-tab"
"Super+k" = "focus prev-tab"

"Alt+Tab" = "focus next-mru"

"Super+Left"  = "snap left"
"Super+Right" = "snap right"

"Super+Up" = "maximize toggle"

"Super+f" = "fullscreen toggle"

"Super+Ctrl+Left"  = "monitor prev"
"Super+Ctrl+Right" = "monitor next"

# special value: remove inherited/default binding
"Super+x" = "none"


# ------------------------------------------------------------
# Mouse bindings on normal managed clients
# ------------------------------------------------------------

[bindings.mouse]

"Super+Button1" = "mouse move-window"
"Super+Button3" = "mouse resize-window"

# Can also remove an inherited mouse binding:
# "Super+Button1" = "none"


# ------------------------------------------------------------
# Mouse bindings on MONOCLE Tab Bar
# ------------------------------------------------------------

[bindings.tabbar]

"Button1"   = "tab focus"
"Button2"   = "tab close"
"Button3"   = "none"

"WheelUp"   = "focus prev-tab"
"WheelDown" = "focus next-tab"


# ============================================================
# Window Rules
# ============================================================

[[rules]]

class = "Firefox"

fullscreen_policy = "fake"


[[rules]]

class = "mpv"

fullscreen_policy = "allow"

workspace = 3
monitor   = 2

placement = "center"

focus_on_map = true
raise_on_map = true

border = true


[[rules]]

class = "SomeChat*"
title = "*Updater*"

focus_on_map = false
raise_on_map = false
```

---

# 29. Config Semantics

## 29.1 Workspaces

```toml
[workspaces]
count = 9
```

表示：

```text
每个 monitor
→ 拥有 1..9 自己独立的 local workspaces
```

不是：

```text
全局只有九个 shared desktops
```

---

# 30. Focus Config

```toml
[focus]
mode = "click"
raise_on_focus = false
focus_on_map = true
raise_on_map = true
monocle_fallback = "tab"
```

这些字段必须分别影响：

- input focus policy
- focus 是否伴随 raise
- new-window focus
- new-window raise
- MONOCLE client disappearing fallback

不能因为实现简单而把：

```text
focus_on_map
raise_on_map
raise_on_focus
```

合并成一个布尔值。

`mode` 的 V1 枚举只有：

```text
click
sloppy
```

sloppy 下 pointer 进入 root / desktop 空白区域保留原 focus。

---

# 31. Placement Config

```toml
[placement]
normal = "center"
dialog = "center"
```

V1 至少支持：

```text
center
client
```

默认仍为：

```text
center
```

即普通 managed window 不让 client 自由决定初始 desktop placement。

---

# 32. Fullscreen Config

```toml
[fullscreen]
client_policy = "fake"
```

只控制：

```text
client-initiated fullscreen request
```

值：

```text
allow
fake
deny
```

用户主动执行：

```text
fullscreen
fullscreen toggle
```

不受这里限制。

---

# 33. Appearance Config

V1 Appearance deliberately small。

支持：

```text
color
width
height
padding
font
bold
```

不支持：

```text
CSS
gradient
rounded corners
shadow engine
animation theme
markup engine
blur
```

---

## 33.1 Colors

V1 配置颜色统一使用：

```text
#RRGGBB
```

例如：

```toml
focused = "#89b4fa"
```

---

## 33.2 Border

```toml
[appearance.border]
width = 2
focused = "#89b4fa"
unfocused = "#45475a"
urgent = "#f38ba8"
```

必须真实改变 X11 client border。

---

## 33.3 Tab Bar

```toml
[appearance.tabs]
```

必须真实控制 MONOCLE Tab Bar：

- enabled
- height
- padding
- font
- bold font
- active fg/bg
- inactive fg/bg
- urgent fg/bg
- active/inactive/urgent bold

V1 使用 Xft，不引入 Pango/Cairo theme stack。

---

## 33.4 Snap Preview

```toml
[appearance.snap_preview]
color = "#89b4fa"
width = 2
```

控制 mouse snap 的 outline rectangle。

不允许只解析字段却始终使用 hard-coded color/width。

---

# 34. Snap Config

```toml
[snap]
enabled = true
edge_zone = 16
side_ratio = 0.5
corner_width_ratio = 0.5
corner_height_ratio = 0.5
preview = true
```

必须分别影响：

- 是否启用 mouse snapping
- edge activation threshold
- side snap geometry
- corner snap geometry
- preview 是否显示

Keyboard snap command 仍然是 command surface。

`snap.enabled` 主要控制 mouse edge snapping；不能让配置把所有 explicit keyboard snap command 无声禁用。

---

# 35. Binding Config

所有默认 binding：

```text
必须可以 override
必须可以 rebind
必须可以 unbind
```

没有 sacred keybinding。

---

## 35.1 `inherit_defaults`

```toml
[bindings]
inherit_defaults = true
```

加载模型：

```text
built-in safe defaults
        ↓
user config
        ↓
override / remove
        ↓
final binding table
```

如果：

```toml
inherit_defaults = false
```

则：

```text
final table starts empty
```

只加载用户明确配置的 bindings。

---

## 35.2 `none`

特殊值：

```toml
"Super+q" = "none"
```

表示：

```text
unbind
```

不能触发一个叫 `none` 的 dummy command。

---

## 35.3 One Final Action Per Binding

用户覆盖默认：

```toml
"Super+q" = "spawn foo"
```

最终只能执行：

```text
spawn foo
```

不能：

```text
window close
+
spawn foo
```

两个动作同时发生。

---

## 35.4 Command Validation

Binding 中出现的 command：

```text
必须在 startup 时解析并验证
```

例如：

```toml
"Super+x" = "workspace potato"
```

应在加载配置时明确失败。

---

## 35.5 Context Validation

例如：

```toml
[bindings.keys]
"Super+x" = "mouse move-window"
```

必须拒绝，因为该 command 需要 mouse context。

同理：

```toml
"Super+x" = "tab close"
```

不能放在普通 keyboard context。

---

# 36. Window Rules

V1 Rules 是：

> one-shot initial-manage rules

只在 client 初次进入 WM 管理时执行一次。

不是 reactive policy engine。

---

## 36.1 Match Fields

V1 支持：

```text
class
instance
title
window_type
```

---

## 36.2 String Matching

字符串 matcher 支持简单 shell-style glob。

例如：

```text
Firefox
SomeApp*
*Updater*
```

V1 不需要 regex engine。

---

## 36.3 Match Combination

同一条 rule 内多个 matcher：

```text
AND
```

例如：

```toml
[[rules]]
class = "Firefox"
title = "*YouTube*"
```

必须同时满足。

---

## 36.4 Rule Ordering

若多条 rule match：

```text
按文件顺序依次 apply
```

后面的显式字段覆盖前面的显式字段。

例如：

```toml
[[rules]]
class = "Firefox"
fullscreen_policy = "fake"

[[rules]]
class = "Firefox"
title = "*Special*"
fullscreen_policy = "deny"
```

第二条匹配时覆盖第一条的 fullscreen policy。

---

## 36.5 V1 Rule Actions

V1 允许规则设置：

```text
workspace
monitor
focus_on_map
raise_on_map
border
fullscreen_policy
placement
```

V1 不提供 `always_on_top`；它与 `ABOVE` stacking layer 一起延后到 Post-V1。

这些 action 必须全部真实生效，并且只影响 initial-manage 过程，不建立持续 policy。

Destination/focus 边界：

```text
final destination workspace is visible
+ focus_on_map = true
→ focus new client
→ selected_monitor = client.monitor

final destination workspace is hidden
+ focus_on_map = true
→ do not switch workspace
→ do not focus hidden client
→ selected_monitor unchanged
```

即 rule 负责 destination，`focus_on_map` 不拥有隐式 follow 权限。

---

# 37. Default Config Philosophy

box2430 提供：

> **Minimal Safe Defaults + Full Example Config**

不是：

> hard-coded immutable workflow

默认只负责确保首次启动可操作。

例如最小 fallback 可以包含：

```text
Mod+Return
Mod+q
Mod+1..N
Mod+Shift+1..N
Mod+LeftMouse
Mod+RightMouse
```

但所有这些默认项都必须：

```text
overrideable
unbindable
```

原则：

> **defaults are policy, not privilege**

---

# 38. Config and Command Relationship

Binding value 永远引用本文前半部分定义的 V1 Command Vocabulary。

例如：

```toml
"Super+m" = "mode monocle toggle"
```

解析为：

```text
Command Registry
→ mode monocle toggle handler
```

未来：

```bash
box2430ctl mode monocle toggle
```

必须进入同一 handler。

因此：

```text
Default Config
        ↓
Command Vocabulary
        ↓
Command Registry
        ↓
WM Core
```

是统一控制路径。

---

# 39. V1 Config Invariants

## CFG1 — Every Field Has Real Effect

schema 中出现的每个字段必须改变真实运行行为。

---

## CFG2 — No Dummy Config

不能为未来功能提前加入无效字段。

---

## CFG3 — Unknown Fields Fail Clearly

未知字段必须明确报错，不可仅 warning 后继续套用 user config，更不可静默吞掉。

---

## CFG4 — All Bindings Are Replaceable

所有 keyboard / mouse default bindings 必须支持 override / unbind。

---

## CFG5 — Bindings Use Public Commands

binding 不允许绕过 Command Registry 直接调用隐藏内部行为。

---

## CFG6 — Rules Are One-shot

V1 rules 只在 initial manage 时 apply。

---

## CFG7 — Rule Ordering Is Deterministic

multiple matching rules：

```text
file order
later explicit values override earlier ones
```

---

## CFG8 — Config Does Not Promise Future Features

V1 未实现的：

```text
IPC
bar
session restore
animation
compositor
```

不得提前进入默认 schema。

## CFG9 — Config Apply Is Atomic

user config 只有整份 parse + validate 成功才 apply；任何错误都整份弃用并使用 built-in safe defaults 启动。

---

# 40. Combined Public Control Surface

box2430 V1 的公共控制面由两部分组成：

```text
V1 Command Vocabulary
+
V1 Default Config Surface
```

二者共同构成：

```text
box2430 public control contract
```

未来新增：

```text
box2430ctl
Unix IPC
script-init
Polybar adapter
box2430-bar
```

都必须建立在这套 contract 之上，而不是另外创造一套不一致的控制语言。
