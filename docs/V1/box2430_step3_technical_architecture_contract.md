# Box2430 — Step 3 Technical Architecture Contract

**Document Version: V2**


> **V1 revision:** 补充 ICCCM `WM_HINTS` urgency 的状态映射：`WM_HINTS urgency → Client.urgent`，供 Step 2 的 workspace-derived urgency 使用。
>
> **V1 alignment revision:** 对齐最终 V1 stacking contract：不提供 public `always_on_top` / general layer API，仅冻结 Desktop → Normal → TabBar/Dock → Fullscreen → WM overlay 的最小 precedence。

> 本文冻结 box2430 的技术架构。
> Step 1 定义产品方向，Step 2 定义状态语义；Step 3 负责约束“这些语义应建立在怎样的技术骨架上”。
> 本文不规定最终源码目录、struct 名称或 helper function，只锁定不能被 Agent 随意改写的技术路线。

---

## 1. Step 3 目标

Step 3 需要保证 box2430：

- 保持 Small-C 风格
- 编译简单、依赖少
- 使用标准 X11 client API
- 与 X.Org / XLibre 兼容
- 不引入无必要的 frame、线程、UI framework
- 从第一天为未来 IPC / script-init 留出自然入口
- 不为了 EWMH 兼容牺牲 Step 1/2 定义的 per-monitor workspace 语义

---




# 2. 实现语言：C

box2430 使用 **C**。

选择 C 的核心原因是项目本质非常适合：

```text
X11 Event
    ↓
Small State Machine
    ↓
Command Handler
    ↓
Direct X Operation
```

不需要大型 runtime、对象系统或 framework。

---

# 3. Small-C Architecture

“用 C”并不等于自动轻量，因此冻结以下原则。

## 3.1 按真实规模设计

WM 实际管理的窗口通常只有个位数到几十个，因此：

```text
O(n) client lookup
O(n) rule matching
O(n) workspace iteration
```

完全可以接受。

不要为了理论复杂度主动引入：

- generic hashmap framework
- generic tree
- generic collection framework
- custom allocator / arena framework
- 大型 utility layer

简单线性结构优先。

## 3.2 数据结构允许 purpose-built

可以使用：

- intrusive linked list
- simple array
- direct pointer relationship
- 小型专用 helper

例如 `tab_order / mru_order / stacking_order` 可以由 Client 自己保存对应链接关系，而不是造通用容器系统。

## 3.3 编译体验

目标保持类似 dwm：

```sh
make
sudo make install
```

默认不要求：

- CMake
- Meson
- Python build helper
- code generator
- package-manager bootstrap

使用简单 Makefile 即可。

## 3.4 依赖哲学

当前正式核心依赖：

```text
libX11
libXinerama
libXft
```

小型第三方功能允许直接 vendor 源码，例如 tiny TOML parser。

避免无明确需求引入：

- GTK
- Qt
- GLib
- Cairo
- Pango
- libevent
- libuv
- 大型 UI / async framework

---

# 4. X.Org / XLibre Compatibility

box2430 必须基于标准 X11 client APIs / protocols。

正式目标：

```text
X.Org Server
XLibre
```

均可运行同一份实现。

禁止依赖：

- Xorg server 私有内部接口
- Xorg-specific implementation details
- 不必要的非标准 server hack

对 box2430 来说，X.Org 与 XLibre 都只是标准 X11 server implementation。

---

# 5. X11 API：Xlib

V1 使用 **Xlib only** 作为主要 X11 client API。

不因为 XCB 更“现代”而主动采用 XCB。

理由：

- API 短且直接
- 传统 WM 参考实现丰富
- ICCCM helper API 实用
- 符合 Small-C
- box2430 的负载规模无法体现 XCB async 模型的明显收益

## 5.1 同步 Round-Trip 纪律

Xlib 最大的问题不是所有操作都同步，而是部分 getter 会隐式等待 server reply。

因此：

> box2430 内部 state 是主要 truth source。

已知的信息，例如：

```text
client geometry
workspace ownership
focus state
client list
stacking state
```

不要反复向 X server 查询。

尤其避免在 hot path 中无意义调用：

```text
XGetGeometry
XQueryTree
XGetWindowAttributes
XGetWindowProperty
```

## 5.2 XCB 的未来位置

V1 不使用 XCB。

未来若某个 extension 明确使用 XCB 有实际收益，可以局部重新评估，但不能仅以“更新”作为理由。

---

# 6. Strict Non-Reparenting

box2430 是 **strict non-reparenting WM**。

普通 managed client 直接位于 root 下：

```text
Root
├── Firefox
├── kitty
├── Emacs
└── ...
```

不创建：

```text
WM Frame
└── Client
```

这种传统 SSD hierarchy。

原因是 box2430 已经明确：

```text
border-only
no SSD titlebar
```

因此不承担：

- frame lifecycle
- reparent/unreparent
- frame/client geometry translation
- frame extent
- titlebar hit testing
- decoration theme

Window border 直接使用 X11 window border。

MONOCLE Tab Bar 是独立 WM-owned X window：

```text
Root
├── box2430 TabBar
├── Firefox
├── Emacs
└── kitty
```

Tab Bar 不作为任何 client 的 parent。

## 6.1 V1 Minimal Stacking Tiers

V1 不为了一项 `always_on_top` 提前建立完整 layer framework。

实现只需保证以下用户可见 precedence（低 → 高）：

```text
Desktop
Normal managed clients
MONOCLE TabBar / EWMH Dock
Fullscreen client
Snap preview / necessary WM overlay
```

约束：

- fullscreen 必须覆盖 normal client、Dock 与 MONOCLE TabBar
- MONOCLE TabBar 必须能覆盖 MONOCLE active normal client
- snap preview / 必要 WM overlay 可以位于 fullscreen 之上
- V1 rule/config/command 不暴露 `always_on_top`
- `ABOVE` 与完整 stacking layer policy 延后到 Post-V1

实现可以用少量显式 restack policy 完成，不要求先抽象通用 layer subsystem。

---

# 7. Monitor：Xinerama-First

box2430 使用 **Xinerama-first / dwm-like monitor model**。

box2430 眼中的 Monitor 是：

```text
一个 Xinerama rectangular screen region
```

而不是：

```text
DP-1 / HDMI-1 / EDID physical device
```

Monitor 的核心数据只需要围绕：

```text
x
y
width
height
workarea
workspaces
active_workspace
```

## 7.1 box2430 不是 Display Configuration Daemon

不负责：

- resolution
- refresh rate
- rotation
- mirroring
- output placement
- EDID identity
- GPU provider
- display profile

这些继续交给：

```text
xrandr
autorandr
用户脚本
外部显示配置工具
```

## 7.2 Hotplug / Topology

采用接近 stock dwm 的简单方案。

不实现：

- persistent offline monitor
- ghost monitor
- EDID reconnect matching
- monitor session restore

如果物理显示器断开，但 X11 逻辑 topology 没变化：

```text
box2430 不主动处理
```

如果外部工具真正改变 Xinerama topology：

```text
box2430 按新 topology reconcile monitors
```

monitor 数减少时允许采用 dwm-like 的简单 client migration / cleanup。

不为低频 hotplug 场景增加复杂 persistence architecture。

---

# 8. Event Loop：单线程 FD-Driven

box2430 使用：

```text
single process
single thread
fd-driven event loop
```

V1 backend：

```text
poll()
```

## 8.1 为什么不是纯 XNextEvent

未来需要接入 Unix IPC socket。

若永久阻塞在：

```c
XNextEvent(...)
```

未来 IPC 会迫使我们增加 thread、signal/wakeup hack 或重写 event loop。

因此从 V1 就把 X11 connection fd 纳入：

```text
poll()
```

X11 fd 通过：

```c
ConnectionNumber(display)
```

取得。

未来自然扩展为：

```text
X11 fd
IPC listen fd
IPC client fd...
```

全部由同一个 thread 处理。

## 8.2 Xlib Event Queue

Xlib 自身有 userspace event queue，所以进入 `poll()` 前必须先 drain：

```text
while XPending():
    XNextEvent()
    handle_event()

poll(...)
```

poll 返回后再次 drain。

## 8.3 不做 Async Framework

不使用：

- libevent
- libuv
- GLib main loop
- custom async runtime
- X11 thread + IPC thread

## 8.4 poll 不是永远锁死

真正冻结的是：

```text
single-threaded fd-driven architecture
```

`poll()` 是 V1 最合适的 backend。

未来若真的出现大量 persistent IPC clients/subscribers，可以再评估 `epoll()`。

---

# 9. Practical ICCCM / EWMH

box2430 不追求完整实现所有 ICCCM / EWMH。

原则：

> 常用应用真正依赖的部分认真支持；无法自然表达 box2430 workspace model 的部分，不为了“看起来兼容”而扭曲内部语义。

## 9.1 ICCCM Practical Subset

至少支持：

```text
WM_PROTOCOLS
WM_DELETE_WINDOW
WM_TAKE_FOCUS
WM_STATE
WM_HINTS
WM_NORMAL_HINTS
WM_CLASS
WM_NAME
WM_TRANSIENT_FOR
```

用于：

- 正常关闭 client
- focus cooperation
- rules 的 class / instance
- title
- transient/dialog
- size hints

实现时允许根据真实兼容性测试少量补充必要 ICCCM 支持。


## 9.2 ICCCM Urgency Mapping

`WM_HINTS` 不只是被动读取兼容字段；V1 必须将其中的 urgency hint 映射到 box2430 core state：

```text
WM_HINTS urgency
→ Client.urgent = true
```

当 client 的 urgency hint 更新时，WM 必须同步：

```text
Client.urgent
```

当该 client 真正获得 keyboard focus 时：

```text
Client.urgent = false
```

Step 2 V2 中的 workspace urgency：

```text
workspace.urgent =
    any(client.urgent for client in workspace.clients)
```

由此推导。

因此状态流为：

```text
WM_HINTS
→ Client state
→ Workspace derived state
→ future bar / IPC consumer
```

不允许 bar、IPC adapter 或其他 UI component 自己维护一套独立 urgency truth。

## 9.3 EWMH Practical Subset

至少关注：

```text
_NET_SUPPORTED

_NET_WM_NAME
_NET_ACTIVE_WINDOW

_NET_CLIENT_LIST
_NET_CLIENT_LIST_STACKING

_NET_WM_STATE
_NET_WM_STATE_FULLSCREEN

_NET_WM_WINDOW_TYPE
_NET_WM_WINDOW_TYPE_NORMAL
_NET_WM_WINDOW_TYPE_DIALOG
_NET_WM_WINDOW_TYPE_DOCK
_NET_WM_WINDOW_TYPE_DESKTOP
_NET_WM_WINDOW_TYPE_NOTIFICATION

_NET_WM_STRUT
_NET_WM_STRUT_PARTIAL
_NET_WORKAREA

_NET_CLOSE_WINDOW
```

不允许 scope 无限制扩张成“完整桌面环境兼容工程”。

---

# 10. Workspace EWMH Exception

传统 EWMH desktop model 假设：

```text
global desktop index
global current desktop
```

例如：

```text
_NET_CURRENT_DESKTOP
_NET_WM_DESKTOP
```

而 box2430 是：

```text
Monitor A → WS2
Monitor B → WS4
```

两个 monitor 可以同时拥有不同 current workspace。

因此：

> **per-monitor workspace semantic 优先于传统 EWMH desktop compatibility。**

无法忠实表达 box2430 状态的 desktop properties：

- 可以不支持
- 或只做明确的最低限度 projection
- 不能成为 box2430 内部 truth source

未来真正 authoritative 的 workspace introspection 由：

```text
box2430ctl / IPC
```

提供。

---

# 11. Tab Bar Rendering：Xft

Tab Bar 使用：

```text
WM-owned X11 window
+ Xlib drawing primitives
+ Xft
```

Xft 负责：

- UTF-8 title
- CJK
- font fallback
- active/inactive/urgent 不同文字颜色
- 简单背景/文字 scheme

例如：

```text
[ Firefox ][ 终端 ][ Emacs ][ 音乐播放器 ]
```

明确不引入：

- Pango
- Cairo
- GLib

也不做：

- rich text markup
- HTML/Pango-like markup
- 通用文字 layout engine
- 完整复杂 shaping/bidi guarantee

Tab Bar 只是窗口导航 UI，不是排版系统。

---

# 12. Command Architecture：argv-like Registry

box2430 的 command system 采用：

> **bspwm / river-classic style argv-like command registry**

而不是 giant typed Command AST。

整体：

```text
argv-like command
        ↓
command registry
        ↓
per-command handler
        ↓
WM core operation
```

例如：

```text
workspace 2
window close
window move-workspace 3
window move-monitor next
snap left
fullscreen toggle
mode monocle
```

概念 C API：

```c
struct CommandContext;

typedef int (*CommandFn)(const struct CommandContext *ctx,
                         int argc,
                         const char **argv);

struct CommandDef {
    const char *name;
    CommandFn fn;
};
```

`CommandContext` 是轻量 invocation metadata，不是 giant Command AST。它用于承载 command string 本身无法表达、但 input source 已知的瞬时上下文，例如：

```text
normal keyboard invocation
mouse target / pointer state
tabbar target
MRU cycle session / phase
```

这样 public argv vocabulary 保持统一，同时 mouse/tab/MRU 等 context-sensitive command 不需要偷偷绕过 Command Registry。

每个 command handler 自己完成：

- argc validation
- argument parsing
- option parsing
- semantic validation
- 调用实际 WM operation

明确不要求：

```text
巨大 enum CommandType
巨大 union Command
中央 AST dispatcher
```

这与 Small-C 更匹配，也参考了 bspwm / river 的成熟实践。

---

# 13. TOML 与 Command Surface

V1 TOML 可以写：

```toml
"Super+2" = "workspace 2"
```

启动时建议：

```text
command string
    ↓ parse once
resolve handler + argv
    ↓
store with binding
```

按键时直接调用对应 handler，不需要每次重新 tokenize。

因此 TOML 只是 command surface 的一个入口，不是另一套 WM operation system。

---

# 14. Future IPC

IPC 不要求在 Step 3 / V1 立即完成，但必须能自然接入同一 command registry。

推荐方向参考 bspwm：

```text
box2430ctl argv
        ↓
Unix socket
        ↓
NUL-separated argv
        ↓
same command registry
```

例如：

```sh
box2430ctl spawn sh -c 'echo hello world'
```

shell 已经给出正确 argv：

```text
spawn
sh
-c
echo hello world
```

wire format 可以保持 argument boundaries：

```text
spawn\0sh\0-c\0echo hello world\0
```

因此无需自己写 shell quote / escape parser。

---

# 15. Future Query / Subscribe / Script-Init

未来 command domain 可以扩展：

```text
query
subscribe
config
workspace
window
monitor
```

例如：

```sh
box2430ctl query monitors
box2430ctl query windows
box2430ctl subscribe focus
box2430ctl subscribe workspace
```

输入协议与输出格式不必相同。

未来 query 可以输出：

```text
plain text
JSON
```

而 command input 仍是 argv-like protocol。

长期 script-init：

```sh
~/.config/box2430/init
```

本质上只是：

```text
shell
→ box2430ctl
→ IPC
→ same command registry
```

因此不需要第二套配置执行逻辑。

---

# 16. Step 3 Architecture Invariants

以下为冻结规则。

## A1 — C / Small-C
核心实现使用 C；优先直接、小型、purpose-built 代码。

## A2 — Simple Build
默认保持 `make` / `make install` 级别编译体验。

## A3 — Minimal Dependencies
当前正式核心依赖：

```text
libX11
libXinerama
libXft
```

小依赖优先 vendor。

## A4 — X.Org / XLibre
必须基于标准 X11 client protocol/API，同时面向 X.Org 与 XLibre。

## A5 — Xlib-First
V1 使用 Xlib，不主动引入 XCB。

## A6 — Non-Reparenting
普通 managed client 不被 reparent 到 WM frame。

## A7 — Xinerama Monitor Model
Monitor 是 Xinerama rectangle，不是复杂的物理 output identity。

## A8 — Not a Display Daemon
box2430 不负责配置显示输出。

## A9 — Single-Threaded FD Loop
X11 与 future IPC 由同一线程处理；V1 使用 `poll()`。

## A10 — State over Re-query
已知事实优先从 box2430 state 读取，避免无意义 Xlib synchronous round-trip。

## A11 — Practical ICCCM/EWMH
只做产品真实需要的协议 subset。

## A12 — Workspace Semantics Win
传统 EWMH desktop model 与 per-monitor workspace 冲突时，box2430 语义优先。

## A13 — Xft-only Simple UI
Tab Bar 用 Xft，不引入 Pango/Cairo/GTK/Qt stack。

## A14 — One Command Surface
TOML、future IPC、future script-init 共享同一 command vocabulary / handlers。

## A15 — argv-like Commands
使用 argv-like command registry，不强制 central typed Command AST。

---

# 17. Step 3 非目标

本阶段不决定：

- 最终源码目录结构
- struct/function 名称
- TOML parser 最终选型
- Tab Bar 高度/颜色
- 默认快捷键
- mouse move/resize gesture
- snap threshold
- tab click/wheel behavior
- IPC 是否在首个开发 milestone 就完成
- query JSON schema
- subscription wire protocol
- monitor persistence
- compositor integration

这些属于 Step 4、Step 5 或后续扩展。

---

# 18. 冻结后的技术骨架

```text
box2430

C / Small-C
│
├── simple Makefile
├── minimal dependencies
│   ├── libX11
│   ├── libXinerama
│   └── libXft
│
├── Xlib
├── strict non-reparenting
├── Xinerama monitor rectangles
├── dwm-like simple topology handling
│
├── single-threaded fd event loop
│   └── poll()
│
├── practical ICCCM / EWMH
│
├── Xft Tab Bar
│
└── argv-like Command Registry
    ├── TOML bindings
    ├── future Unix IPC
    └── future script-init
```

架构总原则：

> **small mechanism, explicit state, minimal policy machinery.**

---

# 19. Step 3 状态

```text
Step 1 — Product Definition
DONE

Step 2 — Semantic / State Contract
DONE

Step 3 — Technical Architecture Contract
DONE

Step 4 — Interaction Contract
NEXT

Step 5 — Acceptance Contract

Step 6 — Goal-driven Agent Development
```

Step 3 完成后，未经显式重新决策，不得：

- 改用其他主要语言
- 用 XCB 替代 Xlib 作为 V1 主 API
- 引入 reparenting frame architecture
- 将 monitor model 扩张为复杂物理 output identity system
- 引入多线程 event architecture
- 用大型 async/UI framework 替代 Small-C 方案
- 为 EWMH 兼容牺牲 per-monitor workspace 语义
- 建立与 IPC 分离的第二套 command execution path
