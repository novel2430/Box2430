#include "box2430.h"

#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <X11/keysym.h>
#include <errno.h>
#include <fnmatch.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stop_requested;

static void enforce_stacking(WM *wm);
static void recompute_workareas(WM *wm);
static void apply_normal_hints(WM *wm, Window window, int *width, int *height);
static void materialize_client_geometry(WM *wm, Client *client);
static void grab_client_buttons(WM *wm, Window window);
static void update_tab_bars(WM *wm);
static bool create_tab_bar(WM *wm, Monitor *monitor);

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

static bool query_monitor_rects(WM *wm, Rect rects[BOX2430_MAX_MONITORS],
                                unsigned int *count_return)
{
    int count = 0;
    XineramaScreenInfo *screens = NULL;
    if (XineramaIsActive(wm->display)) {
        screens = XineramaQueryScreens(wm->display, &count);
    }
    if (count <= 0) {
        count = 1;
    }
    if (count > BOX2430_MAX_MONITORS) {
        fprintf(stderr, "box2430: Xinerama reports more than %d monitors\n",
                BOX2430_MAX_MONITORS);
        XFree(screens);
        return false;
    }
    for (int i = 0; i < count; ++i) {
        rects[i] = screens
            ? (Rect){screens[i].x_org, screens[i].y_org,
                     screens[i].width, screens[i].height}
            : (Rect){0, 0, DisplayWidth(wm->display, wm->screen),
                     DisplayHeight(wm->display, wm->screen)};
    }
    XFree(screens);
    *count_return = (unsigned int)count;
    return true;
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
    Rect rects[BOX2430_MAX_MONITORS];
    unsigned int count;
    if (!query_monitor_rects(wm, rects, &count)) return false;
    wm->monitors = calloc(BOX2430_MAX_MONITORS, sizeof(*wm->monitors));
    if (!wm->monitors) {
        fprintf(stderr, "box2430: out of memory creating monitor state\n");
        return false;
    }
    wm->monitor_count = count;
    for (unsigned int i = 0; i < count; ++i) {
        if (!init_monitor_state(wm, &wm->monitors[i], i, rects[i])) {
            fprintf(stderr, "box2430: out of memory creating workspace state\n");
            for (unsigned int j = 0; j < i; ++j) free(wm->monitors[j].workspaces);
            free(wm->monitors);
            wm->monitors = NULL;
            wm->monitor_count = 0;
            return false;
        }
    }
    wm->selected_monitor = &wm->monitors[0];
    return true;
}

static Client *find_client(WM *wm, Window window)
{
    for (Client *client = wm->clients; client; client = client->next) {
        if (client->window == window) {
            return client;
        }
    }
    return NULL;
}

static SpecialWindow *find_special_window(WM *wm, Window window)
{
    for (SpecialWindow *special = wm->special_windows; special; special = special->next)
        if (special->window == window) return special;
    return NULL;
}

static Monitor *find_tab_monitor(WM *wm, Window window)
{
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        if (wm->monitors[i].tab_bar == window) return &wm->monitors[i];
    return NULL;
}

static bool tab_uses_bold(WM *wm, const Client *client)
{
    return client->urgent ? wm->config.tab_urgent_bold
        : client == wm->focused_client ? wm->config.tab_active_bold
        : wm->config.tab_inactive_bold;
}

static int utf8_length(const FcChar8 *text, FcChar32 *character)
{
    unsigned char first = text[0];
    if (first < 0x80) { *character = first; return 1; }
    int length = first >= 0xf0 && first <= 0xf4 ? 4
        : first >= 0xe0 ? 3 : first >= 0xc2 ? 2 : 1;
    FcChar32 value = length == 4 ? first & 0x07U
        : length == 3 ? first & 0x0fU : length == 2 ? first & 0x1fU : first;
    if (length == 1) { *character = value; return 1; }
    for (int i = 1; i < length; ++i) {
        if (!text[i] || (text[i] & 0xc0U) != 0x80U) {
            *character = first;
            return 1;
        }
        value = (value << 6) | (text[i] & 0x3fU);
    }
    FcChar32 minimum = length == 4 ? 0x10000U : length == 3 ? 0x800U : 0x80U;
    if (value < minimum || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) {
        *character = first;
        return 1;
    }
    *character = value;
    return length;
}

static XftFont *tab_font_for_character(WM *wm, bool bold, FcChar32 character)
{
    XftFont **fonts = bold ? wm->tab_fonts_bold : wm->tab_fonts;
    unsigned int count = bold ? wm->tab_font_bold_count : wm->tab_font_count;
    for (unsigned int i = 0; i < count; ++i)
        if (XftCharExists(wm->display, fonts[i], character)) return fonts[i];
    return fonts[0];
}

static unsigned int natural_tab_width(WM *wm, const Client *client)
{
    const char *title = client->title && client->title[0] ? client->title : "(untitled)";
    bool bold = tab_uses_bold(wm, client);
    const FcChar8 *cursor = (const FcChar8 *)title;
    unsigned int width = 0;
    while (*cursor) {
        FcChar32 character;
        int length = utf8_length(cursor, &character);
        XGlyphInfo extents = {0};
        XftTextExtentsUtf8(wm->display,
                           tab_font_for_character(wm, bold, character),
                           cursor, length, &extents);
        width += (unsigned int)extents.xOff;
        cursor += length;
    }
    return width + 2 * wm->config.tab_padding;
}

static unsigned int tab_count(const Workspace *workspace)
{
    unsigned int count = 0;
    for (const Client *client = workspace->tab_head; client; client = client->tab_next)
        ++count;
    return count;
}

static void tab_bounds(WM *wm, Monitor *monitor, Client *wanted,
                       int *x_return, unsigned int *width_return)
{
    Workspace *workspace = monitor->active_workspace;
    unsigned int count = tab_count(workspace);
    unsigned int total = 0;
    for (Client *client = workspace->tab_head; client; client = client->tab_next)
        total += natural_tab_width(wm, client);
    int x = 0;
    unsigned int index = 0;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        unsigned int width = total <= (unsigned int)monitor->workarea.width
            ? natural_tab_width(wm, client)
            : (unsigned int)monitor->workarea.width / count +
              (index < (unsigned int)monitor->workarea.width % count ? 1U : 0U);
        if (client == wanted) {
            *x_return = x;
            *width_return = width;
            return;
        }
        x += (int)width;
        ++index;
    }
    *x_return = x;
    *width_return = 0;
}

static Client *tab_at(WM *wm, Monitor *monitor, int x)
{
    Workspace *workspace = monitor->active_workspace;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        int left;
        unsigned int width;
        tab_bounds(wm, monitor, client, &left, &width);
        if (x >= left && x < left + (int)width) return client;
    }
    return NULL;
}

static void draw_tab_bar(WM *wm, Monitor *monitor)
{
    if (!monitor->tab_draw) return;
    XClearWindow(wm->display, monitor->tab_bar);
    Workspace *workspace = monitor->active_workspace;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        int x;
        unsigned int width;
        tab_bounds(wm, monitor, client, &x, &width);
        XftColor *fg;
        XftColor *bg;
        if (client->urgent) {
            fg = &wm->tab_urgent_fg; bg = &wm->tab_urgent_bg;
        } else if (client == wm->focused_client) {
            fg = &wm->tab_active_fg; bg = &wm->tab_active_bg;
        } else {
            fg = &wm->tab_inactive_fg; bg = &wm->tab_inactive_bg;
        }
        XSetForeground(wm->display, DefaultGC(wm->display, wm->screen), bg->pixel);
        XFillRectangle(wm->display, monitor->tab_bar,
                       DefaultGC(wm->display, wm->screen), x, 0, width,
                       wm->config.tab_height);
        XRectangle clip = {(short)x, 0, (unsigned short)width,
                           (unsigned short)wm->config.tab_height};
        XftDrawSetClipRectangles(monitor->tab_draw, 0, 0, &clip, 1);
        bool bold = tab_uses_bold(wm, client);
        XftFont *font = bold ? wm->tab_fonts_bold[0] : wm->tab_fonts[0];
        int baseline = ((int)wm->config.tab_height - font->ascent - font->descent) / 2 +
                       font->ascent;
        const char *title = client->title && client->title[0] ? client->title : "(untitled)";
        const FcChar8 *cursor = (const FcChar8 *)title;
        int text_x = x + (int)wm->config.tab_padding;
        while (*cursor) {
            FcChar32 character;
            int length = utf8_length(cursor, &character);
            font = tab_font_for_character(wm, bold, character);
            XftDrawStringUtf8(monitor->tab_draw, fg, font, text_x, baseline,
                              cursor, length);
            XGlyphInfo extents = {0};
            XftTextExtentsUtf8(wm->display, font, cursor, length, &extents);
            text_x += extents.xOff;
            cursor += length;
        }
    }
    XftDrawSetClip(monitor->tab_draw, NULL);
}

static void update_tab_bars(WM *wm)
{
    if (!wm->tab_resources_ready) return;
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Monitor *monitor = &wm->monitors[i];
        bool visible = wm->config.tabs_enabled &&
            monitor->active_workspace->mode == WORKSPACE_MONOCLE;
        XMoveResizeWindow(wm->display, monitor->tab_bar,
                          monitor->workarea.x, monitor->workarea.y,
                          (unsigned int)monitor->workarea.width, wm->config.tab_height);
        if (visible) {
            XMapWindow(wm->display, monitor->tab_bar);
            draw_tab_bar(wm, monitor);
        } else {
            XUnmapWindow(wm->display, monitor->tab_bar);
        }
    }
}

static void enforce_stacking(WM *wm)
{
    update_tab_bars(wm);
    for (SpecialWindow *special = wm->special_windows; special; special = special->next)
        if (special->type == WINDOW_TYPE_DESKTOP)
            XLowerWindow(wm->display, special->window);
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Workspace *workspace = wm->monitors[i].active_workspace;
        for (Client *client = workspace->stack_head; client; client = client->stack_next)
            if (!client->fullscreen) XRaiseWindow(wm->display, client->window);
    }
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        if (wm->config.tabs_enabled &&
            wm->monitors[i].active_workspace->mode == WORKSPACE_MONOCLE)
            XRaiseWindow(wm->display, wm->monitors[i].tab_bar);
    for (SpecialWindow *special = wm->special_windows; special; special = special->next)
        if (special->type != WINDOW_TYPE_DESKTOP)
            XRaiseWindow(wm->display, special->window);
    for (Client *client = wm->clients; client; client = client->next)
        if (client->fullscreen) XRaiseWindow(wm->display, client->window);
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

    client->mru_next = workspace->mru_head;
    if (workspace->mru_head) {
        workspace->mru_head->mru_prev = client;
    } else {
        workspace->mru_tail = client;
    }
    workspace->mru_head = client;

    client->stack_prev = workspace->stack_tail;
    if (workspace->stack_tail) {
        workspace->stack_tail->stack_next = client;
    } else {
        workspace->stack_head = client;
    }
    workspace->stack_tail = client;
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

    if (client->mru_prev) client->mru_prev->mru_next = client->mru_next;
    else workspace->mru_head = client->mru_next;
    if (client->mru_next) client->mru_next->mru_prev = client->mru_prev;
    else workspace->mru_tail = client->mru_prev;

    if (client->stack_prev) client->stack_prev->stack_next = client->stack_next;
    else workspace->stack_head = client->stack_next;
    if (client->stack_next) client->stack_next->stack_prev = client->stack_prev;
    else workspace->stack_tail = client->stack_prev;

    if (workspace->last_focused_client == client) {
        workspace->last_focused_client = NULL;
    }
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
    client->urgent = urgent;
    XWMHints *hints = XGetWMHints(wm->display, client->window);
    if (!hints) return;
    bool hinted = (hints->flags & XUrgencyHint) != 0;
    if (hinted != urgent) {
        if (urgent) hints->flags |= XUrgencyHint;
        else hints->flags &= ~XUrgencyHint;
        XSetWMHints(wm->display, client->window, hints);
    }
    XFree(hints);
}

static void read_focus_hints(WM *wm, Client *client)
{
    XWMHints *hints = XGetWMHints(wm->display, client->window);
    client->accepts_input = !hints || !(hints->flags & InputHint) || hints->input;
    bool urgent = hints && (hints->flags & XUrgencyHint);
    if (hints) XFree(hints);
    set_client_urgent(wm, client, urgent && wm->focused_client != client);
    client->takes_focus = client_supports_protocol(wm, client, wm->atoms.wm_take_focus);
    if (wm->focused_client != client)
        XSetWindowBorder(wm->display, client->window,
                         client->urgent ? wm->urgent_border : wm->unfocused_border);
}

static bool client_can_focus(const Client *client)
{
    return client && (client->accepts_input || client->takes_focus);
}

static Client *workspace_focus_fallback(Workspace *workspace, Client *removed,
                                        bool tab_neighbor)
{
    if (removed && tab_neighbor) {
        for (Client *client = removed->tab_next; client; client = client->tab_next)
            if (client_can_focus(client)) return client;
        for (Client *client = removed->tab_prev; client; client = client->tab_prev)
            if (client_can_focus(client)) return client;
    }
    for (Client *client = workspace->mru_head; client; client = client->mru_next)
        if (client != removed && client_can_focus(client)) return client;
    return NULL;
}

static void promote_mru(Client *client)
{
    Workspace *workspace = client->workspace;
    if (workspace->mru_head == client) return;
    if (client->mru_prev) client->mru_prev->mru_next = client->mru_next;
    if (client->mru_next) client->mru_next->mru_prev = client->mru_prev;
    if (workspace->mru_tail == client) workspace->mru_tail = client->mru_prev;
    client->mru_prev = NULL;
    client->mru_next = workspace->mru_head;
    if (workspace->mru_head) workspace->mru_head->mru_prev = client;
    else workspace->mru_tail = client;
    workspace->mru_head = client;
}

static void focus_client_internal(WM *wm, Client *client, Time time,
                                  bool update_mru)
{
    if (client && !client_can_focus(client)) return;
    if (wm->focused_client == client && client) {
        if (update_mru) promote_mru(client);
        return;
    }
    if (wm->focused_client) {
        XSetWindowBorder(wm->display, wm->focused_client->window,
                         wm->unfocused_border);
    }
    wm->focused_client = client;
    if (!client) {
        XSetInputFocus(wm->display, wm->root, RevertToPointerRoot, time);
        x11_update_active_window(wm);
        update_tab_bars(wm);
        return;
    }

    wm->selected_monitor = client->workspace->monitor;
    x11_update_workarea(wm);
    client->workspace->last_focused_client = client;
    set_client_urgent(wm, client, false);
    if (update_mru) promote_mru(client);
    XSetWindowBorder(wm->display, client->window, wm->focused_border);
    if (client->accepts_input) {
        XSetInputFocus(wm->display, client->window, RevertToPointerRoot, time);
    }
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
    x11_update_active_window(wm);
    update_tab_bars(wm);
    if (wm->config.raise_on_focus) client_raise(wm, client);
}

static void focus_client(WM *wm, Client *client, Time time)
{
    focus_client_internal(wm, client, time, true);
}

static void apply_client_geometry(WM *wm, Client *client, Rect geometry)
{
    client->geometry = geometry;
    XMoveResizeWindow(wm->display, client->window, geometry.x, geometry.y,
                      (unsigned int)geometry.width, (unsigned int)geometry.height);
}

static Rect fit_workarea(WM *wm, const Client *client, Rect area)
{
    (void)wm;
    int border = client->fullscreen ? 0 : (int)client->border_width;
    int width = area.width - 2 * border;
    int height = area.height - 2 * border;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    return (Rect){area.x, area.y, width, height};
}

static void update_fullscreen_property(WM *wm, Client *client)
{
    if (client->fullscreen || client->client_fullscreen) {
        Atom state = wm->atoms.net_wm_state_fullscreen;
        XChangeProperty(wm->display, client->window, wm->atoms.net_wm_state,
                        XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)&state, 1);
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

void client_focus_relative(WM *wm, bool tab_order, bool forward)
{
    Workspace *workspace = wm->selected_monitor->active_workspace;
    Client *target = NULL;
    Client *cursor = wm->focused_client && wm->focused_client->workspace == workspace
        ? wm->focused_client : NULL;
    unsigned int count = tab_count(workspace);
    for (unsigned int i = 0; i < count; ++i) {
        if (tab_order) {
            cursor = cursor ? (forward ? cursor->tab_next : cursor->tab_prev) : NULL;
            if (!cursor) cursor = forward ? workspace->tab_head : workspace->tab_tail;
        } else {
            cursor = cursor ? (forward ? cursor->mru_next : cursor->mru_prev) : NULL;
            if (!cursor) cursor = forward ? workspace->mru_head : workspace->mru_tail;
        }
        if (client_can_focus(cursor)) { target = cursor; break; }
    }
    focus_client(wm, target, CurrentTime);
    if (workspace->mode == WORKSPACE_MONOCLE && target) client_raise(wm, target);
}

void client_focus_tab_target(WM *wm, Client *client, Time time)
{
    Workspace *workspace = wm->selected_monitor->active_workspace;
    if (!client || client->workspace != workspace) return;
    focus_client(wm, client, time);
    if (workspace->mode == WORKSPACE_MONOCLE) client_raise(wm, client);
}

void client_commit_mru_cycle(WM *wm)
{
    if (!wm->mru_cycle.active) return;
    Client *client = wm->focused_client;
    if (client && client->workspace == wm->mru_cycle.workspace)
        promote_mru(client);
    free(wm->mru_cycle.windows);
    memset(&wm->mru_cycle, 0, sizeof(wm->mru_cycle));
}

void client_focus_mru_cycle(WM *wm, bool forward, unsigned int modifiers)
{
    Workspace *workspace = wm->selected_monitor->active_workspace;
    if (!modifiers) {
        client_focus_relative(wm, false, forward);
        return;
    }
    if (wm->mru_cycle.active && wm->mru_cycle.workspace != workspace)
        client_commit_mru_cycle(wm);
    if (!wm->mru_cycle.active) {
        size_t count = 0;
        for (Client *client = workspace->mru_head; client; client = client->mru_next)
            ++count;
        if (!count) return;
        Window *windows = calloc(count, sizeof(*windows));
        if (!windows) return;
        size_t focused = count - 1;
        size_t index = 0;
        for (Client *client = workspace->mru_head; client; client = client->mru_next) {
            windows[index] = client->window;
            if (client == wm->focused_client) focused = index;
            ++index;
        }
        wm->mru_cycle.windows = windows;
        wm->mru_cycle.count = count;
        wm->mru_cycle.cursor = focused;
        wm->mru_cycle.workspace = workspace;
        wm->mru_cycle.modifiers = modifiers;
        wm->mru_cycle.active = true;
    }

    for (size_t attempts = 0; attempts < wm->mru_cycle.count; ++attempts) {
        if (forward)
            wm->mru_cycle.cursor = (wm->mru_cycle.cursor + 1) % wm->mru_cycle.count;
        else
            wm->mru_cycle.cursor = (wm->mru_cycle.cursor + wm->mru_cycle.count - 1) %
                                   wm->mru_cycle.count;
        Client *target = find_client(wm, wm->mru_cycle.windows[wm->mru_cycle.cursor]);
        if (target && target->workspace == workspace && client_can_focus(target)) {
            focus_client_internal(wm, target, CurrentTime, false);
            if (workspace->mode == WORKSPACE_MONOCLE) client_raise(wm, target);
            return;
        }
    }
}

void workspace_set_mode(WM *wm, Workspace *workspace, WorkspaceMode mode)
{
    if (!workspace || workspace->mode == mode) return;
    workspace->mode = mode;
    if (workspace != workspace->monitor->active_workspace) return;
    for (Client *client = workspace->clients; client; client = client->workspace_next)
        materialize_client_geometry(wm, client);
    if (mode == WORKSPACE_MONOCLE) {
        if (wm->focused_client && wm->focused_client->workspace == workspace)
            client_raise(wm, wm->focused_client);
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
    if (client->fullscreen) {
        Rect area = monitor->geometry;
        XMoveResizeWindow(wm->display, client->window, area.x, area.y,
                          (unsigned int)area.width, (unsigned int)area.height);
    } else if (client->workspace->mode == WORKSPACE_MONOCLE) {
        Rect area = fit_workarea(wm, client, monitor->workarea);
        XMoveResizeWindow(wm->display, client->window, area.x, area.y,
                          (unsigned int)area.width, (unsigned int)area.height);
    } else if (client->maximized) {
        apply_client_geometry(wm, client,
                              fit_workarea(wm, client, monitor->workarea));
    } else if (client->snap_state != SNAP_NONE) {
        apply_client_geometry(wm, client,
                              snap_geometry(wm, client, client->snap_state));
    } else {
        apply_client_geometry(wm, client, client->geometry);
    }
}

static bool ranges_overlap(int start, int end, unsigned long other_start,
                           unsigned long other_end)
{
    return end > (int)other_start && start <= (int)other_end;
}

static void recompute_workareas(WM *wm)
{
    int root_width = DisplayWidth(wm->display, wm->screen);
    int root_height = DisplayHeight(wm->display, wm->screen);
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Monitor *monitor = &wm->monitors[i];
        Rect area = monitor->geometry;
        int right = area.x + area.width;
        int bottom = area.y + area.height;
        for (SpecialWindow *dock = wm->special_windows; dock; dock = dock->next) {
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
        monitor->workarea = area;

        for (unsigned int j = 0; j < wm->config.workspace_count; ++j)
            for (Client *client = monitor->workspaces[j].clients; client;
                 client = client->workspace_next)
                materialize_client_geometry(wm, client);
    }
    x11_update_workarea(wm);
    update_tab_bars(wm);
}

void client_snap(WM *wm, Client *client, SnapState state)
{
    if (!client || client->workspace->mode == WORKSPACE_MONOCLE || client->fullscreen)
        return;
    if (state == SNAP_NONE) {
        if (client->snap_state != SNAP_NONE || client->maximized) {
            client->snap_state = SNAP_NONE;
            client->maximized = false;
            apply_client_geometry(wm, client, client->normal_geometry);
        }
        return;
    }
    if (client->snap_state == SNAP_NONE && !client->maximized)
        client->normal_geometry = client->geometry;
    client->maximized = false;
    client->snap_state = state;
    apply_client_geometry(wm, client, snap_geometry(wm, client, state));
}

static Monitor *monitor_at_point(WM *wm, int x, int y)
{
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Rect area = wm->monitors[i].geometry;
        if (x >= area.x && x < area.x + area.width &&
            y >= area.y && y < area.y + area.height) return &wm->monitors[i];
    }
    return wm->selected_monitor;
}

static void hide_snap_preview(WM *wm)
{
    for (size_t i = 0; i < 4; ++i)
        if (wm->drag.preview_windows[i])
            XUnmapWindow(wm->display, wm->drag.preview_windows[i]);
}

static bool ensure_snap_preview(WM *wm)
{
    if (wm->drag.preview_windows[0]) return true;
    XSetWindowAttributes attributes = {
        .override_redirect = True,
        .background_pixel = wm->snap_preview_color,
    };
    for (size_t i = 0; i < 4; ++i) {
        wm->drag.preview_windows[i] = XCreateWindow(
            wm->display, wm->root, 0, 0, 1, 1, 0, CopyFromParent,
            InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel,
            &attributes);
        if (!wm->drag.preview_windows[i]) return false;
    }
    return true;
}

static void show_snap_preview(WM *wm, Client *client, Monitor *monitor,
                              SnapState state)
{
    if (!wm->config.snap_preview || !ensure_snap_preview(wm)) return;
    Rect inner = state == SNAP_NONE
        ? monitor->workarea : snap_geometry_on(wm, client, monitor, state);
    int border = (int)client->border_width;
    int width = inner.width + 2 * border;
    int height = inner.height + 2 * border;
    int line = (int)wm->config.snap_preview_width;
    if (line > width) line = width;
    if (line > height) line = height;
    Rect pieces[4] = {
        {inner.x, inner.y, width, line},
        {inner.x, inner.y + height - line, width, line},
        {inner.x, inner.y, line, height},
        {inner.x + width - line, inner.y, line, height},
    };
    for (size_t i = 0; i < 4; ++i) {
        XMoveResizeWindow(wm->display, wm->drag.preview_windows[i],
                          pieces[i].x, pieces[i].y,
                          (unsigned int)pieces[i].width,
                          (unsigned int)pieces[i].height);
        XMapRaised(wm->display, wm->drag.preview_windows[i]);
    }
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
        client->snap_state = SNAP_NONE;
        client->maximized = false;
        apply_client_geometry(wm, client, client->normal_geometry);
    }
    focus_client(wm, client, CurrentTime);
    wm->drag.client = client;
    wm->drag.active = true;
    wm->drag.resize = resize;
    wm->drag.preview_snap = SNAP_NONE;
    wm->drag.preview_monitor = NULL;
    if (resize) {
        root_x = client->geometry.x + client->geometry.width +
                 2 * (int)client->border_width - 1;
        root_y = client->geometry.y + client->geometry.height +
                 2 * (int)client->border_width - 1;
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
        apply_normal_hints(wm, client->window, &geometry.width, &geometry.height);
    } else {
        geometry.x += dx;
        geometry.y += dy;
    }
    client->snap_state = SNAP_NONE;
    client->maximized = false;
    apply_client_geometry(wm, client, geometry);
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
        if (target != wm->drag.preview_snap || desired_monitor != wm->drag.preview_monitor) {
            hide_snap_preview(wm);
            wm->drag.preview_snap = target;
            wm->drag.preview_monitor = desired_monitor;
            if (maximize) {
                show_snap_preview(wm, client, monitor, SNAP_NONE);
            } else if (target != SNAP_NONE) {
                show_snap_preview(wm, client, monitor, target);
            }
        }
    }
}

static void finish_drag(WM *wm)
{
    if (!wm->drag.active || !wm->drag.client) return;
    Client *client = wm->drag.client;
    Monitor *target = NULL;
    bool maximize = false;
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
    hide_snap_preview(wm);
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
    focus_client(wm, client, CurrentTime);
    wm->drag.active = false;
    wm->drag.client = NULL;
    wm->drag.preview_monitor = NULL;
    wm->drag.preview_snap = SNAP_NONE;
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
        apply_client_geometry(wm, client,
                              fit_workarea(wm, client, client->workspace->monitor->workarea));
    } else {
        client->maximized = false;
        client->snap_state = SNAP_NONE;
        apply_client_geometry(wm, client, client->normal_geometry);
    }
}

static void apply_real_fullscreen(WM *wm, Client *client, bool fullscreen)
{
    if (!client || client->fullscreen == fullscreen) return;
    client->fullscreen = fullscreen;
    XSetWindowBorderWidth(wm->display, client->window,
                          fullscreen ? 0 : client->border_width);
    materialize_client_geometry(wm, client);
    enforce_stacking(wm);
    update_fullscreen_property(wm, client);
    x11_update_client_lists(wm);
}

void client_set_fullscreen(WM *wm, Client *client, bool fullscreen)
{
    if (!client) return;
    client->user_fullscreen = fullscreen;
    bool real = fullscreen ||
        (client->client_fullscreen &&
         client->fullscreen_policy == CLIENT_FULLSCREEN_ALLOW);
    apply_real_fullscreen(wm, client, real);
    update_fullscreen_property(wm, client);
}

static void client_set_requested_fullscreen(WM *wm, Client *client, bool requested)
{
    if (client->fullscreen_policy == CLIENT_FULLSCREEN_DENY) {
        client->client_fullscreen = false;
    } else {
        client->client_fullscreen = requested;
    }
    bool real = client->user_fullscreen ||
        (client->client_fullscreen &&
         client->fullscreen_policy == CLIENT_FULLSCREEN_ALLOW);
    apply_real_fullscreen(wm, client, real);
    update_fullscreen_property(wm, client);
}

void workspace_activate(WM *wm, Monitor *monitor, Workspace *workspace)
{
    if (!monitor || !workspace || workspace->monitor != monitor ||
        monitor->active_workspace == workspace) {
        return;
    }

    Workspace *old = monitor->active_workspace;
    if (wm->focused_client && wm->focused_client->workspace == old) {
        focus_client(wm, NULL, CurrentTime);
    }
    for (Client *client = old->clients; client; client = client->workspace_next) {
        ++client->ignored_unmaps;
        XUnmapWindow(wm->display, client->window);
    }

    monitor->active_workspace = workspace;
    for (Client *client = workspace->stack_head; client; client = client->stack_next) {
        materialize_client_geometry(wm, client);
        XMapWindow(wm->display, client->window);
        XRaiseWindow(wm->display, client->window);
    }
    Client *restore = workspace->last_focused_client;
    if (restore && !client_can_focus(restore))
        restore = workspace_focus_fallback(workspace, NULL, false);
    focus_client(wm, restore, CurrentTime);
    enforce_stacking(wm);
}

void monitor_select(WM *wm, Monitor *monitor)
{
    if (!monitor) return;
    wm->selected_monitor = monitor;
    x11_update_workarea(wm);
    int x = monitor->geometry.x + monitor->geometry.width / 2;
    int y = monitor->geometry.y + monitor->geometry.height / 2;
    XWarpPointer(wm->display, None, wm->root, 0, 0, 0, 0, x, y);
    Workspace *workspace = monitor->active_workspace;
    Client *restore = workspace->last_focused_client;
    if (restore && !client_can_focus(restore))
        restore = workspace_focus_fallback(workspace, NULL, false);
    if (restore && restore->workspace == workspace) focus_client(wm, restore, CurrentTime);
    else focus_client(wm, NULL, CurrentTime);
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
    bool was_focused = wm->focused_client == client;
    bool was_last_focused = old->last_focused_client == client;
    bool tab_neighbor = old->mode == WORKSPACE_MONOCLE &&
                        !wm->config.monocle_fallback_mru;
    Client *fallback = workspace_focus_fallback(old, client, tab_neighbor);
    if (was_focused) focus_client(wm, fallback, CurrentTime);
    else if (was_last_focused) old->last_focused_client = fallback;

    if (old == old->monitor->active_workspace) {
        ++client->ignored_unmaps;
        XUnmapWindow(wm->display, client->window);
    }
    unlink_workspace_orders(old, client);
    if (translate_monitor_geometry && old_monitor != new_monitor) {
        int dx = new_monitor->geometry.x - old_monitor->geometry.x;
        int dy = new_monitor->geometry.y - old_monitor->geometry.y;
        client->geometry.x += dx;
        client->geometry.y += dy;
        client->normal_geometry.x += dx;
        client->normal_geometry.y += dy;
        client->geometry = clamp_to_workarea(client->geometry, new_monitor->workarea,
                                              client->border_width);
        client->normal_geometry = clamp_to_workarea(
            client->normal_geometry, new_monitor->workarea, client->border_width);
    }
    client->workspace = workspace;
    client->workspace_next = NULL;
    client->tab_prev = client->tab_next = NULL;
    client->mru_prev = client->mru_next = NULL;
    client->stack_prev = client->stack_next = NULL;
    append_workspace_orders(workspace, client);

    materialize_client_geometry(wm, client);

    if (follow) {
        wm->selected_monitor = workspace->monitor;
        workspace_activate(wm, workspace->monitor, workspace);
        XMapWindow(wm->display, client->window);
        focus_client(wm, client, CurrentTime);
        client_raise(wm, client);
    } else if (workspace == workspace->monitor->active_workspace) {
        XMapWindow(wm->display, client->window);
    }
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
    XUngrabKey(wm->display, AnyKey, AnyModifier, wm->root);
    for (unsigned int binding_index = 0;
         binding_index < wm->config.key_binding_count; ++binding_index) {
        KeyBinding *binding = &wm->config.key_bindings[binding_index];
        KeyCode code = XKeysymToKeycode(wm->display, binding->symbol);
        for (size_t i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
            XGrabKey(wm->display, (int)code,
                     binding->modifiers | modifiers[i], wm->root,
                     True, GrabModeAsync, GrabModeAsync);
        }
    }
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
                .modifiers = binding->modifiers,
            };
            bool is_mru = binding->argc == 2 &&
                strcmp(binding->argv[0], "focus") == 0 &&
                (strcmp(binding->argv[1], "next-mru") == 0 ||
                 strcmp(binding->argv[1], "prev-mru") == 0);
            if (!is_mru) client_commit_mru_cycle(wm);
            command_run(wm, &context, binding->argc, argv);
            return;
        }
    }
}

static unsigned int modifier_mask_for_keycode(WM *wm, KeyCode code)
{
    XModifierKeymap *map = XGetModifierMapping(wm->display);
    if (!map) return 0;
    unsigned int result = 0;
    for (int modifier = 0; modifier < 8; ++modifier) {
        for (int key = 0; key < map->max_keypermod; ++key) {
            if (map->modifiermap[modifier * map->max_keypermod + key] == code)
                result |= 1U << modifier;
        }
    }
    XFreeModifiermap(map);
    return result;
}

static void handle_key_release(WM *wm, XKeyEvent *event)
{
    if (!wm->mru_cycle.active) return;
    unsigned int released = modifier_mask_for_keycode(wm, event->keycode);
    if (released & wm->mru_cycle.modifiers) client_commit_mru_cycle(wm);
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

static InitialPolicy initial_policy(WM *wm, Client *client)
{
    InitialPolicy policy = {
        .monitor = wm->selected_monitor,
        .workspace_index = wm->selected_monitor->active_workspace->index,
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
        if (rule->has_monitor && rule->monitor <= wm->monitor_count) {
            policy.monitor = &wm->monitors[rule->monitor - 1];
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

static void apply_normal_hints(WM *wm, Window window, int *width, int *height)
{
    XSizeHints hints;
    long supplied;
    if (!XGetWMNormalHints(wm->display, window, &hints, &supplied)) return;
    int base_width = hints.flags & PBaseSize ? hints.base_width
        : hints.flags & PMinSize ? hints.min_width : 0;
    int base_height = hints.flags & PBaseSize ? hints.base_height
        : hints.flags & PMinSize ? hints.min_height : 0;
    int min_width = hints.flags & PMinSize ? hints.min_width : base_width;
    int min_height = hints.flags & PMinSize ? hints.min_height : base_height;
    bool base_is_min = base_width == min_width && base_height == min_height;

    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
    if (!base_is_min) {
        *width -= base_width;
        *height -= base_height;
    }
    if ((hints.flags & PAspect) && *width > 0 && *height > 0 &&
        hints.min_aspect.x > 0 && hints.min_aspect.y > 0 &&
        hints.max_aspect.x > 0 && hints.max_aspect.y > 0) {
        double min_aspect = (double)hints.min_aspect.y / hints.min_aspect.x;
        double max_aspect = (double)hints.max_aspect.x / hints.max_aspect.y;
        if (max_aspect < (double)*width / *height)
            *width = (int)(*height * max_aspect + 0.5);
        else if (min_aspect < (double)*height / *width)
            *height = (int)(*width * min_aspect + 0.5);
    }
    if (base_is_min) {
        *width -= base_width;
        *height -= base_height;
    }
    if ((hints.flags & PResizeInc) && hints.width_inc > 0)
        *width -= *width % hints.width_inc;
    if ((hints.flags & PResizeInc) && hints.height_inc > 0)
        *height -= *height % hints.height_inc;
    *width += base_width;
    *height += base_height;
    if (*width < min_width) *width = min_width;
    if (*height < min_height) *height = min_height;
    if ((hints.flags & PMaxSize) && hints.max_width > 0 &&
        *width > hints.max_width) *width = hints.max_width;
    if ((hints.flags & PMaxSize) && hints.max_height > 0 &&
        *height > hints.max_height) *height = hints.max_height;
}

static Rect initial_geometry(WM *wm, const Monitor *monitor,
                             const XWindowAttributes *attrs, Window window,
                             PlacementPolicy placement, unsigned int border_width)
{
    int width = attrs->width;
    int height = attrs->height;
    apply_normal_hints(wm, window, &width, &height);
    int border = (int)border_width;
    if (width > monitor->workarea.width - 2 * border) {
        width = monitor->workarea.width - 2 * border;
    }
    if (height > monitor->workarea.height - 2 * border) {
        height = monitor->workarea.height - 2 * border;
    }
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (placement == PLACEMENT_CLIENT) {
        return (Rect){attrs->x, attrs->y, width, height};
    }
    return (Rect){
        monitor->workarea.x + (monitor->workarea.width - width) / 2,
        monitor->workarea.y + (monitor->workarea.height - height) / 2,
        width,
        height,
    };
}

static void grab_client_buttons(WM *wm, Window window)
{
    unsigned int event_mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
    XUngrabButton(wm->display, AnyButton, AnyModifier, window);
    if (wm->config.focus_mode == FOCUS_CLICK) {
        XGrabButton(wm->display, AnyButton, AnyModifier, window, False,
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
                        mouse->modifiers | ignored[i], window, False,
                        event_mask, GrabModeSync, GrabModeAsync, None, None);
    }
}

static void manage_special_window(WM *wm, Window window, WindowType type,
                                  bool map_window)
{
    if (find_special_window(wm, window)) {
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
    special->next = wm->special_windows;
    wm->special_windows = special;
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
    SpecialWindow **link = &wm->special_windows;
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
    if (find_client(wm, window)) {
        if (map_window) XMapWindow(wm->display, window);
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
    XGetTransientForHint(wm->display, window, &client->transient_for);
    if (!client->title || !client->instance || !client->class_name) {
        fprintf(stderr, "box2430: out of memory reading window metadata\n");
        free(client->title);
        free(client->instance);
        free(client->class_name);
        free(client);
        wm->running = false;
        return;
    }
    InitialPolicy policy = initial_policy(wm, client);
    client->workspace = &policy.monitor->workspaces[policy.workspace_index];
    client->border_width = policy.border ? wm->config.border_width : 0;
    client->fullscreen_policy = policy.fullscreen_policy;
    client->geometry = initial_geometry(wm, policy.monitor, &attrs, window,
                                        policy.placement, client->border_width);
    client->normal_geometry = client->geometry;
    client->next = wm->clients;
    wm->clients = client;
    append_workspace_orders(client->workspace, client);

    XSelectInput(wm->display, window,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask);
    grab_client_buttons(wm, window);
    XSetWindowBorderWidth(wm->display, window, client->border_width);
    XSetWindowBorder(wm->display, window, wm->unfocused_border);
    XMoveResizeWindow(wm->display, window, client->geometry.x, client->geometry.y,
                      (unsigned int)client->geometry.width,
                      (unsigned int)client->geometry.height);
    x11_set_wm_state(wm, window, NormalState);
    read_focus_hints(wm, client);
    bool visible = client->workspace == client->workspace->monitor->active_workspace;
    if (map_window && visible) {
        XMapWindow(wm->display, window);
    } else if (!map_window && !visible && attrs.map_state == IsViewable) {
        ++client->ignored_unmaps;
        XUnmapWindow(wm->display, window);
    }
    if (policy.raise_on_map) {
        client_raise(wm, client);
    }
    if (policy.focus_on_map && visible) focus_client(wm, client, CurrentTime);
    if (x11_window_requests_fullscreen(wm, window))
        client_set_requested_fullscreen(wm, client, true);
    x11_update_client_lists(wm);
}

static void unmanage_client(WM *wm, Client *client, bool withdrawn)
{
    Workspace *workspace = client->workspace;
    bool was_last_focused = workspace->last_focused_client == client;
    bool tab_neighbor = workspace->mode == WORKSPACE_MONOCLE &&
                        !wm->config.monocle_fallback_mru;
    Client *fallback = workspace_focus_fallback(workspace, client, tab_neighbor);
    if (wm->focused_client == client) {
        wm->focused_client = NULL;
        focus_client(wm, fallback, CurrentTime);
    } else if (was_last_focused) {
        workspace->last_focused_client = fallback;
    }
    unlink_workspace_orders(workspace, client);

    Client **link = &wm->clients;
    while (*link && *link != client) {
        link = &(*link)->next;
    }
    if (*link) {
        *link = client->next;
    }
    if (withdrawn) {
        x11_set_wm_state(wm, client->window, WithdrawnState);
        XSetWindowBorderWidth(wm->display, client->window, 0);
    }
    free(client->title);
    free(client->class_name);
    free(client->instance);
    free(client);
    enforce_stacking(wm);
    x11_update_client_lists(wm);
}

static bool rect_equal(Rect left, Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

static void reconcile_monitors(WM *wm)
{
    Rect rects[BOX2430_MAX_MONITORS];
    unsigned int new_count;
    if (!query_monitor_rects(wm, rects, &new_count)) return;
    bool changed = new_count != wm->monitor_count;
    unsigned int common = new_count < wm->monitor_count ? new_count : wm->monitor_count;
    for (unsigned int i = 0; i < common; ++i)
        changed |= !rect_equal(wm->monitors[i].geometry, rects[i]);
    if (!changed) return;

    unsigned int old_count = wm->monitor_count;
    if (wm->selected_monitor && wm->selected_monitor->index >= new_count)
        wm->selected_monitor = &wm->monitors[0];
    if (wm->focused_client &&
        wm->focused_client->workspace->monitor->index >= new_count) {
        wm->focused_client = NULL;
        XSetInputFocus(wm->display, wm->root, RevertToPointerRoot, CurrentTime);
        x11_update_active_window(wm);
    }
    client_commit_mru_cycle(wm);
    wm->monitor_count = new_count;
    for (unsigned int i = 0; i < common; ++i)
        wm->monitors[i].geometry = rects[i];
    for (unsigned int i = old_count; i < new_count; ++i) {
        if (!init_monitor_state(wm, &wm->monitors[i], i, rects[i]) ||
            (wm->tab_resources_ready && !create_tab_bar(wm, &wm->monitors[i]))) {
            fprintf(stderr, "box2430: cannot create state for added monitor\n");
            wm->running = false;
            return;
        }
    }
    for (unsigned int i = new_count; i < old_count; ++i) {
        Monitor *removed = &wm->monitors[i];
        for (unsigned int workspace_index = 0;
             workspace_index < wm->config.workspace_count; ++workspace_index) {
            Workspace *source = &removed->workspaces[workspace_index];
            Workspace *destination = &wm->monitors[0].workspaces[workspace_index];
            while (source->clients)
                client_move_to_workspace(wm, source->clients, destination, false, true);
        }
        if (removed->tab_draw) XftDrawDestroy(removed->tab_draw);
        if (removed->tab_bar) XDestroyWindow(wm->display, removed->tab_bar);
        free(removed->workspaces);
        memset(removed, 0, sizeof(*removed));
    }
    recompute_workareas(wm);
    if (!wm->focused_client) {
        Workspace *workspace = wm->selected_monitor->active_workspace;
        Client *restore = workspace->last_focused_client;
        if (restore && !client_can_focus(restore))
            restore = workspace_focus_fallback(workspace, NULL, false);
        focus_client(wm, restore, CurrentTime);
    }
    enforce_stacking(wm);
    x11_update_client_lists(wm);
}

static void handle_configure_request(WM *wm, XConfigureRequestEvent *event)
{
    Client *client = find_client(wm, event->window);
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

    bool presentation = client->workspace->mode == WORKSPACE_MONOCLE ||
                        client->fullscreen || client->maximized ||
                        client->snap_state != SNAP_NONE;
    if (!presentation) {
        Rect geometry = client->geometry;
        if (event->value_mask & CWX) geometry.x = event->x;
        if (event->value_mask & CWY) geometry.y = event->y;
        if (event->value_mask & CWWidth) geometry.width = event->width;
        if (event->value_mask & CWHeight) geometry.height = event->height;
        apply_normal_hints(wm, client->window, &geometry.width, &geometry.height);
        if (geometry.width < 1) geometry.width = 1;
        if (geometry.height < 1) geometry.height = 1;
        client->normal_geometry = geometry;
        apply_client_geometry(wm, client, geometry);
    }

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
    Client *client;
    SpecialWindow *special;
    switch (event->type) {
    case MapRequest:
        manage_window(wm, event->xmaprequest.window, true);
        break;
    case ConfigureRequest:
        handle_configure_request(wm, &event->xconfigurerequest);
        break;
    case ConfigureNotify:
        if (event->xconfigure.window == wm->root) reconcile_monitors(wm);
        break;
    case DestroyNotify:
        client = find_client(wm, event->xdestroywindow.window);
        if (client) unmanage_client(wm, client, false);
        else {
            special = find_special_window(wm, event->xdestroywindow.window);
            if (special) unmanage_special_window(wm, special, false);
        }
        break;
    case UnmapNotify:
        client = find_client(wm, event->xunmap.window);
        if (client && client->ignored_unmaps) {
            --client->ignored_unmaps;
        } else if (client && !event->xunmap.send_event) {
            unmanage_client(wm, client, true);
        } else if (!client && !event->xunmap.send_event) {
            special = find_special_window(wm, event->xunmap.window);
            if (special) unmanage_special_window(wm, special, true);
        }
        break;
    case KeyPress:
        handle_key_press(wm, &event->xkey);
        break;
    case KeyRelease:
        handle_key_release(wm, &event->xkey);
        break;
    case MappingNotify:
        XRefreshKeyboardMapping(&event->xmapping);
        if (event->xmapping.request != MappingPointer) {
            grab_default_keys(wm);
            for (Client *mapped = wm->clients; mapped; mapped = mapped->next)
                grab_client_buttons(wm, mapped->window);
        }
        break;
    case ButtonPress:
        {
        Monitor *tab_monitor = find_tab_monitor(wm, event->xbutton.window);
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
                    .client = tab_at(wm, tab_monitor, event->xbutton.x),
                    .root_x = event->xbutton.x_root,
                    .root_y = event->xbutton.y_root,
                    .time = event->xbutton.time,
                };
                command_run(wm, &context, matched->argc, argv);
            }
            break;
        }
        client = find_client(wm, event->xbutton.window);
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
            focus_client(wm, client, event->xbutton.time);
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
        client = find_client(wm, event->xcrossing.window);
        if (client && wm->config.focus_mode == FOCUS_SLOPPY &&
            event->xcrossing.mode == NotifyNormal)
            focus_client(wm, client, event->xcrossing.time);
        break;
    case PropertyNotify:
        client = find_client(wm, event->xproperty.window);
        if (client && event->xproperty.atom == XA_WM_HINTS) {
            read_focus_hints(wm, client);
            update_tab_bars(wm);
        } else if (client && (event->xproperty.atom == wm->atoms.net_wm_name ||
                              event->xproperty.atom == XA_WM_NAME)) {
            char *title = x11_read_window_title(wm, client->window);
            if (title) {
                free(client->title);
                client->title = title;
                update_tab_bars(wm);
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
            } else {
                free(instance);
                free(class_name);
            }
        } else if (client && event->xproperty.atom == XA_WM_TRANSIENT_FOR) {
            XGetTransientForHint(wm->display, client->window, &client->transient_for);
        } else if (!client && (event->xproperty.atom == wm->atoms.net_wm_strut ||
                               event->xproperty.atom == wm->atoms.net_wm_strut_partial)) {
            special = find_special_window(wm, event->xproperty.window);
            if (special && special->type == WINDOW_TYPE_DOCK) {
                special->has_strut = x11_read_strut(wm, special->window,
                                                    special->strut);
                recompute_workareas(wm);
            }
        }
        break;
    case ClientMessage:
        client = find_client(wm, event->xclient.window);
        if (event->xclient.message_type == wm->atoms.net_active_window && client &&
            client->workspace == client->workspace->monitor->active_workspace) {
            focus_client(wm, client, CurrentTime);
        } else if (event->xclient.message_type == wm->atoms.net_close_window && client) {
            client_close(wm, client);
        } else if (event->xclient.message_type == wm->atoms.net_wm_state && client &&
                   (event->xclient.data.l[1] == (long)wm->atoms.net_wm_state_fullscreen ||
                    event->xclient.data.l[2] == (long)wm->atoms.net_wm_state_fullscreen)) {
            long action = event->xclient.data.l[0];
            bool requested = action == 1 || (action == 2 && !client->client_fullscreen);
            if (action >= 0 && action <= 2) client_set_requested_fullscreen(wm, client, requested);
        }
        break;
    case Expose:
        {
        Monitor *tab_monitor = find_tab_monitor(wm, event->xexpose.window);
        if (tab_monitor && event->xexpose.count == 0) draw_tab_bar(wm, tab_monitor);
        break;
        }
    default:
        break;
    }
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
    for (unsigned int i = 0; i < count; ++i) {
        XWindowAttributes attrs;
        if (XGetWindowAttributes(wm->display, children[i], &attrs) &&
            attrs.map_state == IsViewable) {
            manage_window(wm, children[i], false);
        }
    }
    XFree(children);
}

static bool create_tab_bar(WM *wm, Monitor *monitor)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    XSetWindowAttributes attributes = {
        .override_redirect = True,
        .background_pixel = wm->tab_inactive_bg.pixel,
        .event_mask = ExposureMask | ButtonPressMask,
    };
    monitor->tab_bar = XCreateWindow(
        wm->display, wm->root, monitor->workarea.x, monitor->workarea.y,
        (unsigned int)monitor->workarea.width, wm->config.tab_height, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput, visual,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    char name[64];
    snprintf(name, sizeof(name), "box2430-tabbar-%u", monitor->index);
    XStoreName(wm->display, monitor->tab_bar, name);
    monitor->tab_draw = XftDrawCreate(
        wm->display, monitor->tab_bar, visual,
        DefaultColormap(wm->display, wm->screen));
    if (!monitor->tab_draw) {
        XDestroyWindow(wm->display, monitor->tab_bar);
        monitor->tab_bar = None;
        return false;
    }
    return true;
}

static unsigned int load_tab_fonts(WM *wm, const char *name, bool bold,
                                   XftFont **fonts)
{
    unsigned int count = 0;
    fonts[count] = XftFontOpenName(wm->display, wm->screen, name);
    if (!fonts[count]) return 0;
    ++count;
    const char *regular[] = {
        "sans:lang=zh-cn", "sans:lang=ja", "sans:lang=ko", "sans",
    };
    const char *heavy[] = {
        "sans:style=Bold:lang=zh-cn", "sans:style=Bold:lang=ja",
        "sans:style=Bold:lang=ko", "sans:style=Bold",
    };
    const char *const *fallbacks = bold ? heavy : regular;
    for (size_t i = 0; i < sizeof(regular) / sizeof(regular[0]) &&
                       count < BOX2430_MAX_TAB_FONTS; ++i) {
        XftFont *font = XftFontOpenName(wm->display, wm->screen, fallbacks[i]);
        if (font) fonts[count++] = font;
    }
    return count;
}

static bool init_tab_resources(WM *wm)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    Colormap colormap = DefaultColormap(wm->display, wm->screen);
    wm->tab_font_count = load_tab_fonts(wm, wm->config.tab_font, false,
                                        wm->tab_fonts);
    wm->tab_font_bold_count = load_tab_fonts(wm, wm->config.tab_font_bold, true,
                                             wm->tab_fonts_bold);
    if (!wm->tab_font_count || !wm->tab_font_bold_count) {
        fprintf(stderr, "box2430: cannot open configured tab bar fonts\n");
        return false;
    }
    XftColor *colors[] = {
        &wm->tab_active_fg, &wm->tab_active_bg,
        &wm->tab_inactive_fg, &wm->tab_inactive_bg,
        &wm->tab_urgent_fg, &wm->tab_urgent_bg,
    };
    const char *names[] = {
        wm->config.tab_active_fg, wm->config.tab_active_bg,
        wm->config.tab_inactive_fg, wm->config.tab_inactive_bg,
        wm->config.tab_urgent_fg, wm->config.tab_urgent_bg,
    };
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
        if (!XftColorAllocName(wm->display, visual, colormap, names[i], colors[i])) {
            fprintf(stderr, "box2430: cannot allocate tab bar color %s\n", names[i]);
            for (size_t j = 0; j < i; ++j)
                XftColorFree(wm->display, visual, colormap, colors[j]);
            return false;
        }
    }
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Monitor *monitor = &wm->monitors[i];
        if (!create_tab_bar(wm, monitor)) {
            fprintf(stderr, "box2430: cannot create tab bar drawing context\n");
            return false;
        }
    }
    wm->tab_resources_ready = true;
    return true;
}

bool wm_init(WM *wm, const char *display_name, const char *config_path)
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
    if (!x11_acquire_wm_ownership(wm)) return false;
    x11_init_atoms(wm);
    x11_update_active_window(wm);
    if (!init_monitors(wm)) return false;
    recompute_workareas(wm);
    if (!init_tab_resources(wm)) return false;

    wm->focused_border = named_color(wm, wm->config.border_focused,
                                      WhitePixel(wm->display, wm->screen));
    wm->unfocused_border = named_color(wm, wm->config.border_unfocused,
                                        BlackPixel(wm->display, wm->screen));
    wm->urgent_border = named_color(wm, wm->config.border_urgent,
                                     WhitePixel(wm->display, wm->screen));
    wm->snap_preview_color = named_color(wm, wm->config.snap_preview_color,
                                         WhitePixel(wm->display, wm->screen));
    grab_default_keys(wm);
    discover_existing_windows(wm);
    XSync(wm->display, False);
    return true;
}

void wm_run(WM *wm)
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

    struct pollfd descriptor = {.fd = wm->x_fd, .events = POLLIN};
    while (wm->running && !stop_requested) {
        while (XPending(wm->display)) {
            XEvent event;
            XNextEvent(wm->display, &event);
            handle_event(wm, &event);
        }
        if (!wm->running || stop_requested) break;
        int result = poll(&descriptor, 1, -1);
        if (result < 0 && errno != EINTR) {
            fprintf(stderr, "box2430: poll: %s\n", strerror(errno));
            break;
        }
    }
}

void wm_destroy(WM *wm)
{
    if (!wm->display) return;
    client_commit_mru_cycle(wm);
    XSync(wm->display, False);
    while (XPending(wm->display)) {
        XEvent event;
        XNextEvent(wm->display, &event);
        if (event.type == DestroyNotify || event.type == UnmapNotify)
            handle_event(wm, &event);
    }
    while (wm->clients) {
        unmanage_client(wm, wm->clients, true);
    }
    while (wm->special_windows)
        unmanage_special_window(wm, wm->special_windows, true);
    XDeleteProperty(wm->display, wm->root, wm->atoms.net_active_window);
    XDeleteProperty(wm->display, wm->root, wm->atoms.net_client_list);
    XDeleteProperty(wm->display, wm->root, wm->atoms.net_client_list_stacking);
    XDeleteProperty(wm->display, wm->root, wm->atoms.net_supported);
    XDeleteProperty(wm->display, wm->root, wm->atoms.net_workarea);
    for (size_t i = 0; i < 4; ++i)
        if (wm->drag.preview_windows[i])
            XDestroyWindow(wm->display, wm->drag.preview_windows[i]);
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        if (wm->monitors[i].tab_draw) XftDrawDestroy(wm->monitors[i].tab_draw);
        if (wm->monitors[i].tab_bar) XDestroyWindow(wm->display, wm->monitors[i].tab_bar);
    }
    if (wm->tab_resources_ready) {
        Visual *visual = DefaultVisual(wm->display, wm->screen);
        Colormap colormap = DefaultColormap(wm->display, wm->screen);
        XftColor *colors[] = {
            &wm->tab_active_fg, &wm->tab_active_bg,
            &wm->tab_inactive_fg, &wm->tab_inactive_bg,
            &wm->tab_urgent_fg, &wm->tab_urgent_bg,
        };
        for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i)
            XftColorFree(wm->display, visual, colormap, colors[i]);
    }
    for (unsigned int i = 0; i < wm->tab_font_count; ++i)
        XftFontClose(wm->display, wm->tab_fonts[i]);
    for (unsigned int i = 0; i < wm->tab_font_bold_count; ++i)
        XftFontClose(wm->display, wm->tab_fonts_bold[i]);
    XSync(wm->display, False);
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        free(wm->monitors[i].workspaces);
    free(wm->monitors);
    XCloseDisplay(wm->display);
    wm->display = NULL;
}
