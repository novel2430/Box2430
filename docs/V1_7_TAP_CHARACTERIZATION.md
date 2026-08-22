# Box2430 V1.7 TAP Characterization

This note records the established behavior of the V1.7 interaction, ownership,
visibility, and lifecycle paths before the TAP refactor. It is a refactor aid,
not a new product contract. The implementation and regression tests remain the
behavioral authority.

## Client activation and focus

Inputs include map policy, click and sloppy focus, stable-order focus cycling,
MONOCLE tab selection, workspace/monitor activation, lifecycle fallback,
accepted `_NET_ACTIVE_WINDOW` requests, and topology recovery.

The semantic transition selects a focusable client (or no client), changes
`focused_client`, selects the client's monitor, promotes workspace-local focus
history, and clears urgency. Its projection refreshes old/new borders and
passive button grabs, sends `XSetInputFocus` and/or `WM_TAKE_FOCUS`, mirrors
`_NET_ACTIVE_WINDOW` and selected-monitor `_NET_WORKAREA`, updates native UI,
and raises only when `raise_on_focus` requires it.

Focus normally does not alter stable/tab or stack order. MONOCLE focus-cycle
and tab-selection callers explicitly raise after focusing. A repeated request
to focus the already focused non-NULL client is a no-op.

`FocusIn` is different: it is an X observation. When compatible event details
report focus away from the semantic focused client, Box2430 re-sends the X
focus protocol for the existing client without changing semantic focus,
selected monitor, focus history, urgency, borders, or stacking.

An established metadata exception is intentionally preserved: changing
`WM_HINTS` or `WM_PROTOCOLS` can make the currently focused client no longer
focusable, but the property observation does not reactively move semantic
focus. A later semantic focus choice uses the refreshed capability. This is
covered by `xvfb_property_cache.sh`.

## Monitor and workspace activation

Monitor selection and client focus are related but distinct. A selected monitor
remains meaningful with root focus or an empty/non-focusable active workspace;
it is the command target and tray monitor.

`monitor next|prev` selects the destination, updates `_NET_WORKAREA`, warps the
pointer to its center, and then focuses its active-workspace fallback. Exposed
root Button1 selection performs the equivalent semantic selection/fallback
without pointer warping and only when the clicked monitor differs.

Workspace activation owns selected monitor, the destination monitor's
`active_workspace`, and its semantic focus result. Switching workspaces uses an
ordered X protocol:

1. choose and materialize the incoming workspace focus target/geometry;
2. select the monitor and commit its active workspace;
3. map and raise incoming clients in semantic stack order;
4. establish semantic/X focus;
5. increment `ignored_unmaps` and unmap outgoing clients;
6. update native UI and enforce final stacking.

The map-before-unmap overlap is intentional anti-flash behavior. Selecting a
different monitor's already-active workspace selects that monitor and focuses
its fallback. Activating the selected monitor's already-active workspace is a
no-op.

Keyboard workspace commands and workspace-bar commands already converge on
`workspace_activate()`. Workspace-bar hit testing and event timestamps remain
input-adapter concerns.

## Client ownership transfer

`client_move_to_workspace()` is reached by numbered workspace moves,
cross-monitor moves (including `--keep-workspace`), workspace-bar moves, and
cross-monitor drag release.

The ownership transition removes the client from source membership, stable/tab,
stack, and focus-history structures; changes its sole workspace owner; then
appends it to the destination membership/stable/stack structures. Moving does
not itself create destination focus history.

When requested, cross-monitor movement translates both semantic geometry and
normal/restore geometry by the monitor-origin delta, then clamps both to the
destination workarea. Snap/maximize/fullscreen/MONOCLE materialization retains
its existing precedence.

If the moved client was focused, source fallback is resolved while the old
tab/focus links still exist. Without follow, an active-source client is hidden
unless the destination is active, in which case it is mapped. With follow, the
destination workspace is activated, the moved client is mapped, focused, and
raised. A same-monitor move to an inactive workspace stays mapped briefly so
workspace activation can preserve its incoming-before-outgoing paint order.

## Manage, unmanage, and visibility

Management establishes global ownership and all workspace-local orders before
protocol setup and visibility projection. Initial placement/rules determine
ownership; semantic visibility is:

```c
client->workspace == client->workspace->monitor->active_workspace
```

Desired semantic visibility is not instantaneous X mappedness during ordered
transitions. Startup adoption, map requests, workspace activation, movement,
topology reconciliation, and shutdown each retain their protocol-specific
mapping order.

Every WM-generated hide increments `ignored_unmaps` before `XUnmapWindow`.
The later `UnmapNotify` consumes that causal token. A synthetic unmap or an
unmatched ordinary unmap withdraws the client instead. Destruction and
withdrawal both remove semantic ownership, but only withdrawal restores client
X state and `WithdrawnState`.

Unmanage chooses focused-client fallback before destructive unlinking because
the fallback order depends on the disappearing client's focus/tab links. It
then removes workspace/global ownership and performs protocol cleanup.

## Topology reconciliation

Topology is already plan/commit structured. It stages monitor continuity,
client destinations, selected-monitor continuity, and preferred focus; mutates
ownership and latent geometry without ordinary focus/mapping helpers; commits a
coherent monitor/workspace world; then recomputes workareas, rematerializes,
reconciles mapping, and restores focus/projections.

A surviving visible focusable client remains the semantic focused client and
becomes the selected-monitor anchor. Otherwise Box2430 clears focus and applies
the selected monitor's active-workspace fallback. This ordering and the
geometry-continuity model are unchanged refactor boundaries.

## Existing coverage used as the baseline

The untouched debug build and full Xvfb suite pass. High-risk behavior is
covered by `xvfb_workspace_transition.sh`, `xvfb_workspacebar.sh`,
`xvfb_focus_cycle.sh`, `xvfb_focus_history.sh`, `xvfb_focus_protocol.sh`,
`xvfb_focus_compat.sh`, `xvfb_property_cache.sh`, `xvfb_focus_urgency.sh`,
`xvfb_lifecycle.sh`, `xvfb_visibility_withdrawal.sh`,
`xvfb_semantic_geometry.sh`, and `xvfb_configure_request.sh`. The Xephyr
multi-monitor, topology, native-bar, and tray scenarios cover monitor
selection, per-monitor workspaces, movement/follow behavior, focus continuity,
and selected-monitor projections.
