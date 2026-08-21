#include "tray.h"

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
    Atom manager;
    Atom xembed;
    Atom xembed_info;
    TrayIcon *icons;
    TrayIcon *tail;
    unsigned int slot_height;
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

static void apply_icon_hints(WM *wm, TrayIcon *icon, unsigned int target_height,
                             unsigned int *width)
{
    XSizeHints hints = {0};
    long supplied = 0;
    if (!XGetWMNormalHints(wm->display, icon->window, &hints, &supplied)) return;

    int value = *width > (unsigned int)INT_MAX ? INT_MAX : (int)*width;
    int base = (hints.flags & PBaseSize) ? hints.base_width
        : (hints.flags & PMinSize) ? hints.min_width : 0;
    int minimum = (hints.flags & PMinSize) ? hints.min_width : 0;
    int maximum = (hints.flags & PMaxSize) ? hints.max_width : 0;

    if ((hints.flags & PAspect) && target_height > 0 &&
        hints.min_aspect.x > 0 && hints.min_aspect.y > 0 &&
        hints.max_aspect.x > 0 && hints.max_aspect.y > 0) {
        double ratio = (double)value / target_height;
        double minimum_ratio = (double)hints.min_aspect.x / hints.min_aspect.y;
        double maximum_ratio = (double)hints.max_aspect.x / hints.max_aspect.y;
        if (ratio < minimum_ratio)
            value = (int)(target_height * minimum_ratio + 0.5);
        else if (ratio > maximum_ratio)
            value = (int)(target_height * maximum_ratio + 0.5);
    }

    if ((hints.flags & PResizeInc) && hints.width_inc > 0) {
        int adjusted = value - base;
        if (adjusted > 0) adjusted -= adjusted % hints.width_inc;
        value = adjusted + base;
    }
    if (minimum > 0 && value < minimum) value = minimum;
    if (maximum > 0 && value > maximum) value = maximum;
    if (value < 1) value = 1;
    *width = (unsigned int)value;
}

static void normalize_icon(WM *wm, TrayIcon *icon, unsigned int slot_height)
{
    if (!slot_height) slot_height = 1U;
    unsigned int width = scale_width(icon->requested_width,
                                     icon->requested_height, slot_height);
    apply_icon_hints(wm, icon, slot_height, &width);
    icon->width = width ? width : 1U;
    icon->height = slot_height;
}

static void normalize_all_icons(WM *wm, unsigned int slot_height)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->active || !slot_height || tray->slot_height == slot_height)
        return;
    tray->slot_height = slot_height;
    for (TrayIcon *icon = tray->icons; icon; icon = icon->next) {
        normalize_icon(wm, icon, slot_height);
        XResizeWindow(wm->display, icon->window, icon->width, icon->height);
    }
}

static unsigned int icon_width_sum(const Tray *tray)
{
    uint64_t width = 0;
    unsigned int count = 0;
    for (const TrayIcon *icon = tray->icons; icon; icon = icon->next) {
        if (!icon->mapped) continue;
        width += icon->width;
        ++count;
    }
    if (!count) return 0;
    width += (uint64_t)(count + 1U) * TRAY_ICON_SPACING;
    return width > UINT_MAX ? UINT_MAX : (unsigned int)width;
}

static void layout_icons(WM *wm)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->active || !tray->host) return;
    int cursor = (int)TRAY_ICON_SPACING;
    for (TrayIcon *icon = tray->icons; icon; icon = icon->next) {
        if (!icon->mapped) {
            XUnmapWindow(wm->display, icon->window);
            continue;
        }
        int y = tray->allocation.height > (int)icon->height
            ? (tray->allocation.height - (int)icon->height) / 2 : 0;
        XMoveResizeWindow(wm->display, icon->window, cursor, y,
                          icon->width, icon->height);
        XMapWindow(wm->display, icon->window);
        cursor += (int)icon->width + (int)TRAY_ICON_SPACING;
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

static void deactivate(WM *wm, bool release_selection)
{
    Tray *tray = wm->tray;
    if (!tray) return;
    for (TrayIcon *icon = tray->icons; icon;) {
        TrayIcon *next = icon->next;
        unembed_icon(wm, icon);
        free(icon);
        icon = next;
    }
    tray->icons = tray->tail = NULL;

    if (release_selection && tray->active && tray->selection && tray->owner &&
        XGetSelectionOwner(wm->display, tray->selection) == tray->owner)
        XSetSelectionOwner(wm->display, tray->selection, None, CurrentTime);
    if (tray->host) XDestroyWindow(wm->display, tray->host);
    if (tray->owner) XDestroyWindow(wm->display, tray->owner);
    tray->host = tray->owner = None;
    tray->active = false;
    tray->host_mapped = false;
    tray->allocation = (Rect){0};
}

static bool dock_icon(WM *wm, Window window, Time timestamp)
{
    Tray *tray = wm->tray;
    if (!tray || !tray->active || !window || find_icon(tray, window)) return false;

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
    normalize_icon(wm, icon, slot_height);
    tray->slot_height = slot_height;

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
    normalize_icon(wm, icon, slot_height);
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
    tray->manager = XInternAtom(wm->display, "MANAGER", False);
    tray->xembed = XInternAtom(wm->display, "_XEMBED", False);
    tray->xembed_info = XInternAtom(wm->display, "_XEMBED_INFO", False);

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

    XSetSelectionOwner(wm->display, tray->selection, tray->owner, CurrentTime);
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
    manager.xclient.data.l[0] = CurrentTime;
    manager.xclient.data.l[1] = (long)tray->selection;
    manager.xclient.data.l[2] = (long)tray->owner;
    XSendEvent(wm->display, wm->root, False, StructureNotifyMask, &manager);
    XSync(wm->display, False);
    return true;
}

void tray_destroy(WM *wm)
{
    if (!wm || !wm->tray) return;
    deactivate(wm, true);
    XSync(wm->display, False);
    free(wm->tray);
    wm->tray = NULL;
}

void tray_prepare_layout(WM *wm, const Monitor *monitor)
{
    Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || monitor != wm->selected_monitor ||
        monitor->bar_geometry.height <= 0)
        return;
    normalize_all_icons(wm, (unsigned int)monitor->bar_geometry.height);
}

unsigned int tray_widget_width(const WM *wm, const Monitor *monitor)
{
    const Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || !monitor || monitor != wm->selected_monitor ||
        monitor->bar_geometry.width <= 0 || monitor->bar_geometry.height <= 0)
        return 0;
    return icon_width_sum(tray);
}

void tray_set_allocation(WM *wm, const Monitor *monitor, Rect rect)
{
    Tray *tray = wm ? wm->tray : NULL;
    if (!tray || !tray->active || !monitor || monitor != wm->selected_monitor)
        return;

    if (rect.width <= 0 || rect.height <= 0 || !icon_width_sum(tray)) {
        if (tray->host_mapped) XUnmapWindow(wm->display, tray->host);
        tray->host_mapped = false;
        tray->allocation = (Rect){0};
        return;
    }

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

void tray_raise(WM *wm)
{
    Tray *tray = wm ? wm->tray : NULL;
    if (tray && tray->active && tray->host && tray->host_mapped)
        XRaiseWindow(wm->display, tray->host);
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
                                       (unsigned int)event->xresizerequest.width,
                                       (unsigned int)event->xresizerequest.height);
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
                width = (unsigned int)event->xconfigurerequest.width;
            if (event->xconfigurerequest.value_mask & CWHeight)
                height = (unsigned int)event->xconfigurerequest.height;
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
        if (event->xdestroywindow.window == tray->host ||
            event->xdestroywindow.window == tray->owner)
            return TRAY_EVENT_CONSUMED;
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
            deactivate(wm, false);
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
