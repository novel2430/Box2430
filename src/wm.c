#include "box2430.h"
#include "bspwm_compat.h"
#include "ui.h"
#include "tray.h"

#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xcursor/Xcursor.h>
#include <errno.h>
#include <fnmatch.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stop_requested;

static Cursor load_cursor(WM *wm, const char *name, const char *fallback)
{
    Cursor cursor = XcursorLibraryLoadCursor(wm->display, name);
    if (cursor == None && fallback)
        cursor = XcursorLibraryLoadCursor(wm->display, fallback);
    return cursor;
}

static bool init_cursors(WM *wm)
{
    wm->cursor_normal = load_cursor(wm, "left_ptr", NULL);
    wm->cursor_move = load_cursor(wm, "fleur", NULL);
    wm->cursor_resize = load_cursor(wm, "se-resize", "bottom_right_corner");
    if (wm->cursor_normal == None || wm->cursor_move == None ||
        wm->cursor_resize == None) {
        fprintf(stderr, "box2430: cannot load Xcursor theme cursors\n");
        if (wm->cursor_normal != None) XFreeCursor(wm->display, wm->cursor_normal);
        if (wm->cursor_move != None) XFreeCursor(wm->display, wm->cursor_move);
        if (wm->cursor_resize != None) XFreeCursor(wm->display, wm->cursor_resize);
        wm->cursor_normal = None;
        wm->cursor_move = None;
        wm->cursor_resize = None;
        return false;
    }
    XDefineCursor(wm->display, wm->root, wm->cursor_normal);
    return true;
}

static void free_cursors(WM *wm)
{
    if (wm->cursor_normal != None) XFreeCursor(wm->display, wm->cursor_normal);
    if (wm->cursor_move != None) XFreeCursor(wm->display, wm->cursor_move);
    if (wm->cursor_resize != None) XFreeCursor(wm->display, wm->cursor_resize);
    wm->cursor_normal = None;
    wm->cursor_move = None;
    wm->cursor_resize = None;
}

static int ignore_x11_error(Display *display, XErrorEvent *event)
{
    (void)display;
    (void)event;
    return 0;
}

static void enforce_stacking(WM *wm);
static void recompute_workareas(WM *wm);
static void apply_normal_hints(WM *wm, Client *client, int *width, int *height);
static void materialize_client_geometry(WM *wm, Client *client);
static void grab_client_buttons(WM *wm, Client *client, bool focused);
static void project_client_mapped(WM *wm, Client *client);
static void project_client_unmapped(WM *wm, Client *client);
static bool client_workspace_is_active(const Client *client);
static bool client_should_be_mapped(const Client *client);
static void reconcile_client_mapping(WM *wm, Client *client);
static void reconcile_workspace_mapping(WM *wm, Workspace *workspace);
static void translate_client_latent_geometry(Client *client, Rect old_monitor,
                                             Rect new_monitor);
static void clamp_client_latent_geometry(WM *wm, Client *client, Rect workarea);

#ifndef NDEBUG
static void invariant_failure(const char *message)
{
    fprintf(stderr, "box2430: semantic invariant failed: %s\n", message);
    abort();
}

static bool monitor_belongs_to_model(const WMModel *model,
                                     const Monitor *monitor)
{
    if (!monitor) return false;
    for (unsigned int i = 0; i < model->monitor_count; ++i)
        if (&model->monitors[i] == monitor) return true;
    return false;
}

static bool workspace_belongs_to_model(const WMModel *model,
                                       unsigned int workspace_count,
                                       const Workspace *workspace)
{
    if (!workspace) return false;
    for (unsigned int i = 0; i < model->monitor_count; ++i) {
        const Workspace *workspaces = model->monitors[i].workspaces;
        if (!workspaces) continue;
        for (unsigned int j = 0; j < workspace_count; ++j)
            if (&workspaces[j] == workspace) return true;
    }
    return false;
}

static unsigned int global_client_occurrences(const WMModel *model,
                                              const Client *target)
{
    unsigned int count = 0;
    for (const Client *client = model->clients; client; client = client->next)
        if (client == target) ++count;
    return count;
}

static unsigned int membership_occurrences(const Workspace *workspace,
                                           const Client *target,
                                           unsigned int limit)
{
    unsigned int count = 0;
    unsigned int visited = 0;
    for (const Client *client = workspace->clients; client;
         client = client->workspace_next) {
        if (++visited > limit)
            invariant_failure("workspace membership contains a cycle");
        if (client == target) ++count;
    }
    return count;
}

static unsigned int tab_occurrences(const Workspace *workspace,
                                    const Client *target, unsigned int limit)
{
    unsigned int count = 0;
    unsigned int visited = 0;
    for (const Client *client = workspace->tab_head; client;
         client = client->tab_next) {
        if (++visited > limit)
            invariant_failure("workspace stable order contains a cycle");
        if (client == target) ++count;
    }
    return count;
}

static unsigned int stack_occurrences(const Workspace *workspace,
                                      const Client *target, unsigned int limit)
{
    unsigned int count = 0;
    unsigned int visited = 0;
    for (const Client *client = workspace->stack_head; client;
         client = client->stack_next) {
        if (++visited > limit)
            invariant_failure("workspace stack order contains a cycle");
        if (client == target) ++count;
    }
    return count;
}

static unsigned int focus_occurrences(const Workspace *workspace,
                                      const Client *target, unsigned int limit)
{
    unsigned int count = 0;
    unsigned int visited = 0;
    for (const Client *client = workspace->focus_head; client;
         client = client->focus_next) {
        if (++visited > limit)
            invariant_failure("workspace focus history contains a cycle");
        if (client == target) ++count;
    }
    return count;
}

static unsigned int checked_global_client_count(const WMModel *model)
{
    const Client *slow = model->clients;
    const Client *fast = model->clients;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
        if (slow == fast) invariant_failure("global client list contains a cycle");
    }

    unsigned int count = 0;
    for (const Client *client = model->clients; client; client = client->next)
        ++count;
    return count;
}

static void check_workspace_invariants(const WMModel *model,
                                       const Workspace *workspace,
                                       unsigned int global_count)
{
    unsigned int membership_count = 0;
    for (const Client *client = workspace->clients; client;
         client = client->workspace_next) {
        if (++membership_count > global_count)
            invariant_failure("workspace membership contains a cycle or duplicate");
        if (client->workspace != workspace)
            invariant_failure("workspace membership contains a foreign client");
        if (global_client_occurrences(model, client) != 1)
            invariant_failure("workspace client is not globally owned exactly once");
        if (membership_occurrences(workspace, client, global_count) != 1)
            invariant_failure("workspace membership contains a duplicate client");
        if (tab_occurrences(workspace, client, global_count) != 1)
            invariant_failure("workspace stable order differs from membership");
        if (stack_occurrences(workspace, client, global_count) != 1)
            invariant_failure("workspace stack order differs from membership");
    }

    unsigned int tab_count = 0;
    const Client *tab_previous = NULL;
    for (const Client *client = workspace->tab_head; client;
         client = client->tab_next) {
        if (++tab_count > global_count)
            invariant_failure("workspace stable order contains a cycle");
        if (client->tab_prev != tab_previous)
            invariant_failure("workspace stable order has inconsistent links");
        if (membership_occurrences(workspace, client, global_count) != 1)
            invariant_failure("stable-order client is not a workspace member");
        tab_previous = client;
    }
    if (tab_previous != workspace->tab_tail)
        invariant_failure("workspace stable-order tail is inconsistent");
    if (tab_count != membership_count)
        invariant_failure("workspace stable-order size differs from membership");

    unsigned int stack_count = 0;
    const Client *stack_previous = NULL;
    for (const Client *client = workspace->stack_head; client;
         client = client->stack_next) {
        if (++stack_count > global_count)
            invariant_failure("workspace stack order contains a cycle");
        if (client->stack_prev != stack_previous)
            invariant_failure("workspace stack order has inconsistent links");
        if (membership_occurrences(workspace, client, global_count) != 1)
            invariant_failure("stack-order client is not a workspace member");
        stack_previous = client;
    }
    if (stack_previous != workspace->stack_tail)
        invariant_failure("workspace stack-order tail is inconsistent");
    if (stack_count != membership_count)
        invariant_failure("workspace stack-order size differs from membership");

    unsigned int focus_count = 0;
    const Client *focus_previous = NULL;
    for (const Client *client = workspace->focus_head; client;
         client = client->focus_next) {
        if (++focus_count > membership_count)
            invariant_failure("workspace focus history contains a cycle or duplicate");
        if (client->focus_prev != focus_previous)
            invariant_failure("workspace focus history has inconsistent links");
        if (membership_occurrences(workspace, client, global_count) != 1)
            invariant_failure("focus-history client is not a workspace member");
        if (focus_occurrences(workspace, client, membership_count) != 1)
            invariant_failure("workspace focus history contains a duplicate client");
        focus_previous = client;
    }
    if (focus_previous != workspace->focus_tail)
        invariant_failure("workspace focus-history tail is inconsistent");
}

static void wm_check_invariants(const WM *wm)
{
    const WMModel *model = &wm->model;
    unsigned int workspace_count = wm->config.workspace_count;
    if (!model->monitors || model->monitor_count == 0)
        invariant_failure("WM has no monitor authority");
    if (!wm->monitor_snapshot.monitors ||
        wm->monitor_snapshot.count != model->monitor_count)
        invariant_failure("RandR snapshot and monitor authority counts differ");
    if (!monitor_belongs_to_model(model, model->selected_monitor))
        invariant_failure("selected monitor is outside WM monitor authority");

    unsigned int global_count = checked_global_client_count(model);
    for (unsigned int i = 0; i < model->monitor_count; ++i) {
        const Monitor *monitor = &model->monitors[i];
        const Rect observed = wm->monitor_snapshot.monitors[i].geometry;
        if (!monitor->workspaces)
            invariant_failure("monitor has no workspace authority");
        if (monitor->index != i)
            invariant_failure("monitor index does not match its authority slot");
        if (monitor->geometry.x != observed.x ||
            monitor->geometry.y != observed.y ||
            monitor->geometry.width != observed.width ||
            monitor->geometry.height != observed.height)
            invariant_failure("RandR snapshot geometry differs from monitor authority");
        if (!workspace_belongs_to_model(model, workspace_count,
                                        monitor->active_workspace) ||
            monitor->active_workspace->monitor != monitor)
            invariant_failure("active workspace does not belong to its monitor");
        for (unsigned int j = 0; j < workspace_count; ++j) {
            const Workspace *workspace = &monitor->workspaces[j];
            if (workspace->monitor != monitor || workspace->index != j)
                invariant_failure("workspace owner or index is inconsistent");
            check_workspace_invariants(model, workspace, global_count);
        }
    }

    for (const Client *client = model->clients; client; client = client->next) {
        if (!workspace_belongs_to_model(model, workspace_count, client->workspace))
            invariant_failure("globally owned client has no valid workspace");
        if (membership_occurrences(client->workspace, client, global_count) != 1)
            invariant_failure("globally owned client is not owned by one workspace");
        if (client->snap_state != SNAP_NONE && client->maximized)
            invariant_failure("client is both snapped and maximized");
    }

    if (model->focused_client) {
        const Client *focused = model->focused_client;
        if (global_client_occurrences(model, focused) != 1)
            invariant_failure("focused client is not globally owned");
        if (focused->workspace != focused->workspace->monitor->active_workspace)
            invariant_failure("focused client is not semantically visible");
        if (focused->workspace->monitor != model->selected_monitor)
            invariant_failure("focused client and selected monitor disagree");
        /* WM_HINTS/WM_PROTOCOLS observations intentionally do not reactively
         * move an already established semantic focus. A later focus
         * transition still filters through client_can_focus(). */
    }
}

#define WM_CHECK_INVARIANTS(wm) wm_check_invariants(wm)
#else
#define WM_CHECK_INVARIANTS(wm) ((void)0)
#endif

static void handle_signal(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static unsigned long named_color(WM *wm, const char *name, unsigned long fallback)
{
    XColor color;
    XColor exact;
    if (XAllocNamedColor(wm->display, DefaultColormap(wm->display, wm->screen),
                         name, &color, &exact)) {
        return color.pixel;
    }
    return fallback;
}

static bool init_monitor_state(WM *wm, Monitor *monitor, unsigned int index,
                               Rect geometry)
{
    memset(monitor, 0, sizeof(*monitor));
    monitor->workspaces = calloc(wm->config.workspace_count,
                                  sizeof(*monitor->workspaces));
    if (!monitor->workspaces) return false;
    monitor->index = index;
    monitor->geometry = geometry;
    monitor->workarea = geometry;
    for (unsigned int j = 0; j < wm->config.workspace_count; ++j) {
        monitor->workspaces[j].monitor = monitor;
        monitor->workspaces[j].index = j;
        monitor->workspaces[j].mode = WORKSPACE_FREE;
    }
    monitor->active_workspace = &monitor->workspaces[0];
    return true;
}

static bool init_monitors(WM *wm)
{
    RandRMonitorSnapshot snapshot = {0};
    if (!randr_query_monitor_snapshot(wm, &snapshot)) return false;
    wm->model.monitors = calloc(BOX2430_MAX_MONITORS, sizeof(*wm->model.monitors));
    if (!wm->model.monitors) {
        fprintf(stderr, "box2430: out of memory creating monitor state\n");
        randr_free_monitor_snapshot(&snapshot);
        return false;
    }
    wm->model.monitor_count = snapshot.count;
    for (unsigned int i = 0; i < snapshot.count; ++i) {
        if (!init_monitor_state(wm, &wm->model.monitors[i], i,
                                snapshot.monitors[i].geometry)) {
            fprintf(stderr, "box2430: out of memory creating workspace state\n");
            for (unsigned int j = 0; j < i; ++j) free(wm->model.monitors[j].workspaces);
            free(wm->model.monitors);
            wm->model.monitors = NULL;
            wm->model.monitor_count = 0;
            randr_free_monitor_snapshot(&snapshot);
            return false;
        }
    }
    wm->model.selected_monitor = &wm->model.monitors[0];
    wm->monitor_snapshot = snapshot;
    return true;
}

static Client *find_client(const WMModel *model, Window window)
{
    for (Client *client = model->clients; client; client = client->next) {
        if (client->window == window) {
            return client;
        }
    }
    return NULL;
}

static SpecialWindow *find_special_window(const WMModel *model, Window window)
{
    for (SpecialWindow *special = model->special_windows; special; special = special->next)
        if (special->window == window) return special;
    return NULL;
}

static void discard_enter_events(WM *wm)
{
    XEvent event;
    XSync(wm->display, False);
    while (XCheckMaskEvent(wm->display, EnterWindowMask, &event)) {}
}

static void stack_relative(WM *wm, Window window, Window sibling, int mode)
{
    if (!window || !sibling || window == sibling) return;
    XWindowChanges changes = {
        .sibling = sibling,
        .stack_mode = mode,
    };
    XConfigureWindow(wm->display, window, CWSibling | CWStackMode, &changes);
}

static void append_native_stack(WM *wm, Window window, Window ceiling,
                                Window *base, Window *top)
{
    if (!window) return;
    if (!*base) {
        if (ceiling) stack_relative(wm, window, ceiling, Below);
        *base = window;
        *top = window;
        return;
    }
    stack_relative(wm, window, *top, Above);
    *top = window;
}

static void enforce_stacking_below(WM *wm, Window ceiling)
{
    ui_update(wm);

    /* Desktop windows are the only tier that deliberately belongs at the
     * absolute bottom.  Higher tiers are ordered relative to known siblings
     * so restacking Box2430 windows does not repeatedly jump over unrelated
     * override-redirect overlays such as dunst notifications. */
    for (SpecialWindow *special = wm->model.special_windows; special; special = special->next)
        if (special->type == WINDOW_TYPE_DESKTOP)
            XLowerWindow(wm->display, special->window);

    Window native_base = None;
    Window native_top = None;
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i)
        if (wm->config.bar.enabled && wm->model.monitors[i].bar)
            append_native_stack(wm, wm->model.monitors[i].bar, ceiling,
                                &native_base, &native_top);

    Window tray_host = tray_host_window(wm);
    if (tray_host)
        append_native_stack(wm, tray_host, ceiling,
                            &native_base, &native_top);

    for (unsigned int i = 0; i < wm->model.monitor_count; ++i)
        if (wm->model.monitors[i].tab_bar && ui_tabs_should_materialize(
                wm, wm->model.monitors[i].active_workspace))
            append_native_stack(wm, wm->model.monitors[i].tab_bar, ceiling,
                                &native_base, &native_top);

    /* stack_head -> stack_tail is bottom -> top.  Walk backwards from the
     * native-UI ceiling so ordinary clients remain below the bar/tab tier
     * without raising that tier to the top of the root stack. */
    Window upper = native_base;
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        Workspace *workspace = wm->model.monitors[i].active_workspace;
        for (Client *client = workspace->stack_tail; client; client = client->stack_prev) {
            if (client->fullscreen) continue;
            if (upper) {
                stack_relative(wm, client->window, upper, Below);
            } else {
                /* With no native UI there is no stable Box2430 sibling to
                 * anchor against, so preserve the existing raise semantics. */
                XRaiseWindow(wm->display, client->window);
            }
            upper = client->window;
        }
    }

    Window known_top = native_top;
    for (SpecialWindow *special = wm->model.special_windows; special; special = special->next) {
        if (special->type == WINDOW_TYPE_DESKTOP) continue;
        if (known_top) stack_relative(wm, special->window, known_top, Above);
        else XRaiseWindow(wm->display, special->window);
        known_top = special->window;
    }
    for (Client *client = wm->model.clients; client; client = client->next) {
        if (!client->fullscreen ||
            client->workspace != client->workspace->monitor->active_workspace)
            continue;
        if (known_top) stack_relative(wm, client->window, known_top, Above);
        else XRaiseWindow(wm->display, client->window);
        known_top = client->window;
    }
    discard_enter_events(wm);
}

static void enforce_stacking(WM *wm)
{
    enforce_stacking_below(wm, None);
}

static unsigned int tab_count(const Workspace *workspace)
{
    unsigned int count = 0;
    for (const Client *client = workspace->tab_head; client; client = client->tab_next)
        ++count;
    return count;
}

static void append_workspace_orders(Workspace *workspace, Client *client)
{
    client->workspace_next = workspace->clients;
    workspace->clients = client;

    client->tab_prev = workspace->tab_tail;
    if (workspace->tab_tail) {
        workspace->tab_tail->tab_next = client;
    } else {
        workspace->tab_head = client;
    }
    workspace->tab_tail = client;

    client->stack_prev = workspace->stack_tail;
    if (workspace->stack_tail) {
        workspace->stack_tail->stack_next = client;
    } else {
        workspace->stack_head = client;
    }
    workspace->stack_tail = client;
}

static void unlink_workspace_focus(Workspace *workspace, Client *client)
{
    bool linked = workspace->focus_head == client || workspace->focus_tail == client ||
        client->focus_prev || client->focus_next;
    if (!linked) return;

    if (client->focus_prev) client->focus_prev->focus_next = client->focus_next;
    else workspace->focus_head = client->focus_next;
    if (client->focus_next) client->focus_next->focus_prev = client->focus_prev;
    else workspace->focus_tail = client->focus_prev;
    client->focus_prev = NULL;
    client->focus_next = NULL;
}

static void promote_workspace_focus(Workspace *workspace, Client *client)
{
    unlink_workspace_focus(workspace, client);
    client->focus_prev = NULL;
    client->focus_next = workspace->focus_head;
    if (workspace->focus_head) workspace->focus_head->focus_prev = client;
    else workspace->focus_tail = client;
    workspace->focus_head = client;
}

static void unlink_workspace_orders(Workspace *workspace, Client *client)
{
    Client **link = &workspace->clients;
    while (*link && *link != client) {
        link = &(*link)->workspace_next;
    }
    if (*link) {
        *link = client->workspace_next;
    }

    if (client->tab_prev) client->tab_prev->tab_next = client->tab_next;
    else workspace->tab_head = client->tab_next;
    if (client->tab_next) client->tab_next->tab_prev = client->tab_prev;
    else workspace->tab_tail = client->tab_prev;

    if (client->stack_prev) client->stack_prev->stack_next = client->stack_next;
    else workspace->stack_head = client->stack_next;
    if (client->stack_next) client->stack_next->stack_prev = client->stack_prev;
    else workspace->stack_tail = client->stack_prev;

    unlink_workspace_focus(workspace, client);
}

/* Change the sole workspace owner and every workspace-local order without
 * performing focus, mapping, geometry, stacking, EWMH, or UI projection. */
static void reassign_client_workspace_authority(Client *client,
                                                Workspace *workspace)
{
    Workspace *old = client->workspace;
    if (old == workspace) return;
    unlink_workspace_orders(old, client);
    client->workspace = workspace;
    client->workspace_next = NULL;
    client->tab_prev = client->tab_next = NULL;
    client->stack_prev = client->stack_next = NULL;
    client->focus_prev = client->focus_next = NULL;
    append_workspace_orders(workspace, client);
}

static bool client_supports_protocol(WM *wm, Client *client, Atom protocol)
{
    Atom *protocols = NULL;
    int count = 0;
    bool found = false;
    if (XGetWMProtocols(wm->display, client->window, &protocols, &count)) {
        for (int i = 0; i < count; ++i) {
            if (protocols[i] == protocol) {
                found = true;
                break;
            }
        }
    }
    XFree(protocols);
    return found;
}

static void set_client_urgent(WM *wm, Client *client, bool urgent)
{
    bool changed = client->urgent != urgent;
    client->urgent = urgent;
    XWMHints *hints = XGetWMHints(wm->display, client->window);
    if (hints) {
        bool hinted = (hints->flags & XUrgencyHint) != 0;
        if (hinted != urgent) {
            if (urgent) hints->flags |= XUrgencyHint;
            else hints->flags &= ~XUrgencyHint;
            XSetWMHints(wm->display, client->window, hints);
        }
        XFree(hints);
    }
    ui_client_border_refresh(wm, client);
    if (changed) {
        ui_update(wm);
    }
}

static void read_wm_hints(WM *wm, Client *client)
{
    XWMHints *hints = XGetWMHints(wm->display, client->window);
    client->accepts_input = !hints || !(hints->flags & InputHint) || hints->input;
    bool urgent = hints && (hints->flags & XUrgencyHint);
    if (hints) XFree(hints);
    set_client_urgent(wm, client, urgent && wm->model.focused_client != client);
}

static void read_wm_protocols(WM *wm, Client *client)
{
    client->takes_focus = client_supports_protocol(wm, client, wm->atoms.wm_take_focus);
}

static void read_transient_for(WM *wm, Client *client)
{
    Window transient = None;
    if (XGetTransientForHint(wm->display, client->window, &transient))
        client->transient_for = transient;
    else
        client->transient_for = None;
}

static bool client_can_focus(const Client *client)
{
    return client && (client->accepts_input || client->takes_focus);
}

static Client *workspace_stable_focus_fallback(Workspace *workspace, Client *removed)
{
    if (removed) {
        for (Client *client = removed->tab_next; client; client = client->tab_next)
            if (client_can_focus(client)) return client;
        for (Client *client = removed->tab_prev; client; client = client->tab_prev)
            if (client_can_focus(client)) return client;
        return NULL;
    }
    for (Client *client = workspace->tab_tail; client; client = client->tab_prev)
        if (client_can_focus(client)) return client;
    return NULL;
}

static Client *workspace_focus_fallback(Workspace *workspace, Client *removed)
{
    for (Client *client = workspace->focus_head; client; client = client->focus_next)
        if (client != removed && client_can_focus(client)) return client;
    return workspace_stable_focus_fallback(workspace, removed);
}

Client *workspace_focus_target(Workspace *workspace)
{
    return workspace_focus_fallback(workspace, NULL);
}

static void project_client_input_focus(WM *wm, Client *client, Time time)
{
    if (client->accepts_input)
        XSetInputFocus(wm->display, client->window, RevertToPointerRoot, time);
    if (client->takes_focus) {
        XEvent message = {0};
        message.xclient.type = ClientMessage;
        message.xclient.window = client->window;
        message.xclient.message_type = wm->atoms.wm_protocols;
        message.xclient.format = 32;
        message.xclient.data.l[0] = (long)wm->atoms.wm_take_focus;
        message.xclient.data.l[1] = (long)time;
        XSendEvent(wm->display, client->window, False, NoEventMask, &message);
    }
}

/* Project the semantic focus already chosen by Box2430. This helper does not
 * select a client, monitor, or workspace and is also used to repair unexpected
 * FocusIn observations without creating a second semantic focus path. */
static void project_semantic_input_focus(WM *wm, Time time)
{
    if (wm->model.focused_client)
        project_client_input_focus(wm, wm->model.focused_client, time);
    else
        XSetInputFocus(wm->display, wm->root, RevertToPointerRoot, time);
    x11_update_active_window(wm);
}

static void set_selected_monitor_authority(WMModel *model, Monitor *monitor)
{
    model->selected_monitor = monitor;
}

static void select_monitor_context(WM *wm, Monitor *monitor)
{
    set_selected_monitor_authority(&wm->model, monitor);
    x11_update_workarea(wm);
}

/* Semantic client activation for the single interaction seat. Client-specific
 * activation owns focused_client, selected_monitor, and workspace focus
 * history; projection remains ordered exactly as in the V1.7 focus path. */
static void client_activate(WM *wm, Client *client, Time time)
{
    if (client && !client_can_focus(client)) return;
    if (wm->model.focused_client == client && client) return;
    Client *previous = wm->model.focused_client;
    wm->model.focused_client = client;
    if (previous) {
        ui_client_border_refresh(wm, previous);
        grab_client_buttons(wm, previous, false);
    }
    if (!client) {
        project_semantic_input_focus(wm, time);
        ui_update(wm);
        return;
    }

    select_monitor_context(wm, client->workspace->monitor);
    promote_workspace_focus(client->workspace, client);
    set_client_urgent(wm, client, false);
    ui_client_border_refresh(wm, client);
    grab_client_buttons(wm, client, true);
    project_semantic_input_focus(wm, time);
    ui_update(wm);
    if (wm->config.raise_on_focus) client_raise(wm, client);
}

/* Activate a monitor without changing its active workspace or warping the
 * pointer. The selected monitor remains authoritative even when the active
 * workspace has no focusable client. */
static void monitor_activate(WM *wm, Monitor *monitor, Time time)
{
    if (!monitor) return;
    select_monitor_context(wm, monitor);
    client_activate(wm, workspace_focus_target(monitor->active_workspace), time);
}

static void resolve_focus_before_client_removal(WM *wm, Client *client)
{
    if (wm->model.focused_client != client) return;
    /* Fallback depends on the disappearing client's still-valid focus/tab
     * links. Avoid refreshing X presentation on that disappearing client. */
    Client *fallback = workspace_focus_fallback(client->workspace, client);
    wm->model.focused_client = NULL;
    if (fallback && client->workspace->mode == WORKSPACE_MONOCLE &&
        client_workspace_is_active(fallback)) {
        /* The departing client may already be destroyed/unmapped, so do not
         * run a whole-workspace reconciliation that would query it. The
         * remaining MONOCLE tabs are already hidden in steady state. */
        project_client_mapped(wm, fallback);
        client_activate(wm, fallback, CurrentTime);
    } else {
        client_activate(wm, fallback, CurrentTime);
    }
}

static void present_client_geometry(WM *wm, Client *client, Rect geometry)
{
    XMoveResizeWindow(wm->display, client->window, geometry.x, geometry.y,
                      (unsigned int)geometry.width, (unsigned int)geometry.height);
}

/*
 * Commit geometry that belongs to the client's normal/snap/maximize semantic
 * state. Temporary presentations such as MONOCLE and real fullscreen must use
 * present_client_geometry() instead so they cannot overwrite restore state.
 */
static void commit_client_geometry(WM *wm, Client *client, Rect geometry)
{
    client->geometry = geometry;
    present_client_geometry(wm, client, geometry);
}

static unsigned int client_border_width_for_mode(const WM *wm,
                                                 const Client *client,
                                                 WorkspaceMode mode)
{
    if (!client->border_enabled) return 0;
    return mode == WORKSPACE_MONOCLE
        ? wm->config.border.monocle.width : wm->config.border.free.width;
}

static unsigned int client_free_border_width(const WM *wm, const Client *client)
{
    return client_border_width_for_mode(wm, client, WORKSPACE_FREE);
}

static unsigned int client_border_width(const WM *wm, const Client *client)
{
    if (client->fullscreen) return 0;
    return client_border_width_for_mode(wm, client, client->workspace->mode);
}

static Rect fit_workarea(WM *wm, const Client *client, Rect area)
{
    int border = (int)client_border_width(wm, client);
    int width = area.width - 2 * border;
    int height = area.height - 2 * border;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    return (Rect){area.x, area.y, width, height};
}

static Rect monocle_content_area(const WM *wm, const Workspace *workspace)
{
    const Monitor *monitor = workspace->monitor;
    Rect area = monitor->workarea;
    if (!ui_tabs_should_materialize(wm, workspace)) return area;
    int tab_height = (int)ui_tab_height(wm, monitor);
    if (wm->config.tabs.position == UI_EDGE_TOP)
        area.y += tab_height;
    area.height -= tab_height;
    return area;
}

static void update_net_wm_state(WM *wm, Client *client)
{
    Atom states[3];
    int count = 0;
    if (client->fullscreen || client->client_fullscreen)
        states[count++] = wm->atoms.net_wm_state_fullscreen;
    if (client->maximized) {
        states[count++] = wm->atoms.net_wm_state_maximized_horz;
        states[count++] = wm->atoms.net_wm_state_maximized_vert;
    }
    if (count > 0) {
        XChangeProperty(wm->display, client->window, wm->atoms.net_wm_state,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)states, count);
    } else {
        XDeleteProperty(wm->display, client->window, wm->atoms.net_wm_state);
    }
}

void client_raise(WM *wm, Client *client)
{
    if (!client) return;
    Workspace *workspace = client->workspace;
    if (workspace->stack_tail != client) {
        if (client->stack_prev) client->stack_prev->stack_next = client->stack_next;
        else workspace->stack_head = client->stack_next;
        if (client->stack_next) client->stack_next->stack_prev = client->stack_prev;
        client->stack_prev = workspace->stack_tail;
        client->stack_next = NULL;
        workspace->stack_tail->stack_next = client;
        workspace->stack_tail = client;
    }
    enforce_stacking(wm);
    x11_update_client_lists(wm);
}

void client_lower(WM *wm, Client *client)
{
    if (!client) return;
    Workspace *workspace = client->workspace;
    if (workspace->stack_head != client) {
        if (client->stack_prev) client->stack_prev->stack_next = client->stack_next;
        if (client->stack_next) client->stack_next->stack_prev = client->stack_prev;
        else workspace->stack_tail = client->stack_prev;
        client->stack_prev = NULL;
        client->stack_next = workspace->stack_head;
        workspace->stack_head->stack_prev = client;
        workspace->stack_head = client;
    }
    enforce_stacking(wm);
    x11_update_client_lists(wm);
}

void client_close(WM *wm, Client *client)
{
    if (!client) return;
    if (client_supports_protocol(wm, client, wm->atoms.wm_delete_window)) {
        XEvent message = {0};
        message.xclient.type = ClientMessage;
        message.xclient.window = client->window;
        message.xclient.message_type = wm->atoms.wm_protocols;
        message.xclient.format = 32;
        message.xclient.data.l[0] = (long)wm->atoms.wm_delete_window;
        message.xclient.data.l[1] = CurrentTime;
        XSendEvent(wm->display, client->window, False, NoEventMask, &message);
    } else {
        XKillClient(wm->display, client->window);
    }
}

void workspace_focus_relative(WM *wm, Workspace *workspace, bool forward)
{
    if (!workspace || workspace != workspace->monitor->active_workspace) return;
    Client *target = NULL;
    Client *cursor = wm->model.focused_client &&
        wm->model.focused_client->workspace == workspace
        ? wm->model.focused_client : workspace_focus_target(workspace);
    unsigned int count = tab_count(workspace);
    for (unsigned int i = 0; i < count; ++i) {
        cursor = cursor ? (forward ? cursor->tab_next : cursor->tab_prev) : NULL;
        if (!cursor) cursor = forward ? workspace->tab_head : workspace->tab_tail;
        if (client_can_focus(cursor)) { target = cursor; break; }
    }
    if (workspace->mode == WORKSPACE_MONOCLE && target)
        client_focus_tab_target(wm, target, CurrentTime);
    else
        client_activate(wm, target, CurrentTime);
}

void client_focus_tab_target(WM *wm, Client *client, Time time)
{
    if (!client || !client_can_focus(client)) return;
    Workspace *workspace = client->workspace;
    if (workspace != workspace->monitor->active_workspace) return;

    /* The incoming tab must be viewable before X input focus is projected.
     * Keep the old tab mapped until the new one is focused/raised so the
     * handoff never exposes an empty MONOCLE content area. */
    if (workspace->mode == WORKSPACE_MONOCLE)
        project_client_mapped(wm, client);
    client_activate(wm, client, time);
    if (workspace->mode == WORKSPACE_MONOCLE) {
        client_raise(wm, client);
        reconcile_workspace_mapping(wm, workspace);
    }
}

void workspace_set_mode(WM *wm, Workspace *workspace, WorkspaceMode mode)
{
    if (!workspace || workspace->mode == mode) return;
    workspace->mode = mode;
    if (workspace != workspace->monitor->active_workspace) return;
    ui_bar_update(wm);
    if (mode == WORKSPACE_MONOCLE) {
        Client *target = workspace_focus_target(workspace);
        if (target) {
            materialize_client_geometry(wm, target);
            client_raise(wm, target);
        }
        reconcile_workspace_mapping(wm, workspace);
        for (Client *client = workspace->clients; client; client = client->workspace_next)
            if (client != target) materialize_client_geometry(wm, client);
    } else {
        for (Client *client = workspace->clients; client; client = client->workspace_next)
            materialize_client_geometry(wm, client);
        reconcile_workspace_mapping(wm, workspace);
    }
    enforce_stacking(wm);
}

static Rect snap_geometry_on(WM *wm, Client *client, Monitor *monitor,
                             SnapState state)
{
    Rect area = monitor->workarea;
    int side_width = (int)(area.width * wm->config.snap_side_ratio);
    int corner_width = (int)(area.width * wm->config.snap_corner_width_ratio);
    int corner_height = (int)(area.height * wm->config.snap_corner_height_ratio);
    Rect target = area;
    if (state == SNAP_LEFT) target.width = side_width;
    else if (state == SNAP_RIGHT) {
        target.x += area.width - side_width;
        target.width = side_width;
    } else if (state == SNAP_TOP_LEFT || state == SNAP_BOTTOM_LEFT) {
        target.width = corner_width;
    } else if (state == SNAP_TOP_RIGHT || state == SNAP_BOTTOM_RIGHT) {
        target.x += area.width - corner_width;
        target.width = corner_width;
    }
    if (state == SNAP_TOP_LEFT || state == SNAP_TOP_RIGHT)
        target.height = corner_height;
    if (state == SNAP_BOTTOM_LEFT || state == SNAP_BOTTOM_RIGHT) {
        target.y += area.height - corner_height;
        target.height = corner_height;
    }
    return fit_workarea(wm, client, target);
}

static Rect snap_geometry(WM *wm, Client *client, SnapState state)
{
    return snap_geometry_on(wm, client, client->workspace->monitor, state);
}

static void materialize_client_geometry(WM *wm, Client *client)
{
    Monitor *monitor = client->workspace->monitor;
    XSetWindowBorderWidth(wm->display, client->window,
                          client_border_width(wm, client));
    ui_client_border_refresh(wm, client);
    if (client->fullscreen) {
        present_client_geometry(wm, client, monitor->geometry);
    } else if (client->workspace->mode == WORKSPACE_MONOCLE) {
        Rect area = fit_workarea(
            wm, client, monocle_content_area(wm, client->workspace));
        present_client_geometry(wm, client, area);
    } else if (client->maximized) {
        commit_client_geometry(wm, client,
                               fit_workarea(wm, client, monitor->workarea));
    } else if (client->snap_state != SNAP_NONE) {
        commit_client_geometry(wm, client,
                               snap_geometry(wm, client, client->snap_state));
    } else {
        present_client_geometry(wm, client, client->geometry);
    }
}

static bool client_geometry_is_wm_owned(const Client *client)
{
    return client->workspace->mode == WORKSPACE_MONOCLE ||
           client->fullscreen || client->maximized ||
           client->snap_state != SNAP_NONE;
}

static bool ranges_overlap(int start, int end, unsigned long other_start,
                           unsigned long other_end)
{
    return end > (int)other_start && start <= (int)other_end;
}

static void calculate_workareas(WM *wm)
{
    int root_width = DisplayWidth(wm->display, wm->screen);
    int root_height = DisplayHeight(wm->display, wm->screen);
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        Monitor *monitor = &wm->model.monitors[i];
        Rect area = monitor->geometry;
        int right = area.x + area.width;
        int bottom = area.y + area.height;
        for (SpecialWindow *dock = wm->model.special_windows; dock; dock = dock->next) {
            if (dock->type != WINDOW_TYPE_DOCK || !dock->has_strut) continue;
            unsigned long *s = dock->strut;
            if (s[0] && ranges_overlap(area.y, bottom, s[4], s[5]) &&
                (int)s[0] > area.x) area.x = (int)s[0];
            int right_edge = root_width - (int)s[1];
            if (s[1] && ranges_overlap(area.y, bottom, s[6], s[7]) &&
                right_edge < right) right = right_edge;
            if (s[2] && ranges_overlap(area.x, right, s[8], s[9]) &&
                (int)s[2] > area.y) area.y = (int)s[2];
            int bottom_edge = root_height - (int)s[3];
            if (s[3] && ranges_overlap(area.x, right, s[10], s[11]) &&
                bottom_edge < bottom) bottom = bottom_edge;
        }
        area.width = right - area.x;
        area.height = bottom - area.y;
        if (area.width < 1) area.width = 1;
        if (area.height < 1) area.height = 1;

        monitor->bar_geometry = (Rect){
            .x = area.x,
            .y = area.y,
            .width = area.width,
            .height = 0,
        };
        if (wm->config.bar.enabled && area.height > 1) {
            unsigned int height = wm->config.bar.height;
            if (height >= (unsigned int)area.height)
                height = (unsigned int)area.height - 1;
            monitor->bar_geometry.height = (int)height;
            if (wm->config.bar.position == UI_EDGE_TOP) {
                monitor->bar_geometry.y = area.y;
                area.y += (int)height;
            } else {
                monitor->bar_geometry.y = area.y + area.height - (int)height;
            }
            area.height -= (int)height;
        }
        monitor->workarea = area;
    }
}

static void rematerialize_all_clients(WM *wm)
{
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        Monitor *monitor = &wm->model.monitors[i];
        for (unsigned int j = 0; j < wm->config.workspace_count; ++j)
            for (Client *client = monitor->workspaces[j].clients; client;
                 client = client->workspace_next)
                materialize_client_geometry(wm, client);
    }
}

static void recompute_workareas(WM *wm)
{
    calculate_workareas(wm);
    rematerialize_all_clients(wm);
    x11_update_workarea(wm);
    ui_update(wm);
}

void client_snap(WM *wm, Client *client, SnapState state)
{
    if (!client || client->workspace->mode == WORKSPACE_MONOCLE || client->fullscreen)
        return;
    if (state == SNAP_NONE) {
        if (client->snap_state != SNAP_NONE || client->maximized) {
            client->snap_state = SNAP_NONE;
            client->maximized = false;
            commit_client_geometry(wm, client, client->normal_geometry);
            update_net_wm_state(wm, client);
        }
        return;
    }
    if (client->snap_state == SNAP_NONE && !client->maximized)
        client->normal_geometry = client->geometry;
    bool was_maximized = client->maximized;
    client->maximized = false;
    client->snap_state = state;
    commit_client_geometry(wm, client, snap_geometry(wm, client, state));
    if (was_maximized) update_net_wm_state(wm, client);
}

static Monitor *monitor_at_point(WM *wm, int x, int y)
{
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        Rect area = wm->model.monitors[i].geometry;
        if (x >= area.x && x < area.x + area.width &&
            y >= area.y && y < area.y + area.height) return &wm->model.monitors[i];
    }
    return wm->model.selected_monitor;
}

static Rect snap_preview_outer_target(WM *wm, Client *client, Monitor *monitor,
                                      SnapState state, bool maximize)
{
    if (maximize) return monitor->workarea;
    Rect inner = snap_geometry_on(wm, client, monitor, state);
    int border = (int)client_border_width(wm, client);
    inner.width += 2 * border;
    inner.height += 2 * border;
    return inner;
}

static SnapState pointer_snap_target(WM *wm, Monitor *monitor, int x, int y)
{
    if (!wm->config.snap_enabled) return SNAP_NONE;
    Rect area = monitor->geometry;
    int zone = (int)wm->config.snap_edge_zone;
    bool left = x < area.x + zone;
    bool right = x >= area.x + area.width - zone;
    bool top = y < area.y + zone;
    bool bottom = y >= area.y + area.height - zone;
    if (top && left) return SNAP_TOP_LEFT;
    if (top && right) return SNAP_TOP_RIGHT;
    if (bottom && left) return SNAP_BOTTOM_LEFT;
    if (bottom && right) return SNAP_BOTTOM_RIGHT;
    if (top) return SNAP_NONE; /* maximize is tracked separately below */
    if (left) return SNAP_LEFT;
    if (right) return SNAP_RIGHT;
    return SNAP_NONE;
}

void mouse_begin_drag(WM *wm, Client *client, bool resize, int root_x, int root_y)
{
    if (!client || client->workspace->mode == WORKSPACE_MONOCLE || client->fullscreen)
        return;
    if (client->snap_state != SNAP_NONE || client->maximized) {
        bool was_maximized = client->maximized;
        client->snap_state = SNAP_NONE;
        client->maximized = false;
        commit_client_geometry(wm, client, client->normal_geometry);
        if (was_maximized) update_net_wm_state(wm, client);
    }
    client_activate(wm, client, CurrentTime);
    wm->drag.client = client;
    wm->drag.active = true;
    wm->drag.resize = resize;
    wm->drag.preview_snap = SNAP_NONE;
    wm->drag.preview_monitor = NULL;
    wm->drag.preview_maximized = false;
    XChangeActivePointerGrab(
        wm->display, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
        resize ? wm->cursor_resize : wm->cursor_move, CurrentTime);
    if (resize) {
        int border = (int)client_border_width(wm, client);
        root_x = client->geometry.x + client->geometry.width + 2 * border - 1;
        root_y = client->geometry.y + client->geometry.height + 2 * border - 1;
        XWarpPointer(wm->display, None, wm->root, 0, 0, 0, 0, root_x, root_y);
    } else {
        int border = (int)client_border_width(wm, client);
        root_x = client->geometry.x + (client->geometry.width + 2 * border) / 2;
        root_y = client->geometry.y + (client->geometry.height + 2 * border) / 2;
        XWarpPointer(wm->display, None, wm->root, 0, 0, 0, 0, root_x, root_y);
    }
    wm->drag.start_x = root_x;
    wm->drag.start_y = root_y;
    wm->drag.start_geometry = client->geometry;
}

static void update_drag(WM *wm, int root_x, int root_y)
{
    if (!wm->drag.active || !wm->drag.client) return;
    Client *client = wm->drag.client;
    Rect geometry = wm->drag.start_geometry;
    int dx = root_x - wm->drag.start_x;
    int dy = root_y - wm->drag.start_y;
    if (wm->drag.resize) {
        geometry.width += dx;
        geometry.height += dy;
        if (geometry.width < 1) geometry.width = 1;
        if (geometry.height < 1) geometry.height = 1;
        apply_normal_hints(wm, client, &geometry.width, &geometry.height);
    } else {
        geometry.x += dx;
        geometry.y += dy;
    }
    client->snap_state = SNAP_NONE;
    client->maximized = false;
    commit_client_geometry(wm, client, geometry);
    client->normal_geometry = geometry;

    if (!wm->drag.resize) {
        Monitor *monitor = monitor_at_point(wm, root_x, root_y);
        SnapState target = pointer_snap_target(wm, monitor, root_x, root_y);
        bool maximize = root_y < monitor->geometry.y + (int)wm->config.snap_edge_zone &&
                        root_x >= monitor->geometry.x + (int)wm->config.snap_edge_zone &&
                        root_x < monitor->geometry.x + monitor->geometry.width -
                                 (int)wm->config.snap_edge_zone;
        if (maximize) target = SNAP_NONE;
        Monitor *desired_monitor = (target != SNAP_NONE || maximize) ? monitor : NULL;
        if (target != wm->drag.preview_snap ||
            desired_monitor != wm->drag.preview_monitor ||
            maximize != wm->drag.preview_maximized) {
            ui_snap_preview_hide(wm);
            wm->drag.preview_snap = target;
            wm->drag.preview_monitor = desired_monitor;
            wm->drag.preview_maximized = maximize;
            if (maximize || target != SNAP_NONE) {
                Rect outer = snap_preview_outer_target(
                    wm, client, monitor, target, maximize);
                ui_snap_preview_show(wm, outer);
            }
        }
    }
}

static void finish_drag(WM *wm)
{
    if (!wm->drag.active || !wm->drag.client) return;
    Client *client = wm->drag.client;
    Monitor *target = NULL;
    bool maximize = wm->drag.preview_maximized;
    if (!wm->drag.resize && wm->drag.preview_monitor) {
        target = wm->drag.preview_monitor;
        int pointer_x, pointer_y, child_x, child_y;
        unsigned int mask;
        Window root_return, child_return;
        if (XQueryPointer(wm->display, wm->root, &root_return, &child_return,
                          &pointer_x, &pointer_y, &child_x, &child_y, &mask)) {
            Rect area = target->geometry;
            maximize = pointer_y < area.y + (int)wm->config.snap_edge_zone &&
                       pointer_x >= area.x + (int)wm->config.snap_edge_zone &&
                       pointer_x < area.x + area.width -
                                   (int)wm->config.snap_edge_zone;
        }
    }
    ui_snap_preview_hide(wm);
    if (!target) {
        int center_x = client->geometry.x + client->geometry.width / 2;
        int center_y = client->geometry.y + client->geometry.height / 2;
        target = monitor_at_point(wm, center_x, center_y);
    }
    if (target != client->workspace->monitor)
        client_move_to_workspace(wm, client, target->active_workspace, true, false);
    if (!wm->drag.resize && wm->drag.preview_monitor) {
        if (maximize) client_set_maximized(wm, client, true);
        else client_snap(wm, client, wm->drag.preview_snap);
    }
    client_activate(wm, client, CurrentTime);
    wm->drag.active = false;
    wm->drag.client = NULL;
    wm->drag.preview_monitor = NULL;
    wm->drag.preview_snap = SNAP_NONE;
    wm->drag.preview_maximized = false;
}

void client_set_maximized(WM *wm, Client *client, bool maximized)
{
    if (!client || client->workspace->mode == WORKSPACE_MONOCLE || client->fullscreen)
        return;
    if (client->maximized == maximized) return;
    if (maximized) {
        if (client->snap_state == SNAP_NONE) client->normal_geometry = client->geometry;
        client->snap_state = SNAP_NONE;
        client->maximized = true;
        commit_client_geometry(
            wm, client,
            fit_workarea(wm, client, client->workspace->monitor->workarea));
    } else {
        client->maximized = false;
        client->snap_state = SNAP_NONE;
        commit_client_geometry(wm, client, client->normal_geometry);
    }
    update_net_wm_state(wm, client);
}

static void apply_real_fullscreen(WM *wm, Client *client, bool fullscreen)
{
    if (!client || client->fullscreen == fullscreen) return;
    client->fullscreen = fullscreen;
    materialize_client_geometry(wm, client);
    enforce_stacking(wm);
    update_net_wm_state(wm, client);
    x11_update_client_lists(wm);
}

static bool client_wants_real_fullscreen(const Client *client)
{
    return client->user_fullscreen ||
           (client->client_fullscreen &&
            client->fullscreen_policy == CLIENT_FULLSCREEN_ALLOW);
}

void client_set_fullscreen(WM *wm, Client *client, bool fullscreen)
{
    if (!client) return;
    client->user_fullscreen = fullscreen;
    apply_real_fullscreen(wm, client, client_wants_real_fullscreen(client));
    update_net_wm_state(wm, client);
}

static void client_set_requested_fullscreen(WM *wm, Client *client, bool requested)
{
    if (client->fullscreen_policy == CLIENT_FULLSCREEN_DENY) {
        client->client_fullscreen = false;
    } else {
        client->client_fullscreen = requested;
    }
    apply_real_fullscreen(wm, client, client_wants_real_fullscreen(client));
    update_net_wm_state(wm, client);
}

static void project_client_mapped(WM *wm, Client *client)
{
    XMapWindow(wm->display, client->window);
}

/* Record causality before issuing the X request so its later UnmapNotify is an
 * observation of WM projection rather than a client-withdrawal transition. */
static void project_client_unmapped(WM *wm, Client *client)
{
    ++client->ignored_unmaps;
    XUnmapWindow(wm->display, client->window);
}

static bool client_workspace_is_active(const Client *client)
{
    return client && client->workspace == client->workspace->monitor->active_workspace;
}

/* MONOCLE keeps exactly one ordinary client mapped: the current workspace
 * focus target.  FREE keeps every client on the active workspace mapped.
 * Hidden workspaces keep all clients unmapped. */
static bool client_should_be_mapped(const Client *client)
{
    if (!client_workspace_is_active(client)) return false;
    if (client->workspace->mode == WORKSPACE_FREE) return true;
    return workspace_focus_target(client->workspace) == client;
}

static void reconcile_client_mapping(WM *wm, Client *client)
{
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(wm->display, client->window, &attrs)) return;
    bool should_map = client_should_be_mapped(client);
    if (should_map && attrs.map_state == IsUnmapped) {
        project_client_mapped(wm, client);
    } else if (!should_map && attrs.map_state != IsUnmapped) {
        project_client_unmapped(wm, client);
    }
}

static void reconcile_workspace_mapping(WM *wm, Workspace *workspace)
{
    for (Client *client = workspace->clients; client; client = client->workspace_next)
        reconcile_client_mapping(wm, client);
}

void workspace_activate(WM *wm, Monitor *monitor, Workspace *workspace)
{
    if (!monitor || !workspace || workspace->monitor != monitor) return;

    bool monitor_changed = wm->model.selected_monitor != monitor;
    if (monitor->active_workspace == workspace) {
        if (!monitor_changed) return;
        monitor_activate(wm, monitor, CurrentTime);
        return;
    }

    Workspace *old = monitor->active_workspace;
    Client *target = workspace_focus_target(workspace);
    for (Client *client = workspace->clients; client; client = client->workspace_next)
        materialize_client_geometry(wm, client);

    select_monitor_context(wm, monitor);
    monitor->active_workspace = workspace;
    if (workspace->mode == WORKSPACE_MONOCLE) {
        if (target) {
            project_client_mapped(wm, target);
            XRaiseWindow(wm->display, target->window);
        }
    } else {
        /* Preserve FREE's established incoming stack materialization: build
         * the final bottom-to-top order before retiring the old workspace. */
        for (Client *client = workspace->stack_head; client; client = client->stack_next) {
            project_client_mapped(wm, client);
            XRaiseWindow(wm->display, client->window);
        }
    }
    /* Keep the incoming-first handoff, but retire the outgoing projection
     * before client_activate() can publish focus/UI or flush via stacking. */
    if (old->mode == WORKSPACE_MONOCLE) {
        reconcile_workspace_mapping(wm, old);
    } else {
        for (Client *client = old->clients; client; client = client->workspace_next)
            project_client_unmapped(wm, client);
    }
    client_activate(wm, target, CurrentTime);
    ui_bar_update(wm);
    enforce_stacking(wm);
}

void monitor_select(WM *wm, Monitor *monitor)
{
    if (!monitor) return;
    select_monitor_context(wm, monitor);
    int x = monitor->geometry.x + monitor->geometry.width / 2;
    int y = monitor->geometry.y + monitor->geometry.height / 2;
    XWarpPointer(wm->display, None, wm->root, 0, 0, 0, 0, x, y);
    Workspace *workspace = monitor->active_workspace;
    client_activate(wm, workspace_focus_target(workspace), CurrentTime);
}

static Rect clamp_to_workarea(Rect geometry, Rect area, unsigned int border_width)
{
    int outer_width = geometry.width + 2 * (int)border_width;
    int outer_height = geometry.height + 2 * (int)border_width;
    if (outer_width > area.width) geometry.x = area.x;
    else {
        if (geometry.x < area.x) geometry.x = area.x;
        if (geometry.x + outer_width > area.x + area.width)
            geometry.x = area.x + area.width - outer_width;
    }
    if (outer_height > area.height) geometry.y = area.y;
    else {
        if (geometry.y < area.y) geometry.y = area.y;
        if (geometry.y + outer_height > area.y + area.height)
            geometry.y = area.y + area.height - outer_height;
    }
    return geometry;
}

void client_move_to_workspace(WM *wm, Client *client, Workspace *workspace,
                              bool follow, bool translate_monitor_geometry)
{
    if (!client || !workspace || client->workspace == workspace) {
        if (follow && workspace)
            workspace_activate(wm, workspace->monitor, workspace);
        return;
    }
    Workspace *old = client->workspace;
    Monitor *old_monitor = old->monitor;
    Monitor *new_monitor = workspace->monitor;
    bool was_focused = wm->model.focused_client == client;
    if (was_focused) {
        Client *fallback = workspace_focus_fallback(old, client);
        if (fallback && old->mode == WORKSPACE_MONOCLE)
            client_focus_tab_target(wm, fallback, CurrentTime);
        else
            client_activate(wm, fallback, CurrentTime);
    }

    bool keep_mapped_for_follow = follow && old_monitor == new_monitor &&
        workspace != new_monitor->active_workspace;
    bool was_mapped_projection = client_should_be_mapped(client);
    if (was_mapped_projection && !keep_mapped_for_follow)
        project_client_unmapped(wm, client);
    if (translate_monitor_geometry && old_monitor != new_monitor) {
        translate_client_latent_geometry(client, old_monitor->geometry,
                                         new_monitor->geometry);
        clamp_client_latent_geometry(wm, client, new_monitor->workarea);
    }
    reassign_client_workspace_authority(client, workspace);

    materialize_client_geometry(wm, client);

    if (follow) {
        /* Following the move makes this client the destination workspace's
         * remembered tab/focus target before workspace activation, so a
         * MONOCLE destination maps the moved client directly rather than
         * briefly exposing the previous tab. */
        promote_workspace_focus(workspace, client);
        set_selected_monitor_authority(&wm->model, workspace->monitor);
        workspace_activate(wm, workspace->monitor, workspace);
        project_client_mapped(wm, client);
        client_activate(wm, client, CurrentTime);
        client_raise(wm, client);
        if (workspace->mode == WORKSPACE_MONOCLE)
            reconcile_workspace_mapping(wm, workspace);
    } else {
        reconcile_client_mapping(wm, client);
    }
    ui_update(wm);
}

static void grab_default_keys(WM *wm)
{
    XModifierKeymap *map = XGetModifierMapping(wm->display);
    wm->numlock_mask = 0;
    if (map) {
        KeyCode numlock = XKeysymToKeycode(wm->display, XK_Num_Lock);
        for (int modifier = 0; modifier < 8; ++modifier)
            for (int key = 0; key < map->max_keypermod; ++key)
                if (map->modifiermap[modifier * map->max_keypermod + key] == numlock)
                    wm->numlock_mask = 1U << modifier;
        XFreeModifiermap(map);
    }
    unsigned int modifiers[] = {
        0, LockMask, wm->numlock_mask, LockMask | wm->numlock_mask,
    };

    int first_keycode = 0;
    int last_keycode = 0;
    int keysyms_per_keycode = 0;
    XDisplayKeycodes(wm->display, &first_keycode, &last_keycode);
    KeySym *keysyms = XGetKeyboardMapping(
        wm->display, (KeyCode)first_keycode,
        last_keycode - first_keycode + 1, &keysyms_per_keycode);
    if (!keysyms) return;

    XUngrabKey(wm->display, AnyKey, AnyModifier, wm->root);
    for (int keycode = first_keycode; keycode <= last_keycode; ++keycode) {
        /* KeyPress dispatch canonicalizes the unshifted/base keysym too. */
        KeySym symbol = keysyms[(keycode - first_keycode) * keysyms_per_keycode];
        for (unsigned int binding_index = 0;
             binding_index < wm->config.key_binding_count; ++binding_index) {
            KeyBinding *binding = &wm->config.key_bindings[binding_index];
            if (binding->symbol != symbol) continue;
            for (size_t i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
                XGrabKey(wm->display, keycode,
                         binding->modifiers | modifiers[i], wm->root,
                         True, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    XFree(keysyms);
}

static void handle_key_press(WM *wm, XKeyEvent *event)
{
    unsigned int state = event->state & ~(LockMask | wm->numlock_mask);
    KeySym symbol = XLookupKeysym(event, 0);
    for (unsigned int i = 0; i < wm->config.key_binding_count; ++i) {
        KeyBinding *binding = &wm->config.key_bindings[i];
        if (binding->modifiers == state && binding->symbol == symbol) {
            const char *argv[BOX2430_MAX_COMMAND_ARGS];
            for (int j = 0; j < binding->argc; ++j) argv[j] = binding->argv[j];
            CommandContext context = {
                .type = COMMAND_CONTEXT_KEYBOARD,
                .time = event->time,
            };
            command_run(wm, &context, binding->argc, argv);
            return;
        }
    }
}

typedef struct InitialPolicy {
    Monitor *monitor;
    unsigned int workspace_index;
    bool workspace_explicit;
    bool focus_on_map;
    bool raise_on_map;
    bool border;
    PlacementPolicy placement;
    ClientFullscreenPolicy fullscreen_policy;
} InitialPolicy;

static bool rule_matches(const Rule *rule, const Client *client)
{
    return (!rule->has_class || fnmatch(rule->class_pattern, client->class_name, 0) == 0) &&
           (!rule->has_instance || fnmatch(rule->instance_pattern, client->instance, 0) == 0) &&
           (!rule->has_title || fnmatch(rule->title_pattern, client->title, 0) == 0) &&
           (!rule->has_window_type || rule->window_type == client->window_type);
}

static InitialPolicy initial_policy(WM *wm, Client *client,
                                    const Client *transient_parent)
{
    Monitor *default_monitor = transient_parent
        ? transient_parent->workspace->monitor : wm->model.selected_monitor;
    unsigned int default_workspace = transient_parent
        ? transient_parent->workspace->index
        : default_monitor->active_workspace->index;
    InitialPolicy policy = {
        .monitor = default_monitor,
        .workspace_index = default_workspace,
        .focus_on_map = wm->config.focus_on_map,
        .raise_on_map = wm->config.raise_on_map,
        .border = true,
        .placement = client->window_type == WINDOW_TYPE_DIALOG
            ? wm->config.dialog_placement : wm->config.normal_placement,
        .fullscreen_policy = wm->config.client_fullscreen_policy,
    };
    for (unsigned int i = 0; i < wm->config.rule_count; ++i) {
        const Rule *rule = &wm->config.rules[i];
        if (!rule_matches(rule, client)) continue;
        if (rule->has_monitor && rule->monitor <= wm->model.monitor_count) {
            policy.monitor = &wm->model.monitors[rule->monitor - 1];
            if (!policy.workspace_explicit)
                policy.workspace_index = policy.monitor->active_workspace->index;
        }
        if (rule->has_workspace) {
            policy.workspace_index = rule->workspace - 1;
            policy.workspace_explicit = true;
        }
        if (rule->has_focus_on_map) policy.focus_on_map = rule->focus_on_map;
        if (rule->has_raise_on_map) policy.raise_on_map = rule->raise_on_map;
        if (rule->has_border) policy.border = rule->border;
        if (rule->has_placement) policy.placement = rule->placement;
        if (rule->has_fullscreen_policy)
            policy.fullscreen_policy = rule->fullscreen_policy;
    }
    return policy;
}

static void update_size_hints(WM *wm, Client *client)
{
    XSizeHints hints = {0};
    long supplied;
    if (!XGetWMNormalHints(wm->display, client->window, &hints, &supplied))
        hints.flags = 0;

    if (hints.flags & PBaseSize) {
        client->base_width = hints.base_width;
        client->base_height = hints.base_height;
    } else if (hints.flags & PMinSize) {
        client->base_width = hints.min_width;
        client->base_height = hints.min_height;
    } else {
        client->base_width = 0;
        client->base_height = 0;
    }
    if (hints.flags & PMinSize) {
        client->minimum_width = hints.min_width;
        client->minimum_height = hints.min_height;
    } else if (hints.flags & PBaseSize) {
        client->minimum_width = hints.base_width;
        client->minimum_height = hints.base_height;
    } else {
        client->minimum_width = 0;
        client->minimum_height = 0;
    }
    if (hints.flags & PMaxSize) {
        client->maximum_width = hints.max_width;
        client->maximum_height = hints.max_height;
    } else {
        client->maximum_width = 0;
        client->maximum_height = 0;
    }
    if (hints.flags & PResizeInc) {
        client->width_increment = hints.width_inc;
        client->height_increment = hints.height_inc;
    } else {
        client->width_increment = 0;
        client->height_increment = 0;
    }
    if ((hints.flags & PAspect) &&
        hints.min_aspect.x > 0 && hints.min_aspect.y > 0 &&
        hints.max_aspect.x > 0 && hints.max_aspect.y > 0) {
        client->minimum_aspect =
            (double)hints.min_aspect.y / hints.min_aspect.x;
        client->maximum_aspect =
            (double)hints.max_aspect.x / hints.max_aspect.y;
    } else {
        client->minimum_aspect = 0.0;
        client->maximum_aspect = 0.0;
    }
    client->size_hints_valid = true;
}

static void apply_normal_hints(WM *wm, Client *client, int *width, int *height)
{
    if (!client->size_hints_valid) update_size_hints(wm, client);
    bool base_is_min = client->base_width == client->minimum_width &&
                       client->base_height == client->minimum_height;

    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
    if (!base_is_min) {
        *width -= client->base_width;
        *height -= client->base_height;
    }
    if (client->minimum_aspect > 0.0 && client->maximum_aspect > 0.0 &&
        *width > 0 && *height > 0) {
        if (client->maximum_aspect < (double)*width / *height)
            *width = (int)(*height * client->maximum_aspect + 0.5);
        else if (client->minimum_aspect < (double)*height / *width)
            *height = (int)(*width * client->minimum_aspect + 0.5);
    }
    if (base_is_min) {
        *width -= client->base_width;
        *height -= client->base_height;
    }
    if (client->width_increment > 0)
        *width -= *width % client->width_increment;
    if (client->height_increment > 0)
        *height -= *height % client->height_increment;
    *width += client->base_width;
    *height += client->base_height;
    if (*width < client->minimum_width) *width = client->minimum_width;
    if (*height < client->minimum_height) *height = client->minimum_height;
    if (client->maximum_width > 0 && *width > client->maximum_width)
        *width = client->maximum_width;
    if (client->maximum_height > 0 && *height > client->maximum_height)
        *height = client->maximum_height;
}

static Rect initial_geometry(WM *wm, Client *client, const Monitor *monitor,
                             const XWindowAttributes *attrs,
                             PlacementPolicy placement, unsigned int border_width)
{
    int width = attrs->width;
    int height = attrs->height;
    apply_normal_hints(wm, client, &width, &height);
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    Rect geometry;
    if (placement == PLACEMENT_CLIENT) {
        geometry = (Rect){attrs->x, attrs->y, width, height};
    } else {
        geometry = (Rect){
            monitor->workarea.x + (monitor->workarea.width - width) / 2,
            monitor->workarea.y + (monitor->workarea.height - height) / 2,
            width,
            height,
        };
    }

    Rect area = monitor->workarea;
    int outer_width = geometry.width + 2 * (int)border_width;
    int outer_height = geometry.height + 2 * (int)border_width;
    if ((long)geometry.x + outer_width <= area.x) {
        geometry.x = area.x;
    } else if (geometry.x >= area.x + area.width) {
        geometry.x = outer_width >= area.width
            ? area.x : area.x + area.width - outer_width;
    }
    if ((long)geometry.y + outer_height <= area.y) {
        geometry.y = area.y;
    } else if (geometry.y >= area.y + area.height) {
        geometry.y = outer_height >= area.height
            ? area.y : area.y + area.height - outer_height;
    }
    return geometry;
}

static void grab_client_buttons(WM *wm, Client *client, bool focused)
{
    unsigned int event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    XUngrabButton(wm->display, AnyButton, AnyModifier, client->window);
    if (wm->config.focus_mode == FOCUS_CLICK && !focused) {
        XGrabButton(wm->display, AnyButton, AnyModifier, client->window, False,
                    event_mask, GrabModeSync, GrabModeAsync, None, None);
        return;
    }
    unsigned int ignored[] = {
        0, LockMask, wm->numlock_mask, LockMask | wm->numlock_mask,
    };
    for (unsigned int binding = 0; binding < wm->config.mouse_binding_count; ++binding) {
        MouseBinding *mouse = &wm->config.mouse_bindings[binding];
        for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); ++i)
            XGrabButton(wm->display, mouse->button,
                        mouse->modifiers | ignored[i], client->window, False,
                        event_mask, GrabModeSync, GrabModeAsync, None, None);
    }
}

static void manage_special_window(WM *wm, Window window, WindowType type,
                                  bool map_window)
{
    if (find_special_window(&wm->model, window)) {
        if (map_window) XMapWindow(wm->display, window);
        return;
    }
    SpecialWindow *special = calloc(1, sizeof(*special));
    if (!special) {
        fprintf(stderr, "box2430: out of memory managing special window\n");
        wm->running = false;
        return;
    }
    special->window = window;
    special->type = type;
    if (type == WINDOW_TYPE_DOCK)
        special->has_strut = x11_read_strut(wm, window, special->strut);
    special->next = wm->model.special_windows;
    wm->model.special_windows = special;
    XSelectInput(wm->display, window, PropertyChangeMask);
    XSetWindowBorderWidth(wm->display, window, 0);
    x11_set_wm_state(wm, window, NormalState);
    if (map_window) XMapWindow(wm->display, window);
    enforce_stacking(wm);
    if (type == WINDOW_TYPE_DOCK) recompute_workareas(wm);
    x11_update_client_lists(wm);
}

static void unmanage_special_window(WM *wm, SpecialWindow *special, bool withdrawn)
{
    SpecialWindow **link = &wm->model.special_windows;
    while (*link && *link != special) link = &(*link)->next;
    if (*link) *link = special->next;
    bool was_dock = special->type == WINDOW_TYPE_DOCK;
    if (withdrawn) x11_set_wm_state(wm, special->window, WithdrawnState);
    free(special);
    if (was_dock) recompute_workareas(wm);
    enforce_stacking(wm);
    x11_update_client_lists(wm);
}

static void manage_window(WM *wm, Window window, bool map_window)
{
    Client *existing = find_client(&wm->model, window);
    if (existing) {
        if (map_window) reconcile_client_mapping(wm, existing);
        return;
    }

    XWindowAttributes attrs;
    if (!XGetWindowAttributes(wm->display, window, &attrs) || attrs.override_redirect ||
        attrs.class == InputOnly) {
        return;
    }
    WindowType type = x11_read_window_type(wm, window);
    if (type == WINDOW_TYPE_DOCK || type == WINDOW_TYPE_DESKTOP ||
        type == WINDOW_TYPE_NOTIFICATION) {
        manage_special_window(wm, window, type, map_window);
        return;
    }

    Client *client = calloc(1, sizeof(*client));
    if (!client) {
        fprintf(stderr, "box2430: out of memory managing window 0x%lx\n", window);
        wm->running = false;
        return;
    }
    client->window = window;
    client->title = x11_read_window_title(wm, window);
    x11_read_window_class(wm, window, &client->instance, &client->class_name);
    client->window_type = type;
    read_transient_for(wm, client);
    if (!client->title || !client->instance || !client->class_name) {
        fprintf(stderr, "box2430: out of memory reading window metadata\n");
        free(client->title);
        free(client->instance);
        free(client->class_name);
        free(client);
        wm->running = false;
        return;
    }
    Client *transient_parent = find_client(&wm->model, client->transient_for);
    InitialPolicy policy = initial_policy(wm, client, transient_parent);
    client->workspace = &policy.monitor->workspaces[policy.workspace_index];
    client->border_enabled = policy.border;
    client->original_border_width = (unsigned int)attrs.border_width;
    client->fullscreen_policy = policy.fullscreen_policy;
    unsigned int border_width = client_free_border_width(wm, client);
    client->geometry = initial_geometry(wm, client, policy.monitor, &attrs,
                                        policy.placement, border_width);
    client->normal_geometry = client->geometry;
    client->next = wm->model.clients;
    wm->model.clients = client;
    append_workspace_orders(client->workspace, client);

    XSelectInput(wm->display, window,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask);
    grab_client_buttons(wm, client, false);
    materialize_client_geometry(wm, client);
    x11_set_wm_state(wm, window, NormalState);
    read_wm_hints(wm, client);
    read_wm_protocols(wm, client);
    bool workspace_active = client_workspace_is_active(client);
    bool activate_on_map = policy.focus_on_map && workspace_active &&
        client_can_focus(client);
    bool monocle_activation = activate_on_map &&
        client->workspace->mode == WORKSPACE_MONOCLE;

    /* A MONOCLE client that will become active must be mapped before
     * client_activate() projects X input focus. Other mapping decisions can
     * follow the steady-state workspace projection directly. */
    if (monocle_activation) {
        if (attrs.map_state == IsUnmapped) project_client_mapped(wm, client);
    } else {
        reconcile_client_mapping(wm, client);
    }
    if (policy.raise_on_map && !monocle_activation) {
        client_raise(wm, client);
    }
    if (activate_on_map) {
        client_activate(wm, client, CurrentTime);
        if (client->workspace->mode == WORKSPACE_MONOCLE) {
            client_raise(wm, client);
            reconcile_workspace_mapping(wm, client->workspace);
        }
    }
    if (x11_window_requests_fullscreen(wm, window))
        client_set_requested_fullscreen(wm, client, true);
    ui_update(wm);
    x11_update_client_lists(wm);
}

static void unmanage_client(WM *wm, Client *client, bool withdrawn)
{
    Workspace *workspace = client->workspace;
    resolve_focus_before_client_removal(wm, client);
    unlink_workspace_orders(workspace, client);
    if (workspace->mode == WORKSPACE_MONOCLE &&
        workspace == workspace->monitor->active_workspace)
        reconcile_workspace_mapping(wm, workspace);

    Client **link = &wm->model.clients;
    while (*link && *link != client) {
        link = &(*link)->next;
    }
    if (*link) {
        *link = client->next;
    }
    if (withdrawn) {
        XWindowChanges changes = {
            .border_width = (int)client->original_border_width,
        };
        XGrabServer(wm->display);
        XErrorHandler previous_handler = XSetErrorHandler(ignore_x11_error);
        XSelectInput(wm->display, client->window, NoEventMask);
        XConfigureWindow(wm->display, client->window, CWBorderWidth, &changes);
        XUngrabButton(wm->display, AnyButton, AnyModifier, client->window);
        x11_set_wm_state(wm, client->window, WithdrawnState);
        XSync(wm->display, False);
        XSetErrorHandler(previous_handler);
        XUngrabServer(wm->display);
    }
    free(client->title);
    free(client->class_name);
    free(client->instance);
    free(client);
    ui_bar_update(wm);
    enforce_stacking(wm);
    x11_update_client_lists(wm);
}

static bool rect_equal(Rect left, Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

typedef struct TopologyClientPlan {
    Client *client;
    unsigned int old_monitor_index;
    unsigned int new_monitor_index;
    unsigned int workspace_index;
    bool migrate;
    bool adjust_geometry;
} TopologyClientPlan;

typedef struct MonitorTopologyPlan {
    unsigned int old_count;
    unsigned int new_count;
    Rect old_rects[BOX2430_MAX_MONITORS];
    Rect new_rects[BOX2430_MAX_MONITORS];
    int old_for_new[BOX2430_MAX_MONITORS];
    int new_for_old[BOX2430_MAX_MONITORS];
    unsigned int fallback_new_index;
    unsigned int selected_new_index;
    int preview_new_index;
    Client *preferred_focus;
    TopologyClientPlan *clients;
    unsigned int client_count;
} MonitorTopologyPlan;

typedef enum MonitorTopologyPlanResult {
    MONITOR_TOPOLOGY_PLAN_FAILED,
    MONITOR_TOPOLOGY_NO_CHANGE,
    MONITOR_TOPOLOGY_METADATA_ONLY,
    MONITOR_TOPOLOGY_SEMANTIC_CHANGE,
} MonitorTopologyPlanResult;

static void free_topology_plan(MonitorTopologyPlan *plan)
{
    free(plan->clients);
    plan->clients = NULL;
    plan->client_count = 0;
}

static MonitorTopologyPlanResult plan_monitor_topology(
    WM *wm, const RandRMonitorSnapshot *new_snapshot,
    MonitorTopologyPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    if (wm->monitor_snapshot.count != wm->model.monitor_count ||
        !new_snapshot->count ||
        new_snapshot->count > BOX2430_MAX_MONITORS) {
        fprintf(stderr, "box2430: cannot plan invalid RandR monitor snapshot\n");
        return MONITOR_TOPOLOGY_PLAN_FAILED;
    }
    if (randr_monitor_snapshots_equal(&wm->monitor_snapshot, new_snapshot))
        return MONITOR_TOPOLOGY_NO_CHANGE;

    plan->old_count = wm->model.monitor_count;
    plan->new_count = new_snapshot->count;
    for (unsigned int i = 0; i < plan->old_count; ++i)
        plan->old_rects[i] = wm->model.monitors[i].geometry;
    for (unsigned int i = 0; i < plan->new_count; ++i)
        plan->new_rects[i] = new_snapshot->monitors[i].geometry;

    match_monitor_observations(
        wm->monitor_snapshot.monitors, plan->old_count,
        new_snapshot->monitors, plan->new_count,
        plan->old_for_new, plan->new_for_old);

    bool changed = plan->old_count != plan->new_count;
    for (unsigned int new_index = 0; new_index < plan->new_count; ++new_index) {
        int old_index = plan->old_for_new[new_index];
        if (old_index < 0 || old_index != (int)new_index ||
            !rect_equal(plan->old_rects[old_index], plan->new_rects[new_index])) {
            changed = true;
        }
    }
    if (!changed) return MONITOR_TOPOLOGY_METADATA_ONLY;

    plan->fallback_new_index = 0;
    unsigned int selected_old_index = wm->model.selected_monitor
        ? wm->model.selected_monitor->index : 0;
    int selected_new_index = selected_old_index < plan->old_count
        ? plan->new_for_old[selected_old_index] : -1;
    plan->selected_new_index = selected_new_index >= 0
        ? (unsigned int)selected_new_index : plan->fallback_new_index;

    plan->preview_new_index = -1;
    if (wm->drag.preview_monitor) {
        unsigned int preview_old_index = wm->drag.preview_monitor->index;
        if (preview_old_index < plan->old_count)
            plan->preview_new_index = plan->new_for_old[preview_old_index];
    }
    plan->preferred_focus = wm->model.focused_client;

    unsigned int client_count = 0;
    for (Client *client = wm->model.clients; client; client = client->next)
        ++client_count;
    if (!client_count) return MONITOR_TOPOLOGY_SEMANTIC_CHANGE;

    plan->clients = calloc(client_count, sizeof(*plan->clients));
    if (!plan->clients) {
        fprintf(stderr, "box2430: out of memory planning monitor topology\n");
        return MONITOR_TOPOLOGY_PLAN_FAILED;
    }
    plan->client_count = client_count;

    unsigned int i = 0;
    for (Client *client = wm->model.clients; client; client = client->next, ++i) {
        TopologyClientPlan *client_plan = &plan->clients[i];
        unsigned int old_index = client->workspace->monitor->index;
        int continued_new = old_index < plan->old_count
            ? plan->new_for_old[old_index] : -1;
        unsigned int new_index = continued_new >= 0
            ? (unsigned int)continued_new : plan->fallback_new_index;
        client_plan->client = client;
        client_plan->old_monitor_index = old_index;
        client_plan->new_monitor_index = new_index;
        client_plan->workspace_index = client->workspace->index;
        client_plan->migrate = continued_new < 0;
        client_plan->adjust_geometry = client_plan->migrate ||
            !rect_equal(plan->old_rects[old_index], plan->new_rects[new_index]);
    }
    return MONITOR_TOPOLOGY_SEMANTIC_CHANGE;
}

static void translate_client_latent_geometry(Client *client, Rect old_monitor,
                                             Rect new_monitor)
{
    int dx = new_monitor.x - old_monitor.x;
    int dy = new_monitor.y - old_monitor.y;
    client->geometry.x += dx;
    client->geometry.y += dy;
    client->normal_geometry.x += dx;
    client->normal_geometry.y += dy;
}

static void clamp_client_latent_geometry(WM *wm, Client *client, Rect workarea)
{
    unsigned int border_width = client_free_border_width(wm, client);
    client->geometry = clamp_to_workarea(client->geometry, workarea, border_width);
    client->normal_geometry = clamp_to_workarea(client->normal_geometry, workarea,
                                                 border_width);
}

static void retarget_monitor_workspaces(WM *wm, Monitor *monitor)
{
    for (unsigned int i = 0; i < wm->config.workspace_count; ++i)
        monitor->workspaces[i].monitor = monitor;
}

static void destroy_removed_monitor_resources(WM *wm, Monitor *old_monitors,
                                              const MonitorTopologyPlan *plan)
{
    for (unsigned int old_index = 0; old_index < plan->old_count; ++old_index) {
        if (plan->new_for_old[old_index] >= 0) continue;
        Monitor *removed = &old_monitors[old_index];
        ui_bar_destroy_monitor(wm, removed);
        ui_tab_destroy_monitor(wm, removed);
        free(removed->workspaces);
    }
}

static void accept_monitor_snapshot(WM *wm,
                                    RandRMonitorSnapshot *new_snapshot)
{
    RandRMonitorSnapshot old_snapshot = wm->monitor_snapshot;
    wm->monitor_snapshot = *new_snapshot;
    memset(new_snapshot, 0, sizeof(*new_snapshot));
    randr_free_monitor_snapshot(&old_snapshot);
}

static void reconcile_monitors(WM *wm)
{
    RandRMonitorSnapshot new_snapshot = {0};
    if (!randr_query_monitor_snapshot(wm, &new_snapshot)) return;

    MonitorTopologyPlan plan;
    MonitorTopologyPlanResult result = plan_monitor_topology(
        wm, &new_snapshot, &plan);
    if (result == MONITOR_TOPOLOGY_NO_CHANGE) {
        randr_free_monitor_snapshot(&new_snapshot);
        return;
    }
    if (result == MONITOR_TOPOLOGY_METADATA_ONLY) {
        accept_monitor_snapshot(wm, &new_snapshot);
        return;
    }
    if (result == MONITOR_TOPOLOGY_PLAN_FAILED) {
        randr_free_monitor_snapshot(&new_snapshot);
        return;
    }

    Monitor old_monitors[BOX2430_MAX_MONITORS] = {0};
    Monitor staged[BOX2430_MAX_MONITORS] = {0};
    bool added[BOX2430_MAX_MONITORS] = {false};
    for (unsigned int i = 0; i < plan.old_count; ++i)
        old_monitors[i] = wm->model.monitors[i];

    /* Stage the complete future monitor array before mutating ownership. */
    for (unsigned int new_index = 0; new_index < plan.new_count; ++new_index) {
        int old_index = plan.old_for_new[new_index];
        if (old_index >= 0) {
            staged[new_index] = old_monitors[old_index];
            staged[new_index].index = new_index;
            staged[new_index].geometry = plan.new_rects[new_index];
            staged[new_index].workarea = plan.new_rects[new_index];
        } else {
            if (!init_monitor_state(wm, &staged[new_index], new_index,
                                    plan.new_rects[new_index])) {
                fprintf(stderr, "box2430: cannot create state for added monitor\n");
                for (unsigned int j = 0; j < new_index; ++j)
                    if (added[j]) free(staged[j].workspaces);
                free_topology_plan(&plan);
                randr_free_monitor_snapshot(&new_snapshot);
                return;
            }
            added[new_index] = true;
        }
    }

    /* Semantic client ownership and latent geometry are updated without
     * invoking focus, mapping, stacking, or presentation helpers. */
    for (unsigned int i = 0; i < plan.client_count; ++i) {
        TopologyClientPlan *client_plan = &plan.clients[i];
        Client *client = client_plan->client;
        if (client_plan->adjust_geometry) {
            translate_client_latent_geometry(
                client, plan.old_rects[client_plan->old_monitor_index],
                plan.new_rects[client_plan->new_monitor_index]);
        }
        if (client_plan->migrate) {
            Workspace *destination =
                &staged[client_plan->new_monitor_index]
                     .workspaces[client_plan->workspace_index];
            reassign_client_workspace_authority(client, destination);
        }
    }

    for (unsigned int i = 0; i < plan.new_count; ++i)
        wm->model.monitors[i] = staged[i];
    for (unsigned int i = plan.new_count; i < BOX2430_MAX_MONITORS; ++i)
        memset(&wm->model.monitors[i], 0, sizeof(wm->model.monitors[i]));
    wm->model.monitor_count = plan.new_count;
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        wm->model.monitors[i].index = i;
        retarget_monitor_workspaces(wm, &wm->model.monitors[i]);
    }
    set_selected_monitor_authority(
        &wm->model, &wm->model.monitors[plan.selected_new_index]);
    accept_monitor_snapshot(wm, &new_snapshot);

    ui_snap_preview_hide(wm);
    wm->drag.preview_monitor = plan.preview_new_index >= 0
        ? &wm->model.monitors[plan.preview_new_index] : NULL;
    wm->drag.preview_snap = SNAP_NONE;
    wm->drag.preview_maximized = false;

    destroy_removed_monitor_resources(wm, old_monitors, &plan);

    /* Workareas are computed only after the logical monitor/workspace world is
     * coherent. Added UI windows are then created against final monitor state. */
    calculate_workareas(wm);
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        if (wm->bar_resources_ready && added[i] &&
            !ui_bar_create_monitor(wm, &wm->model.monitors[i])) {
            fprintf(stderr, "box2430: cannot create bar for added monitor\n");
            free_topology_plan(&plan);
            wm->running = false;
            return;
        }
        if (wm->tab_resources_ready && added[i] &&
            !ui_tab_create_monitor(wm, &wm->model.monitors[i])) {
            fprintf(stderr, "box2430: cannot create tab bar for added monitor\n");
            free_topology_plan(&plan);
            wm->running = false;
            return;
        }
        if (wm->bar_resources_ready) ui_bar_name_monitor(wm, &wm->model.monitors[i]);
        if (wm->tab_resources_ready) ui_tab_name_monitor(wm, &wm->model.monitors[i]);
    }

    for (unsigned int i = 0; i < plan.client_count; ++i) {
        TopologyClientPlan *client_plan = &plan.clients[i];
        if (!client_plan->adjust_geometry) continue;
        clamp_client_latent_geometry(
            wm, client_plan->client,
            wm->model.monitors[client_plan->new_monitor_index].workarea);
    }

    Client *preferred_focus = plan.preferred_focus;
    bool preserve_preferred_focus = preferred_focus &&
        client_workspace_is_active(preferred_focus) &&
        client_can_focus(preferred_focus);
    if (preserve_preferred_focus)
        promote_workspace_focus(preferred_focus->workspace, preferred_focus);

    rematerialize_all_clients(wm);
    for (Client *client = wm->model.clients; client; client = client->next)
        reconcile_client_mapping(wm, client);

    if (preserve_preferred_focus) {
        set_selected_monitor_authority(&wm->model, preferred_focus->workspace->monitor);
        x11_update_active_window(wm);
    } else {
        client_activate(wm, NULL, CurrentTime);
        Workspace *workspace = wm->model.selected_monitor->active_workspace;
        client_activate(wm, workspace_focus_target(workspace), CurrentTime);
    }

    x11_update_workarea(wm);
    ui_update(wm);
    enforce_stacking(wm);
    x11_update_client_lists(wm);
    free_topology_plan(&plan);
}

static void handle_configure_request(WM *wm, XConfigureRequestEvent *event)
{
    Client *client = find_client(&wm->model, event->window);
    XWindowChanges changes = {
        .x = event->x, .y = event->y, .width = event->width,
        .height = event->height, .border_width = event->border_width,
        .sibling = event->above, .stack_mode = event->detail,
    };
    if (!client) {
        XConfigureWindow(wm->display, event->window, (unsigned int)event->value_mask,
                         &changes);
        return;
    }

    unsigned long geometry_mask =
        (unsigned long)event->value_mask & (CWX | CWY | CWWidth | CWHeight);
    if (!client_geometry_is_wm_owned(client) && geometry_mask != 0) {
        Rect geometry = client->geometry;
        if (event->value_mask & CWX) geometry.x = event->x;
        if (event->value_mask & CWY) geometry.y = event->y;
        if (event->value_mask & CWWidth) geometry.width = event->width;
        if (event->value_mask & CWHeight) geometry.height = event->height;
        if (event->value_mask & (CWWidth | CWHeight))
            apply_normal_hints(wm, client, &geometry.width, &geometry.height);
        if (geometry.width < 1) geometry.width = 1;
        if (geometry.height < 1) geometry.height = 1;
        client->normal_geometry = geometry;
        commit_client_geometry(wm, client, geometry);
    }

    /* CWBorderWidth is intentionally ignored for managed clients. */

    XWindowAttributes attrs;
    if (XGetWindowAttributes(wm->display, client->window, &attrs)) {
        XEvent notification = {0};
        notification.xconfigure.type = ConfigureNotify;
        notification.xconfigure.display = wm->display;
        notification.xconfigure.event = client->window;
        notification.xconfigure.window = client->window;
        notification.xconfigure.x = attrs.x;
        notification.xconfigure.y = attrs.y;
        notification.xconfigure.width = attrs.width;
        notification.xconfigure.height = attrs.height;
        notification.xconfigure.border_width = attrs.border_width;
        notification.xconfigure.above = None;
        notification.xconfigure.override_redirect = False;
        XSendEvent(wm->display, client->window, False, StructureNotifyMask,
                   &notification);
    }
}

static void handle_event(WM *wm, XEvent *event)
{
    TrayEventResult tray_result = tray_handle_event(wm, event);
    if (tray_result & TRAY_EVENT_CONSUMED) {
        if (tray_result & TRAY_EVENT_CHANGED) enforce_stacking(wm);
        WM_CHECK_INVARIANTS(wm);
        return;
    }
    Client *client;
    SpecialWindow *special;
    switch (event->type) {
    /* Client requests are interpreted through current management policy. */
    case MapRequest:
        manage_window(wm, event->xmaprequest.window, true);
        break;
    case ConfigureRequest:
        handle_configure_request(wm, &event->xconfigurerequest);
        break;
    case ConfigureNotify:
        /* Root geometry is an X observation; reconciliation owns any
         * resulting topology transition. */
        if (event->xconfigure.window == wm->root) reconcile_monitors(wm);
        break;
    /* Lifecycle observations may consume WM projection causality or trigger
     * the established unmanage transition. */
    case DestroyNotify:
        client = find_client(&wm->model, event->xdestroywindow.window);
        if (client) unmanage_client(wm, client, false);
        else {
            special = find_special_window(&wm->model, event->xdestroywindow.window);
            if (special) unmanage_special_window(wm, special, false);
        }
        break;
    case UnmapNotify:
        client = find_client(&wm->model, event->xunmap.window);
        if (client && event->xunmap.send_event) {
            unmanage_client(wm, client, true);
        } else if (client && client->ignored_unmaps) {
            --client->ignored_unmaps;
        } else if (client) {
            unmanage_client(wm, client, true);
        } else if (!client && !event->xunmap.send_event) {
            special = find_special_window(&wm->model, event->xunmap.window);
            if (special) unmanage_special_window(wm, special, true);
        }
        break;
    case KeyPress:
        handle_key_press(wm, &event->xkey);
        break;
    case MappingNotify:
        XRefreshKeyboardMapping(&event->xmapping);
        if (event->xmapping.request != MappingPointer) {
            grab_default_keys(wm);
            for (Client *mapped = wm->model.clients; mapped; mapped = mapped->next)
                grab_client_buttons(wm, mapped, mapped == wm->model.focused_client);
        }
        break;
    case ButtonPress:
        {
        /* Pointer hit testing supplies context for user-intent transitions. */
        if (event->xbutton.window == wm->root) {
            if (event->xbutton.button == Button1 &&
                event->xbutton.subwindow == None) {
                Monitor *monitor = monitor_at_point(
                    wm, event->xbutton.x_root, event->xbutton.y_root);
                if (monitor != wm->model.selected_monitor) {
                    monitor_activate(wm, monitor, event->xbutton.time);
                }
            }
            break;
        }
        Monitor *bar_monitor = ui_bar_monitor_for_window(wm, event->xbutton.window);
        if (bar_monitor) {
            Workspace *workspace = ui_bar_workspace_hit_test(
                wm, bar_monitor, event->xbutton.x);
            MouseBinding *matched = NULL;
            if (workspace) {
                for (unsigned int i = 0;
                     i < wm->config.workspacebar_binding_count; ++i) {
                    if (wm->config.workspacebar_bindings[i].button ==
                        event->xbutton.button) {
                        matched = &wm->config.workspacebar_bindings[i];
                        break;
                    }
                }
            }
            if (matched) {
                const char *argv[BOX2430_MAX_COMMAND_ARGS];
                for (int i = 0; i < matched->argc; ++i)
                    argv[i] = matched->argv[i];
                CommandContext context = {
                    .type = COMMAND_CONTEXT_WORKSPACEBAR,
                    .monitor = bar_monitor,
                    .workspace = workspace,
                    .root_x = event->xbutton.x_root,
                    .root_y = event->xbutton.y_root,
                    .time = event->xbutton.time,
                };
                command_run(wm, &context, matched->argc, argv);
            }
            break;
        }
        Monitor *tab_monitor = ui_tab_monitor_for_window(wm, event->xbutton.window);
        if (tab_monitor) {
            MouseBinding *matched = NULL;
            for (unsigned int i = 0; i < wm->config.tab_binding_count; ++i) {
                if (wm->config.tab_bindings[i].button == event->xbutton.button) {
                    matched = &wm->config.tab_bindings[i];
                    break;
                }
            }
            if (matched) {
                const char *argv[BOX2430_MAX_COMMAND_ARGS];
                for (int i = 0; i < matched->argc; ++i) argv[i] = matched->argv[i];
                CommandContext context = {
                    .type = COMMAND_CONTEXT_TABBAR,
                    .client = ui_tab_hit_test(wm, tab_monitor, event->xbutton.x),
                    .monitor = tab_monitor,
                    .workspace = tab_monitor->active_workspace,
                    .root_x = event->xbutton.x_root,
                    .root_y = event->xbutton.y_root,
                    .time = event->xbutton.time,
                };
                command_run(wm, &context, matched->argc, argv);
            }
            break;
        }
        client = find_client(&wm->model, event->xbutton.window);
        if (client) {
            unsigned int state = event->xbutton.state &
                                 ~(LockMask | wm->numlock_mask);
            MouseBinding *matched = NULL;
            for (unsigned int i = 0; i < wm->config.mouse_binding_count; ++i) {
                MouseBinding *binding = &wm->config.mouse_bindings[i];
                if (binding->modifiers == state && binding->button == event->xbutton.button) {
                    matched = binding;
                    break;
                }
            }
            client_activate(wm, client, event->xbutton.time);
            if (matched) {
                const char *argv[BOX2430_MAX_COMMAND_ARGS];
                for (int i = 0; i < matched->argc; ++i) argv[i] = matched->argv[i];
                CommandContext context = {
                    .type = COMMAND_CONTEXT_MOUSE,
                    .client = client,
                    .root_x = event->xbutton.x_root,
                    .root_y = event->xbutton.y_root,
                    .time = event->xbutton.time,
                };
                command_run(wm, &context, matched->argc, argv);
                XAllowEvents(wm->display, AsyncPointer, event->xbutton.time);
            } else {
                XAllowEvents(wm->display, ReplayPointer, event->xbutton.time);
            }
        }
        break;
        }
    case MotionNotify:
        while (XCheckTypedEvent(wm->display, MotionNotify, event)) {}
        update_drag(wm, event->xmotion.x_root, event->xmotion.y_root);
        break;
    case ButtonRelease:
        finish_drag(wm);
        break;
    case EnterNotify:
        client = find_client(&wm->model, event->xcrossing.window);
        if (client && wm->config.focus_mode == FOCUS_SLOPPY &&
            client != wm->model.focused_client &&
            event->xcrossing.mode == NotifyNormal &&
            event->xcrossing.detail != NotifyInferior)
            client_activate(wm, client, event->xcrossing.time);
        break;
    case FocusIn:
        /* Observation only: repair X focus from semantic Authority. */
        if (wm->model.focused_client &&
            event->xfocus.window != wm->model.focused_client->window &&
            event->xfocus.mode == NotifyNormal &&
            event->xfocus.detail != NotifyPointer &&
            event->xfocus.detail != NotifyPointerRoot &&
            event->xfocus.detail != NotifyDetailNone) {
            project_semantic_input_focus(wm, CurrentTime);
        }
        break;
    case PropertyNotify:
        if (event->xproperty.window == wm->root &&
            (event->xproperty.atom == wm->atoms.net_wm_name ||
             event->xproperty.atom == XA_WM_NAME)) {
            ui_status_refresh(wm);
            break;
        }
        client = find_client(&wm->model, event->xproperty.window);
        if (client && event->xproperty.atom == XA_WM_NORMAL_HINTS) {
            client->size_hints_valid = false;
        } else if (client && event->xproperty.atom == XA_WM_HINTS) {
            read_wm_hints(wm, client);
            ui_update(wm);
        } else if (client && event->xproperty.atom == wm->atoms.wm_protocols) {
            read_wm_protocols(wm, client);
        } else if (client && (event->xproperty.atom == wm->atoms.net_wm_name ||
                              event->xproperty.atom == XA_WM_NAME)) {
            char *title = x11_read_window_title(wm, client->window);
            if (title) {
                free(client->title);
                client->title = title;
                ui_update(wm);
            }
        } else if (client && event->xproperty.atom == XA_WM_CLASS) {
            char *instance = NULL;
            char *class_name = NULL;
            x11_read_window_class(wm, client->window, &instance, &class_name);
            if (instance && class_name) {
                free(client->instance);
                free(client->class_name);
                client->instance = instance;
                client->class_name = class_name;
                ui_update(wm);
            } else {
                free(instance);
                free(class_name);
            }
        } else if (client && event->xproperty.atom == XA_WM_TRANSIENT_FOR) {
            read_transient_for(wm, client);
            client->window_type = x11_read_window_type(wm, client->window);
        } else if (client && event->xproperty.atom == wm->atoms.net_wm_window_type) {
            client->window_type = x11_read_window_type(wm, client->window);
        } else if (!client && (event->xproperty.atom == wm->atoms.net_wm_strut ||
                               event->xproperty.atom == wm->atoms.net_wm_strut_partial)) {
            special = find_special_window(&wm->model, event->xproperty.window);
            if (special && special->type == WINDOW_TYPE_DOCK) {
                special->has_strut = x11_read_strut(wm, special->window,
                                                    special->strut);
                recompute_workareas(wm);
            }
        }
        break;
    case ClientMessage:
        /* EWMH messages remain client requests subject to existing policy. */
        client = find_client(&wm->model, event->xclient.window);
        if (event->xclient.message_type == wm->atoms.net_active_window && client &&
            client != wm->model.focused_client) {
            if (wm->config.active_window_policy == ACTIVE_WINDOW_URGENT) {
                set_client_urgent(wm, client, true);
            } else if (client->workspace ==
                       client->workspace->monitor->active_workspace) {
                if (client->workspace->mode == WORKSPACE_MONOCLE)
                    client_focus_tab_target(wm, client, CurrentTime);
                else
                    client_activate(wm, client, CurrentTime);
            }
        } else if (event->xclient.message_type == wm->atoms.net_close_window && client) {
            client_close(wm, client);
        } else if (event->xclient.message_type == wm->atoms.net_wm_state && client) {
            long action = event->xclient.data.l[0];
            Atom state1 = (Atom)event->xclient.data.l[1];
            Atom state2 = (Atom)event->xclient.data.l[2];
            if (action < 0 || action > 2) break;
            if (state1 == wm->atoms.net_wm_state_fullscreen ||
                state2 == wm->atoms.net_wm_state_fullscreen) {
                bool requested = action == 1 ||
                                 (action == 2 && !client->client_fullscreen);
                client_set_requested_fullscreen(wm, client, requested);
            }
            if (state1 == wm->atoms.net_wm_state_maximized_horz ||
                state1 == wm->atoms.net_wm_state_maximized_vert ||
                state2 == wm->atoms.net_wm_state_maximized_horz ||
                state2 == wm->atoms.net_wm_state_maximized_vert) {
                bool requested = action == 1 || (action == 2 && !client->maximized);
                client_set_maximized(wm, client, requested);
            }
        }
        break;
    case Expose:
        {
        Monitor *bar_monitor = ui_bar_monitor_for_window(wm, event->xexpose.window);
        if (bar_monitor && event->xexpose.count == 0) {
            ui_bar_draw(wm, bar_monitor);
            break;
        }
        Monitor *tab_monitor = ui_tab_monitor_for_window(wm, event->xexpose.window);
        if (tab_monitor && event->xexpose.count == 0) ui_tab_draw(wm, tab_monitor);
        break;
        }
    default:
        break;
    }
    WM_CHECK_INVARIANTS(wm);
}

static void discover_existing_windows(WM *wm)
{
    Window root_return;
    Window parent_return;
    Window *children = NULL;
    unsigned int count = 0;
    if (!XQueryTree(wm->display, wm->root, &root_return, &parent_return,
                    &children, &count)) {
        return;
    }

    /* A notification daemon may survive a WM restart while a notification is
     * visible.  Its override-redirect window is intentionally unmanaged, but
     * a freshly created native bar would otherwise start above it.  Remember
     * the lowest such notification as a one-shot startup ceiling. */
    Window override_notification = None;
    for (unsigned int i = 0; i < count; ++i) {
        XWindowAttributes attrs;
        if (!XGetWindowAttributes(wm->display, children[i], &attrs) ||
            !attrs.override_redirect || attrs.class == InputOnly ||
            attrs.map_state != IsViewable)
            continue;
        if (x11_read_window_type(wm, children[i]) == WINDOW_TYPE_NOTIFICATION) {
            override_notification = children[i];
            break;
        }
    }

    enum { SCAN_SPECIAL, SCAN_ORDINARY, SCAN_TRANSIENT };
    for (int pass = SCAN_SPECIAL; pass <= SCAN_TRANSIENT; ++pass) {
        for (unsigned int i = 0; i < count; ++i) {
            XWindowAttributes attrs;
            if (!XGetWindowAttributes(wm->display, children[i], &attrs) ||
                attrs.override_redirect || attrs.class == InputOnly) {
                continue;
            }
            WindowType type = x11_read_window_type(wm, children[i]);
            bool special = type == WINDOW_TYPE_DOCK ||
                type == WINDOW_TYPE_DESKTOP ||
                type == WINDOW_TYPE_NOTIFICATION;
            if (attrs.map_state != IsViewable &&
                (special || !x11_window_is_iconic(wm, children[i]))) {
                continue;
            }
            Window transient_for;
            bool transient = XGetTransientForHint(wm->display, children[i],
                                                   &transient_for);
            bool matches = pass == SCAN_SPECIAL ? special
                : pass == SCAN_ORDINARY ? !special && !transient &&
                    type != WINDOW_TYPE_DIALOG
                : !special && (transient || type == WINDOW_TYPE_DIALOG);
            if (matches) manage_window(wm, children[i], false);
        }
    }
    if (override_notification)
        enforce_stacking_below(wm, override_notification);
    XFree(children);
}


bool wm_init(WM *wm, const char *display_name, const char *config_path,
             bool session_start)
{
    config_set_defaults(&wm->config);
    config_load(&wm->config, config_path);
    wm->display = XOpenDisplay(display_name);
    if (!wm->display) {
        fprintf(stderr, "box2430: cannot open display %s\n",
                display_name ? display_name : "(default)");
        return false;
    }
    wm->screen = DefaultScreen(wm->display);
    wm->root = RootWindow(wm->display, wm->screen);
    wm->x_fd = ConnectionNumber(wm->display);
    wm->running = true;
    if (!randr_check_version(wm)) return false;
    if (!x11_acquire_wm_ownership(wm)) return false;
    if (!init_cursors(wm)) return false;
    if (session_start) {
        unsigned long background = named_color(
            wm, wm->config.background, BlackPixel(wm->display, wm->screen));
        XSetWindowBackground(wm->display, wm->root, background);
        XClearWindow(wm->display, wm->root);
    }
    x11_init_atoms(wm);
    x11_update_active_window(wm);
    if (!init_monitors(wm)) return false;
    recompute_workareas(wm);
    if (!ui_init(wm)) return false;
    if (!tray_init(wm)) return false;
    ui_bar_update(wm);

    grab_default_keys(wm);
    discover_existing_windows(wm);
    XSync(wm->display, False);
    WM_CHECK_INVARIANTS(wm);
    if (wm->config.bspwm_compat.enabled)
        wm->bspwm_compat = bspwm_compat_create(wm);
    return true;
}

void wm_run(WM *wm, const char *autostart_path)
{
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    struct sigaction child_action = {0};
    child_action.sa_handler = SIG_IGN;
    child_action.sa_flags = SA_NOCLDWAIT;
    sigemptyset(&child_action.sa_mask);
    sigaction(SIGCHLD, &child_action, NULL);

    if (autostart_path) spawn_autostart(wm, autostart_path);

    struct pollfd descriptors[1 + BSPWM_COMPAT_MAX_POLL_FDS];
    while (wm->running && !stop_requested) {
        while (XPending(wm->display)) {
            XEvent event;
            XNextEvent(wm->display, &event);
            handle_event(wm, &event);
        }
        if (!wm->running || stop_requested) break;
        bspwm_compat_publish(wm);
        ui_clock_tick(wm);
        int timeout = ui_clock_visible(wm) ? 1000 : -1;
        descriptors[0] = (struct pollfd){.fd = wm->x_fd, .events = POLLIN};
        size_t compat_count = bspwm_compat_pollfds(
            wm->bspwm_compat, descriptors + 1, BSPWM_COMPAT_MAX_POLL_FDS);
        int result = poll(descriptors, 1 + compat_count, timeout);
        if (result < 0 && errno != EINTR) {
            fprintf(stderr, "box2430: poll: %s\n", strerror(errno));
            break;
        }
        if (result > 0 && compat_count > 0) {
            bool transitioned = bspwm_compat_dispatch(
                wm, descriptors + 1, compat_count);
            if (transitioned) WM_CHECK_INVARIANTS(wm);
            bspwm_compat_publish(wm);
        }
    }
}

void wm_destroy(WM *wm)
{
    if (!wm->display) return;
    bspwm_compat_destroy(wm->bspwm_compat);
    wm->bspwm_compat = NULL;
    XSync(wm->display, False);
    if (wm->model.monitors && wm->model.monitor_count > 0) {
        while (XPending(wm->display)) {
            XEvent event;
            XNextEvent(wm->display, &event);
            if (event.type == DestroyNotify || event.type == UnmapNotify)
                handle_event(wm, &event);
        }
    }
    /* Once the WM relinquishes ownership there is no inactive workspace to
       keep clients hidden.  Remap them so a successor WM can discover every
       live client without Box2430-specific persistent state. */
    for (Client *client = wm->model.clients; client; client = client->next)
        project_client_mapped(wm, client);
    while (wm->model.clients) {
        unmanage_client(wm, wm->model.clients, true);
    }
    while (wm->model.special_windows)
        unmanage_special_window(wm, wm->model.special_windows, true);
    if (wm->atoms.net_active_window != None)
        XDeleteProperty(wm->display, wm->root, wm->atoms.net_active_window);
    if (wm->atoms.net_client_list != None)
        XDeleteProperty(wm->display, wm->root, wm->atoms.net_client_list);
    if (wm->atoms.net_client_list_stacking != None)
        XDeleteProperty(wm->display, wm->root,
                        wm->atoms.net_client_list_stacking);
    if (wm->atoms.net_supporting_wm_check != None)
        XDeleteProperty(wm->display, wm->root,
                        wm->atoms.net_supporting_wm_check);
    if (wm->atoms.net_supported != None)
        XDeleteProperty(wm->display, wm->root, wm->atoms.net_supported);
    if (wm->atoms.net_workarea != None)
        XDeleteProperty(wm->display, wm->root, wm->atoms.net_workarea);
    if (wm->wm_check_window) {
        XDestroyWindow(wm->display, wm->wm_check_window);
        wm->wm_check_window = None;
    }
    tray_destroy(wm);
    ui_destroy(wm);
    free_cursors(wm);
    XSync(wm->display, False);
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i)
        free(wm->model.monitors[i].workspaces);
    free(wm->model.monitors);
    wm->model.monitors = NULL;
    wm->model.monitor_count = 0;
    wm->model.selected_monitor = NULL;
    randr_free_monitor_snapshot(&wm->monitor_snapshot);
    XCloseDisplay(wm->display);
    wm->display = NULL;
}
