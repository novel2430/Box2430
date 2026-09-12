# Wayland Phase 0 Architecture Contract

Status: Phase 0A architecture decision record; Phase 0B extraction implemented
Snapshot date: 2026-09-12
Target Wayland runtime: river, using `river-window-management-v1`

Phase 0B implements this contract with an X11-free `core.h`/`core.c`, an
embedding `X11Client` runtime owner, separate X11 monitor UI attachments, and a
protocol-neutral Core topology planner. The remaining X11 runtime continues to
use the same direct transition/projection ordering rather than a generic backend
vtable.

This document was the design input for Phase 0B. It does not add Wayland support
and it does not prescribe a generic backend framework. Its purpose is narrower:
use the current X11 implementation and river's current window-manager contract as
two independent reference points, then identify the smallest backend-neutral
Box2430 authority that can naturally serve both.

The intended migration remains:

```text
Phase 0A  architecture contract and semantic mapping       <- this document
Phase 0B  extract Box core and X11 runtime attachments
Phase 1   add the river runtime skeleton
Phase 2   restore FREE behavior on river
Phase 3   restore MONOCLE behavior on river
Phase 4   add the river MONOCLE tab bar
Phase 5   finish snap/maximize/fullscreen/rules/bindings
```

The overriding rule for Phase 0 is:

> Share Box2430 semantics, not display-server protocol mechanics.

A small amount of duplicated frontend orchestration is preferable to an
abstraction that forces X11 and river to pretend they have the same lifecycle or
projection cadence.

## 1. External contract used for this design

As of the snapshot date, river `main` identifies itself as `0.5.0-dev` and uses
wlroots 0.20.1. The current `river-window-management-v1` interface is version 5.
River describes the window manager as an external client and states that the
river window-management protocols are stable.

The important contract is the protocol, not river's compositor implementation.
Phase 0B must therefore be designed against the following public semantics rather
than against private Zig structures:

* a logical `river_window_v1` may represent an xdg-toplevel or an XWayland
  window;
* logical outputs are represented by `river_output_v1` and carry global logical
  position and dimensions;
* keyboard/pointer interaction is represented per `river_seat_v1`;
* window-management state and rendering state are distinct and globally
  double-buffered;
* window-management state may be changed only inside a manage sequence;
* rendering state is applied at a render sequence boundary;
* requested window dimensions are proposals and the actual dimensions are
  reported later by the compositor;
* real fullscreen is a compositor-managed state, not merely a large rectangle;
* `river-layer-shell-v1` can report the output area remaining after exclusive
  layer-shell zones;
* `river_shell_surface_v1` is available for WM-owned UI such as the future
  MONOCLE tab bar.

Primary references:

* <https://isaacfreund.com/software/river/>
* <https://isaacfreund.com/docs/wayland/river-window-management-v1/>
* <https://isaacfreund.com/docs/wayland/river-layer-shell-v1/>
* <https://github.com/riverwm/river/blob/main/ARCHITECTURE.md>
* <https://github.com/riverwm/river/blob/main/build.zig.zon>

Useful implementation references, not normative contracts:

* Canoe, a stacking river WM: <https://github.com/roblillack/canoe>
* JrWM, a small C99 river WM: <https://github.com/jpco/jrwm>
* tinyrwm, river's example WM, linked from the river project site

Canoe is useful evidence that the protocol can support the broad Box2430
stacking feature family: click focus, free movement/resizing, multihead, snap,
maximize, fullscreen, rules, and optional SSD. JrWM is particularly useful as a
C implementation reference because it explicitly separates work performed in
manage and render sequences. Neither implementation is a template that Box2430
should copy wholesale.

## 2. What V2.3 already gets right

V2.3 is not backend-neutral, but its TAP architecture already contains the
correct high-level split:

```text
observation / input
        -> semantic transition
        -> authoritative Box state
        -> ordered projection
```

The useful existing authority is real rather than aspirational:

* `WMModel.selected_monitor` is an interaction authority independent from X
  input focus;
* `WMModel.focused_client` is semantic client focus;
* workspace membership, stable/tab order, stack order, and focus history are
  explicit in-memory orders;
* workspace ownership changes are already separated from their X11 projection;
* `client_should_be_mapped()` already derives desired visibility from Box state
  rather than treating current X mappedness as policy;
* `Client.geometry` is already latent across MONOCLE and real-fullscreen
  presentation: those modes use `present_client_geometry()` without overwriting
  the stored geometry;
* monitor reconciliation already separates a planning step from the final X/UI
  projection and preserves workspace/client continuity across topology changes.

Phase 0B should preserve these strengths. It is not a rewrite of the policy
model.

## 3. Where V2.3 still mixes authority and X11

`WMModel` currently owns objects that carry both semantic and platform state.
The main leaks are intentional historical ones, but they are exactly the Phase
0B extraction boundary.

### 3.1 Client

The current `Client` combines:

```text
Box semantics / policy
----------------------
workspace ownership
persistent geometry + restore geometry
snap state
stable / stack / focus orders
urgent state
maximize / fullscreen intent
rule-derived policy

X11 / projection attachment
---------------------------
Window XID
mapped / ignored_unmaps
WM_HINTS focus protocol facts
WM_TAKE_FOCUS fact
transient XID
WM_NORMAL_HINTS cache
original X border width
X11 decoration Window / Xft draw state
```

The first group belongs in the shared model, subject to the refinements below.
The second group must not remain in the shared model merely under more generic
names.

### 3.2 Monitor

The current `Monitor` correctly owns Box concepts such as logical geometry,
workarea, workspaces, and active workspace, but also owns native X11 UI objects:

```text
Window bar
XftDraw *bar_draw
bar widget hit rectangles
Window tab_bar
XftDraw *tab_draw
```

These projection attachments must leave the shared `Monitor`.

### 3.3 WMModel

`SpecialWindow` and its `_NET_WM_STRUT(_PARTIAL)` data are X11 observations, not
Box desktop authority. `WMModel.special_windows` must leave the shared model.
The resulting workarea is useful shared state; the protocol-specific source of
that workarea is not.

## 4. Target authority boundary

Phase 0B should make the following conceptual ownership true. Exact file names
and helper names may differ if a smaller change achieves the same contract.

```text
Box authority
+------------------------------------------------------------------+
| WMModel                                                          |
|   monitors                                                       |
|   selected_monitor                                               |
|   clients                                                        |
|   focused_client                                                 |
|                                                                  |
| Monitor                                                          |
|   geometry                                                       |
|   workarea                                                       |
|   workspaces                                                     |
|   active_workspace                                               |
|                                                                  |
| Workspace                                                        |
|   monitor                                                        |
|   mode: FREE / MONOCLE                                           |
|   membership order                                               |
|   stable/tab order                                               |
|   stack order                                                    |
|   focus history                                                  |
|                                                                  |
| Client                                                           |
|   workspace                                                      |
|   persistent windowed geometry                                   |
|   normal restore geometry                                        |
|   snap / maximize / fullscreen policy state                      |
|   membership/stable/stack/focus links                            |
|   portable metadata / semantic capability facts                  |
+------------------------------------------------------------------+
             ^ observations                    | presentation intent
             |                                 v
+----------------------------+      +------------------------------+
| X11 runtime attachment     |      | river runtime attachment     |
| XIDs / Atoms / WM_HINTS    |      | river_window_v1 / node       |
| mapped / ignored_unmaps    |      | actual dimensions            |
| RandR / struts             |      | output / seat objects        |
| Xft / bar / decoration     |      | manage/render lifecycle      |
+----------------------------+      +------------------------------+
```

The diagram deliberately does **not** place a generic `BackendOps` object between
Box and the two runtimes.

## 5. Semantic mapping contract

The table below is the main Phase 0A output. Phase 0B should be reviewed against
it row by row.

| Box concept | V2.3 X11 expression | river expression | Phase 0B ownership |
| --- | --- | --- | --- |
| managed client identity | `Window` XID | `river_window_v1` / stable identifier | runtime attachment; never core identity |
| client lifetime | MapRequest/adoption, Destroy/Unmap withdrawal | `window`, `closed`, object destroy | frontend observes; core adopts/removes semantic client |
| workspace membership | Box linked lists | no river concept | **Core** |
| per-monitor active workspace | Box state | no river concept | **Core** |
| FREE/MONOCLE mode | Box state | no river concept | **Core** |
| stable/tab order | Box linked list | no river concept | **Core** |
| focus history | Box linked list | no river concept | **Core** |
| semantic stack order | Box linked list | render-node ordering | **Core order**, frontend projection |
| semantic selected monitor | `WMModel.selected_monitor` | no direct river concept | **Core** |
| semantic focused client | `WMModel.focused_client` | seat focus request | **Core**, with runtime focus suppression noted below |
| focusability | `WM_HINTS` input + `WM_TAKE_FOCUS` | logical toplevels generally focusable | backend-observed semantic capability in Core; raw protocol facts stay frontend |
| desired visibility | `client_should_be_mapped()` | `show` / `hide` | **derived Core policy**, frontend projection |
| current mapped/rendered state | X mapped state + `mapped` | compositor lifecycle/show-hide/render sequence | runtime only |
| persistent client geometry | `Client.geometry` | policy-owned target position/dimensions | **Core** |
| actual presented dimensions | usually close to X configure result | asynchronous `dimensions` event, may differ from proposal | runtime observation only |
| normal restore geometry | `Client.normal_geometry` | no river concept | **Core** |
| snap state | Box state + X configure | position + proposed dimensions | **Core policy**, frontend projection |
| maximize state | Box state + EWMH + workarea geometry | `inform_maximized` plus WM-owned position/size | **Core policy**, frontend projection |
| real fullscreen | EWMH + monitor-sized X projection | `fullscreen(output)` + `inform_fullscreen` | **Core resolved mode**, backend-specific projection |
| fake fullscreen | EWMH-visible client request without real full monitor occupation | `inform_fullscreen` without compositor `fullscreen` | **Core policy**, backend-specific projection |
| client fullscreen request | `_NET_WM_STATE` request | `fullscreen_requested` | frontend observation into Core policy |
| title | `_NET_WM_NAME` / WM_NAME | `title` event | portable Core metadata |
| application identity | WM_CLASS class/instance | `app_id` | optional metadata fields; do not invent a single universal identifier |
| parent/dialog relation | `WM_TRANSIENT_FOR` | `parent` window event | raw ID frontend; semantic parent relation may be Core |
| X11 window type | `_NET_WM_WINDOW_TYPE` | no equivalent general enum | X11 compatibility metadata; do not force river into this enum |
| urgency / attention | XUrgencyHint | no current rwm-v5 equivalent | optional Core state; river parity gap for now |
| size hints | WM_NORMAL_HINTS incl. increments/aspect/min/max | `dimensions_hint` only min/max, actual size may differ | raw hints frontend; Core may consume normalized min/max capability only where policy needs it |
| monitor geometry | RandR logical monitor observation | `river_output_v1.position/dimensions` | **Core fact**, frontend observation |
| monitor identity continuity | RandR metadata matching | river object lifetime / removal | frontend resolves identity; Core applies topology consequences |
| workarea | docks/struts + Box native bar | layer-shell `non_exclusive_area` | **Core fact**, frontend observation |
| native status bar | Box X override-redirect bar | external Waybar/Quickshell preferred | X11-only; not migrated |
| tray | XEmbed | external StatusNotifier ecosystem | X11-only; not migrated |
| MONOCLE tab semantics | Box stable order/focus + reserved content geometry | same Box semantics | **Core** |
| MONOCLE tab pixels/input | X override-redirect/Xft | future `river_shell_surface_v1` | frontend-specific; Phase 4 for river |
| FREE SSD decoration | sibling X window/Xft | optional river decoration surface/SSD | X11 frontend initially; river deferred |
| interactive move/resize | X pointer grabs and root deltas | `river_seat_v1.op_start_pointer` + deltas | semantic drag action shared; input lifecycle frontend-specific |
| client-requested CSD move/resize | X CSD generally sends configure-like behavior externally | `pointer_move_requested` / `pointer_resize_requested` | river frontend event into existing move/resize policy |
| key/mouse bindings | X grabs / KeySym | river XKB binding protocol | not Core model; normalize later without contaminating Phase 0B |
| XWayland support | Box directly manages X11 | river exposes XWayland as logical river windows | river/compositor responsibility; Box river frontend stays protocol-level |

## 6. Binding decisions for Phase 0B

The decisions in this section are normative for Phase 0B unless implementation
work uncovers a contradiction. If that happens, update this document before
creating a broader abstraction.

### D1. Keep one repository and one Box semantic model

Do not create `box2430-wayland` as an independent project. X11 and river should
be two runtimes around the same policy model.

**Benefit:** feature semantics and tests do not silently drift into two WMs.
**Cost:** source/build layout becomes slightly more complex once Phase 1 starts.

### D2. River is the first Wayland runtime, not the definition of Core

Core must contain no `river_*`, `wl_*`, wlroots, or Wayland protocol objects.
Likewise it must contain no Xlib/Xft/RandR objects.

The test for a Core concept is not "both protocols have an object with this
name". The test is "Box2430 needs this fact or policy even if the display-system
mechanism changes".

### D3. Do not build a giant `BackendOps` vtable

Do not introduce an interface such as:

```c
backend->show(client);
backend->hide(client);
backend->focus(client);
backend->move_resize(client, rect);
backend->raise(client);
```

as the central Core API.

X11 allows ordered immediate requests inside an event transition. River requires
manage/render sequence discipline and asynchronous size negotiation. Making
Core issue protocol-like operations one by one would encode X11's cadence into
the supposedly neutral API.

Instead:

1. semantic transitions mutate coherent Core authority;
2. desired presentation is derived from that authority;
3. each frontend materializes it at a cadence legal for that frontend.

X11 may continue to interleave authority and projection where observable X11
ordering requires it. River will later coalesce projection at manage/render
boundaries. TAP therefore remains a discipline, not a generic transaction
engine.

### D4. `mapped` is not Core visibility

`Client.mapped` and `ignored_unmaps` are X11 runtime attachment state and must
leave the Core client.

The portable question is already present in V2.3:

```text
should this semantic client be presented now?
```

It is derived from active workspace, FREE/MONOCLE mode, and MONOCLE focus
target. Keep this derivation in Core. X11 maps/unmaps; river later shows/hides.

**Benefit:** inactive workspace and MONOCLE semantics become truly protocol
independent.
**Cost:** debugging must distinguish desired presentation from actual runtime
state.

### D5. Explicitly distinguish persistent geometry from actual presentation

Do not redefine `Client.geometry` to mean "the rectangle the compositor is
showing right now". It already does not mean that during MONOCLE or real
fullscreen.

The Core contract is:

* the stored client geometry is persistent **windowed geometry authority** for
  ordinary FREE/snap/maximized state;
* `normal_geometry` is the restore target used around snap/maximize;
* MONOCLE, fullscreen, workarea changes, and other transitions derive a current
  presentation target without necessarily overwriting persistent geometry;
* the frontend owns observed/current protocol presentation facts.

A rename such as `windowed_geometry` is allowed in Phase 0B if it materially
clarifies this invariant, but it is not required merely for naming purity.

This distinction is mandatory because river's proposed dimensions are not the
actual dimensions. A client may choose a nearby size, and river reports the
actual content dimensions later in a render sequence.

### D6. Real fullscreen is a semantic mode, not `geometry == monitor`

Keep the existing distinction among:

* user-requested fullscreen;
* client-requested fullscreen;
* per-client allow/fake/deny policy;
* resolved real fullscreen state.

Do not encode real fullscreen as only a monitor-sized `Rect` in shared Core
projection logic. On river, real fullscreen must use compositor fullscreen;
while fullscreen, river ignores normal position and proposed-dimension requests.

The existing fake-fullscreen policy maps especially well:

```text
real fullscreen:
    client is informed fullscreen
    + compositor owns fullscreen placement

fake fullscreen:
    client is informed fullscreen
    + ordinary Box placement remains in force
```

X11 remains free to materialize these semantics using EWMH and geometry as it
does today.

### D7. Keep Box single-logical-seat for the migration

`WMModel.focused_client` and `selected_monitor` remain single interaction
authorities. Phase 0B must not grow a general `Seat[]` model because river can
expose multiple seats.

The first river frontend will support one logical/primary seat. Multi-seat can
be a future architecture change if a real use case appears.

**Benefit:** preserves the current mental model and avoids spreading per-seat
focus through every policy path.
**Cost:** first Wayland version intentionally does not expose river's full
multi-seat capability.

### D8. Define semantic focus separately from actual keyboard receiver

`focused_client` means "the client Box regards as the current window focus and
focus-history target". It must not be documented as an unconditional statement
that this client is receiving keyboard input at this instant.

This distinction is required by layer-shell. `river-layer-shell-v1` can give a
layer surface exclusive keyboard focus and temporarily ignore WM requests to
focus windows. When layer-shell focus ends, Box should be able to restore its
semantic focused client without losing focus history.

For X11, the distinction normally collapses and current input-focus projection
can remain unchanged.

### D9. Normalize focus capability, not X11 focus protocols

`accepts_input` and `takes_focus` are raw ICCCM facts and belong to the X11
attachment. Core focus fallback still needs a yes/no answer, so Core may keep a
normalized semantic capability such as `focusable` that the frontend updates.

For X11:

```text
WM_HINTS InputHint / WM_TAKE_FOCUS -> normalized focusable
```

For river, managed logical toplevels can initially be considered focusable
unless a later protocol fact requires otherwise.

Do not carry `WM_TAKE_FOCUS` concepts into river code.

### D10. Backend observes monitor identity; Core owns topology consequences

Do not invent a universal monitor identifier structure merely to hide RandR.
The two runtimes have different continuity mechanisms:

* X11 currently needs geometry plus RandR metadata/output identity matching;
* river has explicit `river_output_v1` object lifetime and `removed` events.

The frontend should therefore resolve old/new logical-output continuity and pass
a topology change in protocol-neutral terms to Core. Core then owns the Box
consequences:

* selected-monitor continuity/fallback;
* workspace ownership continuity;
* client migration if an output disappears;
* translation/clamping of persistent client geometry;
* focus fallback after migration.

The existing geometry/client-migration portion of `plan_monitor_topology()` is
the main code to preserve. The RandR matching input to that plan remains an X11
concern.

### D11. `Monitor.workarea` stays Core; struts do not

`Monitor.geometry` and `Monitor.workarea` are useful Box facts and remain in
Core.

Their source is frontend-specific:

```text
X11:
    RandR logical monitor geometry
    + EWMH dock struts
    + optional Box native X11 bar reservation
    -> Core monitor geometry/workarea

river:
    river_output_v1 geometry
    + river-layer-shell-v1 non_exclusive_area
    -> Core monitor geometry/workarea
```

`SpecialWindow`, dock XIDs, and strut arrays therefore leave `WMModel`.

This decision also preserves snap/maximize/MONOCLE code: those policies continue
to consume `monitor->workarea` without knowing why it has that rectangle.

One river-specific operational consequence is important: binding
`river-layer-shell-v1` is how the external WM declares layer-shell support. If
the WM does not bind it, the compositor is expected not to allow layer-shell
clients to map. Therefore the river runtime must support this protocol early
even though Box will not port its native bar. This is what allows external
Waybar/Quickshell-style shell components to coexist with Box and is also how
Box receives the non-exclusive workarea hint.

### D12. Keep metadata heterogeneous instead of inventing universal app identity

Portable metadata should be allowed to coexist with backend-specific optional
facts.

Expected direction:

```text
title                 portable
app_id                Wayland/river-oriented optional field
class_name            X11 compatibility optional field
instance              X11 compatibility optional field
parent Client *       portable semantic relation where known
window_type           retained for X11 rule compatibility, not required of river
```

Do not map `app_id` into `class_name` merely so an old rule structure looks
universal. Phase 1/5 can extend rule syntax deliberately.

Similarly, an X11 transient XID is runtime metadata. If Box needs the relation
for placement or stacking, resolve it to a semantic `Client *parent` rather than
keeping the XID in Core.

### D13. Treat urgency as optional semantic state with a known river parity gap

Urgency is useful Box state: it affects tabs, workspace labels, borders, and
focus policy. Keep the semantic `urgent` flag.

However, current river-window-management-v1 v5 does not expose an equivalent of
XUrgencyHint / xdg-activation requests to the external WM. River's current
server source still contains a TODO for forwarding `xdg_activation_v1.activate`
to the external WM protocol.

Therefore the first river frontend may never set `urgent == true` from client
attention requests. This is a feature-parity gap, not a reason to contaminate
Core or invent a private protocol.

### D14. Keep size-hint mechanics frontend-specific

Current X11 `WM_NORMAL_HINTS` contains more than river exposes: base size,
increments, aspect bounds, and min/max dimensions. River v5 exposes preferred
min/max dimensions and explicitly allows a window to choose actual dimensions
that differ from a proposal.

Do not place raw X11 size-hint structures in Core. Policy may consume a small
normalized capability where required, but Phase 0B should avoid designing a
"universal size hints" structure that promises parity which does not exist.

In particular, actual river dimensions must stay runtime observation state.

### D15. MONOCLE tab behavior is Core; tab rendering is frontend

The following remain shared semantics:

* stable tab ordering;
* active tab = semantic focus target for MONOCLE;
* conditions under which the tab strip exists;
* the fact that a visible tab strip reserves part of MONOCLE content area;
* actions such as selecting a tab.

The following are frontend implementation:

* X11 tab Window/Xft resources;
* font/render buffers;
* protocol surface lifetime;
* pixel-level pointer events and hit rectangles where those depend on rendering.

The river renderer is deferred to Phase 4 and is expected to use a
`river_shell_surface_v1`. Phase 0B only needs to prevent X11 tab objects from
living inside shared `Monitor` state.

### D16. Native bar, tray, and first-generation SSD are not migration blockers

The native status/workspace bar and XEmbed tray stay X11-only. The river desktop
will use external shell components such as Quickshell/Waybar.

FREE-mode X11 SSD remains supported on X11 but is not required for the first
river version. River defaults to CSD if the external WM does not request SSD, so
Phase 1 can intentionally rely on CSD.

Do not force native bar/tray/decoration abstractions into Core simply because
those features exist in V2.3.

### D17. Do not redesign input/config/IPC in Phase 0B unless needed to free Core

`KeySym`, X modifier masks, X event timestamps, and mouse-grab details are not
Core model concepts. Eventually the X11 and river runtimes will both feed the
same semantic commands, but Phase 0B does not need to solve every binding
representation immediately.

A practical extraction may leave configuration and command orchestration close
to the X11 runtime while moving pure model types into an X11-free header.

Likewise `BspwmCompat` remains an optional interoperability adapter outside Core.
Do not make Phase 0B a redesign of IPC.

## 7. Refined TAP model for two runtimes

The existing TAP language should remain, with one clarification: "projection"
does not imply a single immediate function call or a shared render pass.

### X11

```text
X event
  -> semantic transition
  -> authority mutation
  -> ordered X11 projection where required
  -> possibly more authority/projection in the same transition
```

This remains legal because X11 protocol ordering is sometimes itself observable.
Workspace switching is the clearest example: V2.3 maps the incoming MONOCLE
client before focus projection and retires outgoing mapping before later UI and
stacking publication.

### river

The future runtime instead looks like:

```text
river observations
  -> manage_start
  -> semantic transitions / coherent Box authority
  -> window-management requests derived from authority
  -> manage_finish
  -> compositor/client negotiation
  -> dimensions observations
  -> render_start
  -> rendering requests derived from authority + actual dimensions
  -> render_finish
```

Core must not need to know which sequence is currently open. The river runtime
owns that legality.

This is why Phase 0B should extract authority/policy helpers but should not try
to replace `wm.c` with a generic cross-backend event loop.

## 8. Presentation intent: concept, not mandatory framework

It is useful during review to ask what presentation the Core currently desires
for a client:

```text
visible?
semantic stack position?
windowed target rectangle?
real fullscreen on which monitor?
maximized/fullscreen state communicated to client?
focused by Box?
border/decor policy?
```

Phase 0B may express these through focused pure helpers rather than one large
`PresentationIntent` struct. This document intentionally does not mandate a
snapshot/reducer architecture.

The requirement is only that frontend projection can derive these facts from
Core authority without reading X11-only state to reconstruct Box policy.

## 9. Monitor topology split

Monitor hotplug deserves an explicit split because it is easy to over-generalize.

Current V2.3 roughly does:

```text
RandR snapshot
    -> RandR continuity matching
    -> topology plan
       - old/new rectangles
       - selected monitor continuity
       - client/workspace migration
       - latent geometry translation
    -> mutate model
    -> recreate/destroy X11 UI resources
    -> project geometry/focus/UI
```

Phase 0B target:

```text
X11 frontend                           river frontend (Phase 1)
--------------                         ------------------------
RandR snapshot                         river_output_v1 lifecycle
RandR identity matching                direct output-object continuity
           \                            /
            \ resolved logical topology /
             +--------------------------+
                         |
                         v
                 Box topology policy
                 -------------------
                 monitor continuity/fallback
                 workspace continuity
                 client migration
                 latent geometry translation/clamp
                 selected/focused authority repair
                         |
                         v
                 frontend projection/resources
```

This avoids two bad outcomes at once:

1. leaving client/workspace migration buried inside RandR code;
2. inventing a generic monitor-ID schema that is less natural than either
   backend's real lifecycle.

## 10. Client data target

Phase 0B does not need to freeze final C structures, but the shared client should
move toward the following responsibility shape:

```c
/* illustrative ownership only; not a required exact declaration */
typedef struct Client {
    Workspace *workspace;

    Rect geometry;          /* persistent windowed Box geometry authority */
    Rect normal_geometry;   /* restore geometry around snap/maximize */
    SnapState snap_state;

    /* Box orders */
    struct Client *next;
    struct Client *workspace_next;
    struct Client *tab_prev, *tab_next;
    struct Client *stack_prev, *stack_next;
    struct Client *focus_prev, *focus_next;

    bool focusable;         /* normalized backend-observed capability */
    bool urgent;            /* optional semantic attention state */
    bool maximized;
    bool fullscreen;        /* resolved real-fullscreen Box state */
    bool user_fullscreen;
    bool client_fullscreen;

    char *title;
    char *app_id;           /* optional */
    char *class_name;       /* optional X11 compatibility metadata */
    char *instance;         /* optional X11 compatibility metadata */
    struct Client *parent;  /* optional semantic relation */

    /* rule-derived Box policy that is meaningful across runtimes */
    bool border_enabled;
    ClientFullscreenPolicy fullscreen_policy;
    ClientDecorationPolicy decoration_policy;
} Client;
```

The exact handling of X11-only `WindowType` compatibility and decoration policy
can remain conservative in 0B. The important negative contract is that the
shared object does not own XIDs, Xft handles, Atoms, raw ICCCM facts, or river
protocol objects.

A natural X11 allocation pattern later is an embedding/owner object:

```c
struct X11Client {
    Client client;
    Window window;
    bool mapped;
    unsigned int ignored_unmaps;
    bool accepts_input;
    bool takes_focus;
    Window transient_for;
    /* size hints, decoration/Xft attachments, original X border, ... */
};
```

A future river owner can independently embed the same Core `Client` without
requiring a `void *backend_data` field in Core.

For monitors, where V2.3 relies on a contiguous model array and monitor count is
small, a separate runtime attachment table keyed by model monitor/index is
preferable to forcing a larger backend object into the Core array layout.

## 11. Tab bar contract

The native bar and tab bar must not be grouped together simply because both are
currently drawn by `ui.c`.

### Native bar

The native bar is desktop-shell UI. It is not required by the Box window model
and will not be ported in the planned river runtime.

### MONOCLE tab bar

The tab bar is part of the MONOCLE interaction model because:

* the stable tab order is semantic Box state;
* selecting a tab selects a managed client;
* tab visibility changes which client is presented in MONOCLE;
* tab strip height changes the MONOCLE client content rectangle.

Therefore Phase 0B should preserve a renderer-independent notion of the MONOCLE
content area with tab reservation, while moving X Window/Xft materialization out
of `Monitor`.

The future river tab bar should be a WM shell surface so its surface commit can
be synchronized with river render state. That is a Phase 4 implementation
choice, not a Phase 0B dependency.

## 12. Known river parity gaps and deliberate deferrals

These are known differences, not blockers.

### 12.1 Attention / urgency

Current rwm v5 has no client-attention event equivalent to XUrgencyHint, and
river currently has an unresolved TODO around forwarding xdg-activation requests
to the external WM. River Box may therefore lack application-driven urgency at
first.

### 12.2 X11 rule identity is richer/different

Wayland's `app_id` is not WM_CLASS class/instance. X11 window-type enums also do
not map directly to river logical windows. Phase 0B must preserve X11 behavior
without pretending these metadata systems are identical. Phase 5 can define the
user-facing Wayland rule vocabulary.

### 12.3 Size hints are not identical

River offers min/max dimension hints but not the full ICCCM base/increment/aspect
model. Moreover actual dimensions are negotiated asynchronously. Exact X11
WM_NORMAL_HINTS behavior must therefore stay an X11 feature unless a portable
subset is deliberately defined later.

### 12.4 Multi-seat

River is natively per-seat; Box migration deliberately remains one logical seat.

### 12.5 Native bar / tray / SSD

No river native bar or tray is planned. River SSD is deferred until after the
core FREE/MONOCLE behavior is usable; CSD is the initial policy.

## 13. Phase 0B required work

Phase 0B is an X11-preserving refactor. It should contain no river runtime code.

### Must do

1. Create an X11-free Core/model header boundary.
2. Move X11/Xft protocol attachments out of shared `Client` and `Monitor` state.
3. Remove `SpecialWindow`/strut ownership from `WMModel` while preserving the
   resulting `Monitor.workarea` authority.
4. Preserve workspace membership, stable/tab order, stack order, focus history,
   selected monitor, semantic focus, FREE/MONOCLE policy, snap/maximize and
   fullscreen policy as Core concepts.
5. Make desired client visibility derivable without X mapped state.
6. Make monitor topology consequences consume protocol-neutral rectangles and a
   frontend-resolved continuity mapping rather than a RandR snapshot directly.
7. Preserve persistent versus temporary/presented geometry semantics.
8. Define or expose a normalized focusability fact without leaking ICCCM into
   Core focus selection.
9. Remove X Window/Xft tab resources from shared `Monitor` while preserving tab
   order and MONOCLE content-area semantics.
10. Keep all existing X11 behavior and Xvfb contracts unchanged unless a test
    itself encodes an implementation detail being intentionally moved.
11. Add at least a small Core-only test target that can compile without X11/Xft
    development headers. This is an architectural acceptance test, not only a
    convenience.

### May do if it simplifies the extraction

* split `src/box2430.h` into a pure model header plus the existing X11 runtime
  umbrella;
* rename geometry fields if the new name materially clarifies persistent versus
  presented state;
* introduce small pure helpers for visibility, focus fallback, topology
  consequences, or presentation policy;
* use an embedding `X11Client { Client core; ... }` owner object;
* use small X11 monitor attachment records for native bar/tab resources.

### Must not do

* add Wayland/river build dependencies;
* implement `river_window_v1` handling;
* fork or vendor river;
* add a generic plugin system;
* add a broad `BackendOps` vtable;
* make Box multi-seat;
* port the native bar, tray, or Wayland SSD;
* redesign the entire config/command/IPC subsystem merely for architectural
  symmetry;
* weaken existing X11 ordering solely to make the code look more backend-neutral.

## 14. Phase 0B acceptance criteria

Phase 0B is complete when all of the following are true:

1. **Core isolation:** pure Box model/policy headers and their unit tests compile
   without X11, Xft, RandR, Wayland, wlroots, or river headers.
2. **No protocol objects in authority:** XIDs/Xft handles/Atoms/RandR metadata and
   future river objects are outside shared `Client`, `Monitor`, and `WMModel`.
3. **Same Box semantics:** FREE/MONOCLE, per-monitor workspaces, semantic focus,
   stack/stable/focus orders, snap/maximize/fullscreen policy, and tab semantics
   remain represented in Core.
4. **X11 regression preservation:** the existing test suite should pass in an
   environment with its current X11 dependencies. Any sandbox dependency
   limitation may defer execution to the real development machine, but the
   patch must not intentionally reduce coverage.
5. **River-readiness audit:** every future Phase 1 requirement in the mapping
   table has a natural place to attach without adding X11 concepts back into
   Core.
6. **No framework tax:** the code remains direct C. The extraction should make
   ownership more obvious, not replace it with indirection for its own sake.

## 15. Phase 1 assumptions created by this contract

If Phase 0B satisfies the contract, Phase 1 can be intentionally small. It only
needs to prove that another runtime can drive the same authority:

```text
connect to Wayland
bind river-window-management-v1
bind river-layer-shell-v1 sufficiently to permit external shell clients and
consume output workarea/focus observations
observe output/window/seat lifecycle
create Core Monitor/Workspace/Client state
enter manage sequences
project basic focus/dimensions policy
consume dimensions events
enter render sequences
project position/visibility/stack order
```

Phase 1 does **not** need a native bar, tray, SSD, or complete MONOCLE tab
renderer to validate the architecture.

The most important Phase 1 review question will be:

> Does the river runtime consume Box authority naturally, or does it repeatedly
> need to reconstruct/override policy because Phase 0B accidentally preserved
> an X11 assumption?

If the latter occurs, fix the smallest authority boundary rather than growing a
backend framework around the mistake.

## 16. Final architectural position

After Phase 0B, the project should be understood as:

```text
                         Box2430 Core
              policy / authority / invariants
                              |
              +---------------+----------------+
              |                                |
         X11 runtime                    river runtime (Phase 1+)
    Xlib/ICCCM/EWMH/RandR             Wayland protocol client
    native X11 UI / XEmbed             manage/render sequences
```

The Core is not a compositor abstraction and not a window-system SDK. It is the
part of Box2430 that explains why the WM behaves like Box2430:

* per-monitor workspace ownership;
* FREE/MONOCLE dual model;
* semantic focus and focus history;
* stable tab order and stack order;
* persistent geometry, snap/maximize/fullscreen policy;
* monitor/workarea-aware policy;
* rules and interaction semantics where the underlying facts exist.

Everything else is allowed to remain honestly backend-specific.
