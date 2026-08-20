# Box2430 当前问题与修复建议

本轮需要处理以下四项。

暂不处理：

- bottom-half snapping；V1 明确规定 `bottom center -> no snap`。
- move / resize 时更新 gesture cursor。

## 1. Spawn child 继承错误的 SIGCHLD disposition

### 问题

`wm_run()` 将 `SIGCHLD` 设置为 `SIG_IGN | SA_NOCLDWAIT`，但
`command_spawn()` fork 后直接 `setsid()` 和 `execvp()`。child 没有在
exec 前恢复 `SIGCHLD`，因此 xterm 等需要管理自身子进程的程序可能继承
错误的 signal disposition。

当前 `execvp()` 失败后只 `_exit(127)`，也会令失败表现为“没有反应”。

### 建议

- 保留 WM 父进程的 `SIG_IGN | SA_NOCLDWAIT`，避免 zombie。
- fork child 在 `execvp()` 前用 `sigaction()` 将 `SIGCHLD` 恢复为
  `SIG_DFL`。
- 保持关闭 X connection fd、`setsid()` 和 `_exit(127)`。
- `execvp()` 失败时输出包含 program name 与 `strerror(errno)` 的简短错误。
- 不引入 shell wrapper 或通用进程管理层。

### dwm 参考

查看：

```text
~/src/dwm/dwm.c
setup()
spawn()
```

dwm 的 WM 父进程同样忽略 `SIGCHLD`，但 spawn child 会在 exec 前将其
恢复为 `SIG_DFL`。

## 2. MONOCLE Tab Bar 遮挡 client 顶部

### 问题

Tab Bar 位于 `monitor.workarea` 顶部，而 MONOCLE client 同时铺满整个
`monitor.workarea`。Tab Bar 又被 raise 到 normal client 上方，因此必然
覆盖 client 最上缘。

MONOCLE 不应只是“maximize 到完整 workarea，再盖上一条 Tab Bar”；它应有
扣除 workspace UI 后的独立 content area。

### 建议

计算 MONOCLE content area：

```text
content = monitor.workarea

if tabs are enabled:
    content.y      += tab_height
    content.height -= tab_height
```

- Tab Bar 继续放在 workarea 顶部。
- MONOCLE normal client 使用 content area，并正确扣除 client X11 border。
- tabs disabled 时使用完整 workarea。
- 对过大的 `tab_height` 做最小高度保护。
- 不修改 `monitor.workarea`、`client.geometry` 或 `normal_geometry`。
- 不把 Tab Bar 做成永久 EWMH Dock/strut。
- fullscreen 继续覆盖完整 monitor 与 Tab Bar；退出后恢复 MONOCLE
  presentation。

### dwm 参考

查看：

```text
~/src/dwm/dwm.c
updatebarpos()
resizebarwin()
monocle()
```

可参考 dwm 将 bar area 与 client layout area 分开的方式，但不能照搬其全局
bar 语义；Box2430 Tab Bar 只属于 MONOCLE workspace UI。

## 3. Mouse move 开始时 pointer 应 warp 到 client 中心

### 问题

当前 `mouse_begin_drag()` 只有 resize 会 warp 到右下角。move 直接使用最初
ButtonPress 的 root coordinates，未将 pointer 移至 client 中心。

### 建议

move 开始时按以下顺序处理：

1. 保持 MONOCLE/fullscreen 中 move 为 no-op。
2. snapped/maximized client 先恢复 `normal_geometry`。
3. focus client。
4. 保存恢复后的 geometry 为 `drag.start_geometry`。
5. 将 pointer warp 到恢复后 client 可见外框的中心。
6. 将同一个 warp destination 保存为 `drag.start_x/start_y`。
7. 后续 MotionNotify 继续用 pointer delta 移动窗口。

warp destination 与 drag origin 必须完全一致，否则 warp 产生的 MotionNotify
可能令窗口跳动。

不得改变 release-time window-center monitor ownership、edge snap detection、
`normal_geometry` 更新规则或 resize 的 bottom-right warp。

### dwm 参考

查看：

```text
~/src/dwm/dwm.c
movemouse()
resizemouse()
```

dwm 可用于参考 drag delta、warp 与 geometry 起点的一致性。move-to-center 是
Box2430 自身需求，不能直接复制 dwm 保留原 pointer position 的行为。

## 4. Whole-workarea preview 缺少右边与下边 outline

### 问题

普通 side/corner snap 的 geometry 已通过 `fit_workarea()` 扣除 client border，
preview 再加回两倍 border，可以得到正确 outer rectangle。

maximize preview 却直接使用完整 `monitor.workarea`，随后仍加两倍 client
border。因此 preview 比 workarea 更宽、更高；当 workarea 接触屏幕右边和
下边时，这两条 outline 会落到屏幕外并被裁掉。

例如：

```text
workarea      = 800x600 at 0,0
client border = 2
错误 preview  = 804x604 at 0,0
```

### 建议

preview 绘制应统一接收“最终 outer target rectangle”：

```text
maximize outer target
-> monitor.workarea exactly

side/corner outer target
-> snapped client inner geometry + client X11 borders
```

四条 outline 在 outer target 内侧绘制：

```text
top    = y
bottom = y + height - line_width
left   = x
right  = x + width - line_width
```

最小修复是 maximize preview 直接使用 workarea 的原始 width/height，不再
添加 client border。更稳妥的是增加一个小型 outer-target helper，避免再次
混淆 client inner geometry、X11 border 与 workarea。

内部最好不要继续让 `SNAP_NONE` 同时表示“无 snap”和“maximize preview”；
可以增加内部 preview kind 或明确的 maximize flag，但不要新增 public
command/config。

修复后仍须保证：

- preview 与 release 后的实际 outer placement 一致。
- Dock/strut 存在时使用 workarea，而不是完整 monitor geometry。
- bottom-center 保持 V1 的 no-snap 行为。
