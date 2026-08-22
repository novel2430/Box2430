#include "tray.h"
#include "ui.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYSTEM_TRAY_REQUEST_DOCK 0L
#define SYSTEM_TRAY_BEGIN_MESSAGE 1L
#define SYSTEM_TRAY_CANCEL_MESSAGE 2L

#define XEMBED_EMBEDDED_NOTIFY 0L
#define XEMBED_MAPPED (1UL << 0)
#define XEMBED_VERSION 0UL
#define TRAY_ICON_SPACING 2U

typedef struct TrayIcon TrayIcon;

struct TrayIcon {
    Window window;
    unsigned int requested_width;
    unsigned int requested_height;
    unsigned int width;
    unsigned int height;
    unsigned long xembed_version;
    bool mapped;
    TrayIcon *next;
};

struct Tray {
    Window owner;
    Window host;
    Atom selection;
    Atom opcode;
    Atom orientation;
    Atom visual;
    Atom manager;
    Atom xembed;
    Atom xembed_info;
    Atom timestamp_atom;
    TrayIcon *icons;
    TrayIcon *tail;
    unsigned int slot_height;
    unsigned int width_limit;
    Rect allocation;
    bool active;
    bool host_mapped;
};

static bool tray_configured(const WM *wm)
{
    if (!wm || !wm->config.bar.enabled) return false;
    const UIBarWidget *groups[] = {
        wm->config.bar.left,
        wm->config.bar.center,
        wm->config.bar.right,
    };
    const unsigned int counts[] = {
        wm->config.bar.left_count,
        wm->config.bar.center_count,
        wm->config.bar.right_count,
    };
    for (size_t group = 0; group < sizeof(groups) / sizeof(groups[0]); ++group)
        for (unsigned int i = 0; i < counts[group]; ++i)
            if (groups[group][i] == UI_WIDGET_TRAY) return true;
    return false;
}

static TrayIcon *find_icon(const Tray *tray, Window window)
{
    if (!tray || !window) return NULL;
    for (TrayIcon *icon = tray->icons; icon; icon = icon->next)
        if (icon->window == window) return icon;
    return NULL;
}

static bool window_owned_elsewhere(const WM *wm, Window window)
{
    if (!wm || !window || window == wm->root) return true;
    const Tray *tray = wm->tray;
    if (tray && (window == tray->owner || window == tray->host ||
                 find_icon(tray, window)))
        return true;

    for (const Client *client = wm->clients; client; client = client->next)
        if (client->window == window) return true;
    for (const SpecialWindow *special = wm->special_windows; special;
         special = special->next)
        if (special->window == window) return true;
    if (ui_is_internal_window(wm, window)) return true;
    return false;
}

static unsigned int icon_width_limit(unsigned int bar_width)
{
    uint64_t spacing = 2U * (uint64_t)TRAY_ICON_SPACING;
    return bar_width > spacing ? bar_width - (unsigned int)spacing : 1U;
}

static unsigned int selected_icon_width_limit(const WM *wm)
{
    if (wm && wm->selected_monitor &&
        wm->selected_monitor->bar_geometry.width > 0)
        return icon_width_limit(
            (unsigned int)wm->selected_monitor->bar_geometry.width);
    if (wm && wm->display) {
        int width = DisplayWidth(wm->display, wm->screen);
        if (width > 0) return icon_width_limit((unsigned int)width);
    }
    return 1U;
}

static unsigned int positive_dimension(int value)
{
    return value > 0 ? (unsigned int)value : 1U;
}

typedef struct TrayTimestampWait {
    Window window;
    Atom atom;
} TrayTimestampWait;

static Bool timestamp_event(Display *display, XEvent *event, XPointer data)
{
    (void)display;
    TrayTimestampWait *wait = (TrayTimestampWait *)data;
    return event->type == PropertyNotify &&
        event->xproperty.window == wait->window &&
        event->xproperty.atom == wait->atom;
}

static Time server_timestamp(WM *wm)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->owner || !tray->timestamp_atom) return CurrentTime;
    unsigned char marker = 0;
    XChangeProperty(wm->display, tray->owner, tray->timestamp_atom,
                    tray->timestamp_atom, 8, PropModeReplace, &marker, 1);
    TrayTimestampWait wait = {tray->owner, tray->timestamp_atom};
    XEvent event;
    XIfEvent(wm->display, &event, timestamp_event, (XPointer)&wait);
    return event.xproperty.time;
}

static bool read_xembed_info(WM *wm, TrayIcon *icon,
                             unsigned long *version, bool *mapped)
{
    Tray *tray = wm->tray;
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = NULL;
    int result = XGetWindowProperty(
        wm->display, icon->window, tray->xembed_info, 0, 2, False,
        tray->xembed_info, &actual_type, &actual_format, &count, &remaining,
        &data);
    bool valid = result == Success && data &&
                 actual_type == tray->xembed_info && actual_format == 32 &&
                 count >= 2;
    if (valid) {
        unsigned long *items = (unsigned long *)data;
        *version = items[0];
        *mapped = (items[1] & XEMBED_MAPPED) != 0;
    } else {
        /* XEmbed requires the property, but mature trays are tolerant of
           clients that omit it.  Keep such icons visible rather than
           rejecting an otherwise usable tray client. */
        *version = 0;
        *mapped = true;
    }
    if (data) XFree(data);
    return valid;
}

static void send_xembed(WM *wm, Window window, Time timestamp, long message,
                        long detail, long data1, long data2)
{
    Tray *tray = wm->tray;
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = tray->xembed;
    event.xclient.format = 32;
    event.xclient.data.l[0] = (long)timestamp;
    event.xclient.data.l[1] = message;
    event.xclient.data.l[2] = detail;
    event.xclient.data.l[3] = data1;
    event.xclient.data.l[4] = data2;
    XSendEvent(wm->display, window, False, NoEventMask, &event);
}

static void tray_set_wm_state(WM *wm, Window window, long state)
{
    long data[2] = {state, None};
    XChangeProperty(wm->display, window, wm->atoms.wm_state, wm->atoms.wm_state,
                    32, PropModeReplace, (unsigned char *)data, 2);
}

static void set_icon_state(WM *wm, TrayIcon *icon, bool mapped)
{
    icon->mapped = mapped;
    if (mapped) {
        XMapWindow(wm->display, icon->window);
        tray_set_wm_state(wm, icon->window, NormalState);
    } else {
        XUnmapWindow(wm->display, icon->window);
        tray_set_wm_state(wm, icon->window, WithdrawnState);
    }
}

static unsigned int scale_width(unsigned int width, unsigned int height,
                                unsigned int target_height)
{
    if (!target_height) return 1U;
    if (!width || !height) return target_height;
    uint64_t scaled = ((uint64_t)width * target_height + height / 2U) / height;
    if (!scaled) return 1U;
    return scaled > UINT_MAX ? UINT_MAX : (unsigned int)scaled;
}

static uint64_t aspect_width(unsigned int height, int numerator,
                             int denominator, bool round_up)
{
    if (!height || numerator <= 0 || denominator <= 0) return 1U;
    uint64_t product = (uint64_t)height * (unsigned int)numerator;
    if (round_up) product += (unsigned int)denominator - 1U;
    return product / (unsigned int)denominator;
}

static void apply_icon_hints(WM *wm, TrayIcon *icon, unsigned int target_height,
                             unsigned int *width)
{
    XSizeHints hints = {0};
    long supplied = 0;
    if (!XGetWMNormalHints(wm->display, icon->window, &hints, &supplied)) return;

    uint64_t value = *width;
    int base = (hints.flags & PBaseSize) ? hints.base_width
        : (hints.flags & PMinSize) ? hints.min_width : 0;
    int minimum = (hints.flags & PMinSize) ? hints.min_width : 0;
    int maximum = (hints.flags & PMaxSize) ? hints.max_width : 0;

    if ((hints.flags & PAspect) && target_height > 0 &&
        hints.min_aspect.x > 0 && hints.min_aspect.y > 0 &&
        hints.max_aspect.x > 0 && hints.max_aspect.y > 0) {
        uint64_t minimum_left = value * (unsigned int)hints.min_aspect.y;
        uint64_t minimum_right =
            (uint64_t)target_height * (unsigned int)hints.min_aspect.x;
        uint64_t maximum_left = value * (unsigned int)hints.max_aspect.y;
        uint64_t maximum_right =
            (uint64_t)target_height * (unsigned int)hints.max_aspect.x;
        if (minimum_left < minimum_right)
            value = aspect_width(target_height, hints.min_aspect.x,
                                 hints.min_aspect.y, true);
        else if (maximum_left > maximum_right)
            value = aspect_width(target_height, hints.max_aspect.x,
                                 hints.max_aspect.y, false);
    }

    int64_t adjusted_value = value > (uint64_t)INT64_MAX
        ? INT64_MAX : (int64_t)value;
    if ((hints.flags & PResizeInc) && hints.width_inc > 0) {
        int64_t adjusted = adjusted_value - (int64_t)base;
        if (adjusted > 0) adjusted -= adjusted % hints.width_inc;
        adjusted_value = adjusted + (int64_t)base;
    }
    if (minimum > 0 && adjusted_value < minimum) adjusted_value = minimum;
    if (maximum > 0 && adjusted_value > maximum) adjusted_value = maximum;
    if (adjusted_value < 1) adjusted_value = 1;
    if ((uint64_t)adjusted_value > UINT_MAX) adjusted_value = UINT_MAX;
    *width = (unsigned int)adjusted_value;
}

static void normalize_icon(WM *wm, TrayIcon *icon, unsigned int slot_height,
                           unsigned int width_limit)
{
    if (!slot_height) slot_height = 1U;
    if (!width_limit) width_limit = 1U;
    unsigned int width = scale_width(icon->requested_width,
                                     icon->requested_height, slot_height);
    apply_icon_hints(wm, icon, slot_height, &width);
    if (!width) width = 1U;
    if (width > width_limit) width = width_limit;
    icon->width = width;
    icon->height = slot_height;
}

static void normalize_all_icons(WM *wm, unsigned int slot_height,
                                unsigned int width_limit)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->active || !slot_height || !width_limit ||
        (tray->slot_height == slot_height && tray->width_limit == width_limit))
        return;
    tray->slot_height = slot_height;
    tray->width_limit = width_limit;
    for (TrayIcon *icon = tray->icons; icon; icon = icon->next) {
        normalize_icon(wm, icon, slot_height, width_limit);
        XResizeWindow(wm->display, icon->window, icon->width, icon->height);
    }
}

static unsigned int icon_width_sum(const Tray *tray, unsigned int limit)
{
    if (!limit) return 0;
    uint64_t width = 0;
    unsigned int count = 0;
    for (const TrayIcon *icon = tray->icons; icon; icon = icon->next) {
        if (!icon->mapped) continue;
        width += icon->width;
        ++count;
        if (width >= limit) return limit;
    }
    if (!count) return 0;
    width += (uint64_t)(count + 1U) * TRAY_ICON_SPACING;
    return width >= limit ? limit : (unsigned int)width;
}

static void layout_icons(WM *wm)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->active || !tray->host || tray->allocation.width <= 0)
        return;
    unsigned int allocation_width = (unsigned int)tray->allocation.width;
    unsigned int cursor = TRAY_ICON_SPACING < allocation_width
        ? TRAY_ICON_SPACING : allocation_width;
    for (TrayIcon *icon = tray->icons; icon; icon = icon->next) {
        if (!icon->mapped) {
            XUnmapWindow(wm->display, icon->window);
            continue;
        }
        int y = tray->allocation.height > (int)icon->height
            ? (tray->allocation.height - (int)icon->height) / 2 : 0;
        XMoveResizeWindow(wm->display, icon->window, (int)cursor, y,
                          icon->width, icon->height);
        XMapWindow(wm->display, icon->window);
        uint64_t next = (uint64_t)cursor + icon->width + TRAY_ICON_SPACING;
        cursor = next >= allocation_width ? allocation_width : (unsigned int)next;
    }
    XClearWindow(wm->display, tray->host);
}

static void unlink_icon(Tray *tray, TrayIcon *icon)
{
    TrayIcon **link = &tray->icons;
    while (*link && *link != icon) link = &(*link)->next;
    if (!*link) return;
    *link = icon->next;
    if (tray->tail == icon) {
        tray->tail = tray->icons;
        while (tray->tail && tray->tail->next) tray->tail = tray->tail->next;
    }
}

static void remove_icon(Tray *tray, TrayIcon *icon)
{
    unlink_icon(tray, icon);
    free(icon);
}

static void unembed_icon(WM *wm, TrayIcon *icon)
{
    bool remap = icon->mapped;
    XUnmapWindow(wm->display, icon->window);
    XReparentWindow(wm->display, icon->window, wm->root, 0, 0);
    XRemoveFromSaveSet(wm->display, icon->window);
    tray_set_wm_state(wm, icon->window, WithdrawnState);
    if (remap) XMapWindow(wm->display, icon->window);
}

static void clear_icons(WM *wm, bool unembed)
{
    Tray *tray = wm->tray;
    for (TrayIcon *icon = tray->icons; icon;) {
        TrayIcon *next = icon->next;
        if (unembed) unembed_icon(wm, icon);
        free(icon);
        icon = next;
    }
    tray->icons = tray->tail = NULL;
}

static void deactivate(WM *wm, bool release_selection, bool unembed_icons)
{
    Tray *tray = wm->tray;
    if (!tray) return;
    bool was_active = tray->active;
    tray->active = false;
    clear_icons(wm, unembed_icons);

    if (release_selection && was_active && tray->selection && tray->owner &&
        XGetSelectionOwner(wm->display, tray->selection) == tray->owner)
        XSetSelectionOwner(wm->display, tray->selection, None, CurrentTime);
    if (tray->host) XDestroyWindow(wm->display, tray->host);
    if (tray->owner) XDestroyWindow(wm->display, tray->owner);
    tray->host = tray->owner = None;
    tray->host_mapped = false;
    tray->slot_height = 0;
    tray->width_limit = 0;
    tray->allocation = (Rect){0};
}

static bool dock_icon(WM *wm, Window window, Time timestamp)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->active || !window || window_owned_elsewhere(wm, window))
        return false;

    XWindowAttributes attributes;
    if (!XGetWindowAttributes(wm->display, window, &attributes) ||
        attributes.class == InputOnly)
        return false;

    TrayIcon *icon = calloc(1, sizeof(*icon));
    if (!icon) {
        fprintf(stderr, "box2430: out of memory docking tray icon\n");
        return false;
    }
    icon->window = window;
    icon->requested_width = attributes.width > 0 ? (unsigned int)attributes.width : 1U;
    icon->requested_height = attributes.height > 0 ? (unsigned int)attributes.height : 1U;
    read_xembed_info(wm, icon, &icon->xembed_version, &icon->mapped);
    unsigned int slot_height = tray->slot_height;
    if (!slot_height && wm->selected_monitor &&
        wm->selected_monitor->bar_geometry.height > 0)
        slot_height = (unsigned int)wm->selected_monitor->bar_geometry.height;
    if (!slot_height) slot_height = wm->config.bar.height ? wm->config.bar.height : 1U;
    unsigned int width_limit = selected_icon_width_limit(wm);
    normalize_icon(wm, icon, slot_height, width_limit);
    tray->slot_height = slot_height;
    tray->width_limit = width_limit;

    XAddToSaveSet(wm->display, window);
    XSelectInput(wm->display, window,
                 StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
    XReparentWindow(wm->display, window, tray->host, 0, 0);
    XResizeWindow(wm->display, window, icon->width, icon->height);
    set_icon_state(wm, icon, icon->mapped);

    if (tray->tail) tray->tail->next = icon;
    else tray->icons = icon;
    tray->tail = icon;

    unsigned long version = XEMBED_VERSION;
    send_xembed(wm, window, timestamp, XEMBED_EMBEDDED_NOTIFY, 0,
                (long)tray->host, (long)version);
    return true;
}

static bool refresh_icon_state(WM *wm, TrayIcon *icon)
{
    unsigned long version = 0;
    bool mapped = true;
    read_xembed_info(wm, icon, &version, &mapped);
    icon->xembed_version = version;
    if (mapped == icon->mapped) return false;
    set_icon_state(wm, icon, mapped);
    return true;
}

static bool resize_icon(WM *wm, TrayIcon *icon, unsigned int width,
                        unsigned int height)
{
    if (!width) width = 1U;
    if (!height) height = 1U;
    unsigned int old_width = icon->width;
    unsigned int old_height = icon->height;
    icon->requested_width = width;
    icon->requested_height = height;
    unsigned int slot_height = wm->tray->slot_height;
    if (!slot_height) slot_height = wm->config.bar.height ? wm->config.bar.height : 1U;
    unsigned int width_limit = selected_icon_width_limit(wm);
    normalize_icon(wm, icon, slot_height, width_limit);
    wm->tray->width_limit = width_limit;
    XResizeWindow(wm->display, icon->window, icon->width, icon->height);
    return old_width != icon->width || old_height != icon->height;
}

bool tray_init(WM *wm)
{
    if (!tray_configured(wm)) return true;

    Tray *tray = calloc(1, sizeof(*tray));
    if (!tray) {
        fprintf(stderr, "box2430: out of memory creating system tray\n");
        return false;
    }
    wm->tray = tray;

    char selection_name[64];
    snprintf(selection_name, sizeof(selection_name), "_NET_SYSTEM_TRAY_S%d",
             wm->screen);
    tray->selection = XInternAtom(wm->display, selection_name, False);
    tray->opcode = XInternAtom(wm->display, "_NET_SYSTEM_TRAY_OPCODE", False);
    tray->orientation = XInternAtom(wm->display,
                                    "_NET_SYSTEM_TRAY_ORIENTATION", False);
    tray->visual = XInternAtom(wm->display, "_NET_SYSTEM_TRAY_VISUAL", False);
    tray->manager = XInternAtom(wm->display, "MANAGER", False);
    tray->xembed = XInternAtom(wm->display, "_XEMBED", False);
    tray->xembed_info = XInternAtom(wm->display, "_XEMBED_INFO", False);
    tray->timestamp_atom = XInternAtom(wm->display,
                                       "_BOX2430_TRAY_TIMESTAMP", False);

    Window existing = XGetSelectionOwner(wm->display, tray->selection);
    if (existing != None) {
        fprintf(stderr,
                "box2430: system tray selection %s already owned; tray disabled\n",
                selection_name);
        return true;
    }

    XSetWindowAttributes owner_attributes = {
        .override_redirect = True,
        .event_mask = PropertyChangeMask | StructureNotifyMask,
    };
    tray->owner = XCreateWindow(
        wm->display, wm->root, -1, -1, 1, 1, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput,
        DefaultVisual(wm->display, wm->screen),
        CWOverrideRedirect | CWEventMask, &owner_attributes);
    if (!tray->owner) {
        free(tray);
        wm->tray = NULL;
        return false;
    }
    XStoreName(wm->display, tray->owner, "box2430-tray-owner");

    XSetWindowAttributes host_attributes = {
        .override_redirect = True,
        .background_pixel = wm->bar_bg.pixel,
        .event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
                      ExposureMask | StructureNotifyMask,
    };
    tray->host = XCreateWindow(
        wm->display, wm->root, -1, -1, 1, 1, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput,
        DefaultVisual(wm->display, wm->screen),
        CWOverrideRedirect | CWBackPixel | CWEventMask, &host_attributes);
    if (!tray->host) {
        XDestroyWindow(wm->display, tray->owner);
        free(tray);
        wm->tray = NULL;
        return false;
    }
    XStoreName(wm->display, tray->host, "box2430-tray");

    unsigned long orientation = 0;
    XChangeProperty(wm->display, tray->owner, tray->orientation, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&orientation, 1);
    unsigned long visual = XVisualIDFromVisual(DefaultVisual(wm->display, wm->screen));
    XChangeProperty(wm->display, tray->owner, tray->visual, XA_VISUALID, 32,
                    PropModeReplace, (unsigned char *)&visual, 1);

    Time timestamp = server_timestamp(wm);
    XSetSelectionOwner(wm->display, tray->selection, tray->owner, timestamp);
    if (XGetSelectionOwner(wm->display, tray->selection) != tray->owner) {
        fprintf(stderr, "box2430: unable to acquire system tray selection %s\n",
                selection_name);
        XDestroyWindow(wm->display, tray->host);
        XDestroyWindow(wm->display, tray->owner);
        tray->host = tray->owner = None;
        return true;
    }

    tray->active = true;
    XEvent manager = {0};
    manager.xclient.type = ClientMessage;
    manager.xclient.window = wm->root;
    manager.xclient.message_type = tray->manager;
    manager.xclient.format = 32;
    manager.xclient.data.l[0] = (long)timestamp;
    manager.xclient.data.l[1] = (long)tray->selection;
    manager.xclient.data.l[2] = (long)tray->owner;
    XSendEvent(wm->display, wm->root, False, StructureNotifyMask, &manager);
    XSync(wm->display, False);
    return true;
}

void tray_destroy(WM *wm)
{
    if (!wm || !wm->tray) return;
    deactivate(wm, true, true);
    XSync(wm->display, False);
    free(wm->tray);
    wm->tray = NULL;
}

void tray_prepare_layout(WM *wm, const Monitor *monitor)
{
    Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || monitor != wm->selected_monitor ||
        monitor->bar_geometry.width <= 0 || monitor->bar_geometry.height <= 0)
        return;
    normalize_all_icons(wm, (unsigned int)monitor->bar_geometry.height,
                        icon_width_limit(
                            (unsigned int)monitor->bar_geometry.width));
}

unsigned int tray_widget_width(const WM *wm, const Monitor *monitor)
{
    const Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || !monitor || monitor != wm->selected_monitor ||
        monitor->bar_geometry.width <= 0 || monitor->bar_geometry.height <= 0)
        return 0;
    return icon_width_sum(tray, (unsigned int)monitor->bar_geometry.width);
}

void tray_set_allocation(WM *wm, const Monitor *monitor, Rect rect)
{
    Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || !monitor || monitor != wm->selected_monitor)
        return;

    unsigned int bar_width = monitor->bar_geometry.width > 0
        ? (unsigned int)monitor->bar_geometry.width : 0U;
    unsigned int bar_height = monitor->bar_geometry.height > 0
        ? (unsigned int)monitor->bar_geometry.height : 0U;
    if (rect.width <= 0 || rect.height <= 0 ||
        !icon_width_sum(tray, bar_width)) {
        if (tray->host_mapped) XUnmapWindow(wm->display, tray->host);
        tray->host_mapped = false;
        tray->allocation = (Rect){0};
        return;
    }

    if ((unsigned int)rect.width > bar_width) rect.width = (int)bar_width;
    if ((unsigned int)rect.height > bar_height) rect.height = (int)bar_height;
    Rect allocation = {
        monitor->bar_geometry.x + rect.x,
        monitor->bar_geometry.y + rect.y,
        rect.width,
        rect.height,
    };
    bool geometry_changed = allocation.x != tray->allocation.x ||
        allocation.y != tray->allocation.y ||
        allocation.width != tray->allocation.width ||
        allocation.height != tray->allocation.height;
    tray->allocation = allocation;
    if (geometry_changed)
        XMoveResizeWindow(wm->display, tray->host,
                          allocation.x, allocation.y,
                          (unsigned int)allocation.width,
                          (unsigned int)allocation.height);
    layout_icons(wm);
    if (!tray->host_mapped) XMapWindow(wm->display, tray->host);
    tray->host_mapped = true;
}

Window tray_host_window(const WM *wm)
{
    const Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || !tray->host || !tray->host_mapped) return None;
    return tray->host;
}

TrayEventResult tray_handle_event(WM *wm, XEvent *event)
{
    Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !event) return TRAY_EVENT_NONE;
    if (!tray->active) return TRAY_EVENT_NONE;

    TrayIcon *icon = NULL;
    switch (event->type) {
    case ClientMessage:
        if (event->xclient.message_type == tray->opcode) {
            if (event->xclient.format == 32 &&
                event->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
                Window requested = (Window)event->xclient.data.l[2];
                Time timestamp = (Time)event->xclient.data.l[0];
                bool changed = dock_icon(wm, requested, timestamp);
                return TRAY_EVENT_CONSUMED |
                    (changed ? TRAY_EVENT_CHANGED : 0U);
            }
            if (event->xclient.data.l[1] == SYSTEM_TRAY_BEGIN_MESSAGE ||
                event->xclient.data.l[1] == SYSTEM_TRAY_CANCEL_MESSAGE)
                return TRAY_EVENT_CONSUMED;
            return TRAY_EVENT_CONSUMED;
        }
        icon = find_icon(tray, event->xclient.window);
        if (icon && event->xclient.message_type == tray->xembed)
            return TRAY_EVENT_CONSUMED;
        break;
    case PropertyNotify:
        icon = find_icon(tray, event->xproperty.window);
        if (!icon) break;
        if (event->xproperty.atom == tray->xembed_info) {
            bool changed = refresh_icon_state(wm, icon);
            return TRAY_EVENT_CONSUMED |
                (changed ? TRAY_EVENT_CHANGED : 0U);
        }
        if (event->xproperty.atom == XA_WM_NORMAL_HINTS) {
            bool changed = resize_icon(wm, icon, icon->requested_width,
                                       icon->requested_height);
            return TRAY_EVENT_CONSUMED |
                (changed ? TRAY_EVENT_CHANGED : 0U);
        }
        return TRAY_EVENT_CONSUMED;
    case ResizeRequest:
        icon = find_icon(tray, event->xresizerequest.window);
        if (icon) {
            bool changed = resize_icon(wm, icon,
                                       positive_dimension(event->xresizerequest.width),
                                       positive_dimension(event->xresizerequest.height));
            return TRAY_EVENT_CONSUMED |
                (changed ? TRAY_EVENT_CHANGED : 0U);
        }
        break;
    case ConfigureRequest:
        icon = find_icon(tray, event->xconfigurerequest.window);
        if (icon) {
            unsigned int width = icon->requested_width;
            unsigned int height = icon->requested_height;
            if (event->xconfigurerequest.value_mask & CWWidth)
                width = positive_dimension(event->xconfigurerequest.width);
            if (event->xconfigurerequest.value_mask & CWHeight)
                height = positive_dimension(event->xconfigurerequest.height);
            bool changed = resize_icon(wm, icon, width, height);
            return TRAY_EVENT_CONSUMED |
                (changed ? TRAY_EVENT_CHANGED : 0U);
        }
        break;
    case MapRequest:
        icon = find_icon(tray, event->xmaprequest.window);
        if (icon) {
            bool changed = refresh_icon_state(wm, icon);
            if (icon->mapped) XMapWindow(wm->display, icon->window);
            return TRAY_EVENT_CONSUMED |
                (changed ? TRAY_EVENT_CHANGED : 0U);
        }
        break;
    case UnmapNotify:
        icon = find_icon(tray, event->xunmap.window);
        if (icon) {
            bool changed = refresh_icon_state(wm, icon);
            if (icon->mapped) XMapWindow(wm->display, icon->window);
            return TRAY_EVENT_CONSUMED |
                (changed ? TRAY_EVENT_CHANGED : 0U);
        }
        break;
    case DestroyNotify:
        icon = find_icon(tray, event->xdestroywindow.window);
        if (icon) {
            remove_icon(tray, icon);
            return TRAY_EVENT_CONSUMED | TRAY_EVENT_CHANGED;
        }
        if (event->xdestroywindow.window == tray->host) {
            fprintf(stderr, "box2430: system tray host destroyed; tray disabled\n");
            tray->host = None;
            deactivate(wm, true, false);
            return TRAY_EVENT_CONSUMED | TRAY_EVENT_CHANGED;
        }
        if (event->xdestroywindow.window == tray->owner) {
            fprintf(stderr, "box2430: system tray owner destroyed; tray disabled\n");
            tray->owner = None;
            deactivate(wm, false, true);
            return TRAY_EVENT_CONSUMED | TRAY_EVENT_CHANGED;
        }
        break;
    case ReparentNotify:
        icon = find_icon(tray, event->xreparent.window);
        if (icon) {
            if (event->xreparent.parent != tray->host) {
                XRemoveFromSaveSet(wm->display, icon->window);
                remove_icon(tray, icon);
                return TRAY_EVENT_CONSUMED | TRAY_EVENT_CHANGED;
            }
            return TRAY_EVENT_CONSUMED;
        }
        break;
    case SelectionClear:
        if (event->xselectionclear.selection == tray->selection &&
            event->xselectionclear.window == tray->owner) {
            fprintf(stderr, "box2430: system tray selection lost; tray disabled\n");
            deactivate(wm, false, true);
            return TRAY_EVENT_CONSUMED | TRAY_EVENT_CHANGED;
        }
        break;
    case Expose:
        if (event->xexpose.window == tray->host) {
            if (event->xexpose.count == 0) XClearWindow(wm->display, tray->host);
            return TRAY_EVENT_CONSUMED;
        }
        break;
    case ConfigureNotify:
        icon = find_icon(tray, event->xconfigure.window);
        if (event->xconfigure.window == tray->host ||
            event->xconfigure.window == tray->owner || icon)
            return TRAY_EVENT_CONSUMED;
        break;
    default:
        break;
    }
    return TRAY_EVENT_NONE;
}
