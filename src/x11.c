#include "box2430.h"

#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ownership_failed;

static int ownership_error_handler(Display *display, XErrorEvent *event)
{
    (void)display;
    if (event->error_code == BadAccess) {
        ownership_failed = true;
    }
    return 0;
}

static int runtime_error_handler(Display *display, XErrorEvent *event)
{
    if (event->error_code == BadWindow) return 0;
    char message[128];
    XGetErrorText(display, event->error_code, message, sizeof(message));
    fprintf(stderr, "box2430: X11 error: %s (request %u.%u, resource 0x%lx)\n",
            message, event->request_code, event->minor_code, event->resourceid);
    return 0;
}

bool x11_acquire_wm_ownership(WM *wm)
{
    ownership_failed = false;
    XSetErrorHandler(ownership_error_handler);
    XSelectInput(wm->display, wm->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                     StructureNotifyMask | PropertyChangeMask |
                     FocusChangeMask);
    XSync(wm->display, False);
    XSetErrorHandler(runtime_error_handler);

    if (ownership_failed) {
        fprintf(stderr, "box2430: another window manager owns display %s\n",
                DisplayString(wm->display));
        return false;
    }
    return true;
}

void x11_init_atoms(WM *wm)
{
    wm->atoms.wm_protocols = XInternAtom(wm->display, "WM_PROTOCOLS", False);
    wm->atoms.wm_delete_window = XInternAtom(wm->display, "WM_DELETE_WINDOW", False);
    wm->atoms.wm_take_focus = XInternAtom(wm->display, "WM_TAKE_FOCUS", False);
    wm->atoms.wm_state = XInternAtom(wm->display, "WM_STATE", False);
    wm->atoms.net_supported = XInternAtom(wm->display, "_NET_SUPPORTED", False);
    wm->atoms.net_supporting_wm_check =
        XInternAtom(wm->display, "_NET_SUPPORTING_WM_CHECK", False);
    wm->atoms.net_active_window = XInternAtom(wm->display, "_NET_ACTIVE_WINDOW", False);
    wm->atoms.net_client_list = XInternAtom(wm->display, "_NET_CLIENT_LIST", False);
    wm->atoms.net_client_list_stacking =
        XInternAtom(wm->display, "_NET_CLIENT_LIST_STACKING", False);
    wm->atoms.net_wm_state = XInternAtom(wm->display, "_NET_WM_STATE", False);
    wm->atoms.net_wm_state_fullscreen =
        XInternAtom(wm->display, "_NET_WM_STATE_FULLSCREEN", False);
    wm->atoms.net_close_window = XInternAtom(wm->display, "_NET_CLOSE_WINDOW", False);
    wm->atoms.utf8_string = XInternAtom(wm->display, "UTF8_STRING", False);
    wm->atoms.net_wm_name = XInternAtom(wm->display, "_NET_WM_NAME", False);
    wm->atoms.net_wm_window_type =
        XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE", False);
    wm->atoms.net_wm_window_type_normal =
        XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    wm->atoms.net_wm_window_type_dialog =
        XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    wm->atoms.net_wm_window_type_dock =
        XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    wm->atoms.net_wm_window_type_desktop =
        XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    wm->atoms.net_wm_window_type_notification =
        XInternAtom(wm->display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    wm->atoms.net_wm_strut = XInternAtom(wm->display, "_NET_WM_STRUT", False);
    wm->atoms.net_wm_strut_partial =
        XInternAtom(wm->display, "_NET_WM_STRUT_PARTIAL", False);
    wm->atoms.net_workarea = XInternAtom(wm->display, "_NET_WORKAREA", False);

    Atom supported[] = {
        wm->atoms.net_supported,
        wm->atoms.net_supporting_wm_check,
        wm->atoms.net_active_window,
        wm->atoms.net_client_list,
        wm->atoms.net_client_list_stacking,
        wm->atoms.net_wm_state,
        wm->atoms.net_wm_state_fullscreen,
        wm->atoms.net_close_window,
        wm->atoms.net_wm_name,
        wm->atoms.net_wm_window_type,
        wm->atoms.net_wm_window_type_normal,
        wm->atoms.net_wm_window_type_dialog,
        wm->atoms.net_wm_window_type_dock,
        wm->atoms.net_wm_window_type_desktop,
        wm->atoms.net_wm_window_type_notification,
        wm->atoms.net_wm_strut,
        wm->atoms.net_wm_strut_partial,
        wm->atoms.net_workarea,
    };
    wm->wm_check_window = XCreateSimpleWindow(
        wm->display, wm->root, -1, -1, 1, 1, 0,
        BlackPixel(wm->display, wm->screen), BlackPixel(wm->display, wm->screen));
    if (wm->wm_check_window) {
        XChangeProperty(wm->display, wm->wm_check_window,
                        wm->atoms.net_supporting_wm_check, XA_WINDOW, 32,
                        PropModeReplace,
                        (unsigned char *)&wm->wm_check_window, 1);
        static const char wm_name[] = "Box2430";
        XChangeProperty(wm->display, wm->wm_check_window, wm->atoms.net_wm_name,
                        wm->atoms.utf8_string, 8, PropModeReplace,
                        (const unsigned char *)wm_name, sizeof(wm_name) - 1U);
        XStoreName(wm->display, wm->wm_check_window, wm_name);
        XChangeProperty(wm->display, wm->root,
                        wm->atoms.net_supporting_wm_check, XA_WINDOW, 32,
                        PropModeReplace,
                        (unsigned char *)&wm->wm_check_window, 1);
    }

    XChangeProperty(wm->display, wm->root, wm->atoms.net_supported, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)supported,
                    (int)(sizeof(supported) / sizeof(supported[0])));
}

WindowType x11_read_window_type(WM *wm, Window window)
{
    Atom actual_type;
    int actual_format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    WindowType type = WINDOW_TYPE_NORMAL;
    if (XGetWindowProperty(wm->display, window, wm->atoms.net_wm_window_type,
                           0, 32, False, XA_ATOM, &actual_type, &actual_format,
                           &count, &remaining, &data) == Success && data &&
        actual_type == XA_ATOM && actual_format == 32 && count > 0) {
        Atom atom = ((Atom *)data)[0];
        if (atom == wm->atoms.net_wm_window_type_dialog) type = WINDOW_TYPE_DIALOG;
        else if (atom == wm->atoms.net_wm_window_type_dock) type = WINDOW_TYPE_DOCK;
        else if (atom == wm->atoms.net_wm_window_type_desktop) type = WINDOW_TYPE_DESKTOP;
        else if (atom == wm->atoms.net_wm_window_type_notification)
            type = WINDOW_TYPE_NOTIFICATION;
    }
    if (data) XFree(data);
    Window transient;
    if (type == WINDOW_TYPE_NORMAL && XGetTransientForHint(wm->display, window, &transient))
        type = WINDOW_TYPE_DIALOG;
    return type;
}


static bool valid_utf8(const unsigned char *text, size_t length)
{
    size_t i = 0;
    while (i < length) {
        unsigned char first = text[i];
        if (first < 0x80) {
            ++i;
            continue;
        }
        unsigned int needed;
        unsigned int value;
        unsigned int minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            needed = 1; value = first & 0x1fU; minimum = 0x80U;
        } else if (first >= 0xe0 && first <= 0xef) {
            needed = 2; value = first & 0x0fU; minimum = 0x800U;
        } else if (first >= 0xf0 && first <= 0xf4) {
            needed = 3; value = first & 0x07U; minimum = 0x10000U;
        } else {
            return false;
        }
        if (i + needed >= length) return false;
        for (unsigned int j = 1; j <= needed; ++j) {
            unsigned char next = text[i + j];
            if ((next & 0xc0U) != 0x80U) return false;
            value = (value << 6) | (next & 0x3fU);
        }
        if (value < minimum || value > 0x10ffffU ||
            (value >= 0xd800U && value <= 0xdfffU)) return false;
        i += needed + 1U;
    }
    return true;
}

char *x11_read_root_status(WM *wm)
{
    XTextProperty property = {0};
    if (XGetTextProperty(wm->display, wm->root, &property,
                         wm->atoms.net_wm_name)) {
        bool usable = property.encoding == wm->atoms.utf8_string &&
                      property.format == 8 &&
                      valid_utf8(property.value, property.nitems);
        if (usable) {
            char *copy = property.nitems
                ? strndup((const char *)property.value, property.nitems)
                : strdup("");
            if (property.value) XFree(property.value);
            return copy;
        }
        if (property.value) XFree(property.value);
    }

    property = (XTextProperty){0};
    if (!XGetTextProperty(wm->display, wm->root, &property, XA_WM_NAME) ||
        !property.value || !property.nitems) {
        if (property.value) XFree(property.value);
        return strdup("");
    }
    char *copy = NULL;
    if (property.encoding == XA_STRING) {
        copy = strndup((const char *)property.value, property.nitems);
    } else {
        char **list = NULL;
        int list_count = 0;
        if (XmbTextPropertyToTextList(wm->display, &property, &list,
                                      &list_count) >= Success &&
            list_count > 0 && list && list[0])
            copy = strdup(list[0]);
        if (list) XFreeStringList(list);
    }
    XFree(property.value);
    return copy ? copy : strdup("");
}

char *x11_read_window_title(WM *wm, Window window)
{
    Atom actual_type;
    int actual_format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    if (XGetWindowProperty(wm->display, window, wm->atoms.net_wm_name,
                           0, 4096, False, wm->atoms.utf8_string,
                           &actual_type, &actual_format, &count, &remaining,
                           &data) == Success && data && actual_format == 8) {
        char *copy = strndup((char *)data, count);
        XFree(data);
        return copy;
    }
    if (data) XFree(data);
    char *legacy = NULL;
    if (XFetchName(wm->display, window, &legacy) && legacy) {
        char *copy = strdup(legacy);
        XFree(legacy);
        return copy;
    }
    return strdup("");
}

void x11_read_window_class(WM *wm, Window window, char **instance,
                           char **class_name)
{
    XClassHint hint = {0};
    if (XGetClassHint(wm->display, window, &hint)) {
        *instance = strdup(hint.res_name ? hint.res_name : "");
        *class_name = strdup(hint.res_class ? hint.res_class : "");
        if (hint.res_name) XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
    } else {
        *instance = strdup("");
        *class_name = strdup("");
    }
}

bool x11_window_requests_fullscreen(WM *wm, Window window)
{
    Atom actual_type;
    int actual_format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    bool requested = false;
    if (XGetWindowProperty(wm->display, window, wm->atoms.net_wm_state,
                           0, 64, False, XA_ATOM, &actual_type, &actual_format,
                           &count, &remaining, &data) == Success && data &&
        actual_type == XA_ATOM && actual_format == 32) {
        Atom *states = (Atom *)data;
        for (unsigned long i = 0; i < count; ++i)
            if (states[i] == wm->atoms.net_wm_state_fullscreen) requested = true;
    }
    if (data) XFree(data);
    return requested;
}

static bool read_cardinals(WM *wm, Window window, Atom property,
                           unsigned long *values, unsigned long wanted)
{
    Atom actual_type;
    int actual_format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    bool ok = false;
    if (XGetWindowProperty(wm->display, window, property, 0, (long)wanted,
                           False, XA_CARDINAL, &actual_type, &actual_format,
                           &count, &remaining, &data) == Success && data &&
        actual_type == XA_CARDINAL && actual_format == 32 && count >= wanted) {
        unsigned long *source = (unsigned long *)data;
        for (unsigned long i = 0; i < wanted; ++i) values[i] = source[i];
        ok = true;
    }
    if (data) XFree(data);
    return ok;
}

bool x11_read_strut(WM *wm, Window window, unsigned long strut[12])
{
    memset(strut, 0, 12 * sizeof(*strut));
    if (read_cardinals(wm, window, wm->atoms.net_wm_strut_partial, strut, 12))
        return true;
    if (!read_cardinals(wm, window, wm->atoms.net_wm_strut, strut, 4)) return false;
    unsigned long width = (unsigned long)DisplayWidth(wm->display, wm->screen);
    unsigned long height = (unsigned long)DisplayHeight(wm->display, wm->screen);
    strut[4] = strut[6] = 0;
    strut[5] = strut[7] = height ? height - 1 : 0;
    strut[8] = strut[10] = 0;
    strut[9] = strut[11] = width ? width - 1 : 0;
    return true;
}

void x11_update_workarea(WM *wm)
{
    Monitor *monitor = wm->selected_monitor ? wm->selected_monitor : &wm->monitors[0];
    unsigned long values[4] = {
        (unsigned long)monitor->workarea.x, (unsigned long)monitor->workarea.y,
        (unsigned long)monitor->workarea.width, (unsigned long)monitor->workarea.height,
    };
    XChangeProperty(wm->display, wm->root, wm->atoms.net_workarea,
                    XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)values, 4);
}

void x11_set_wm_state(WM *wm, Window window, long state)
{
    long values[2] = {state, None};
    XChangeProperty(wm->display, window, wm->atoms.wm_state,
                    wm->atoms.wm_state, 32, PropModeReplace,
                    (unsigned char *)values, 2);
}

bool x11_window_is_iconic(WM *wm, Window window)
{
    Atom actual_type;
    int actual_format;
    unsigned long count, remaining;
    unsigned char *data = NULL;
    bool iconic = false;
    if (XGetWindowProperty(wm->display, window, wm->atoms.wm_state,
                           0, 2, False, wm->atoms.wm_state,
                           &actual_type, &actual_format, &count, &remaining,
                           &data) == Success && data &&
        actual_type == wm->atoms.wm_state && actual_format == 32 && count >= 1) {
        iconic = ((long *)data)[0] == IconicState;
    }
    if (data) XFree(data);
    return iconic;
}

void x11_update_client_lists(WM *wm)
{
    unsigned long count = 0;
    for (Client *client = wm->clients; client; client = client->next) {
        ++count;
    }
    for (SpecialWindow *special = wm->special_windows; special; special = special->next)
        ++count;

    Window *windows = NULL;
    Window *stacking = NULL;
    if (count > 0) {
        windows = malloc(count * sizeof(*windows));
        stacking = malloc(count * sizeof(*stacking));
        if (!windows || !stacking) {
            fprintf(stderr, "box2430: out of memory updating client list\n");
            free(windows);
            free(stacking);
            wm->running = false;
            return;
        }
        unsigned long i = 0;
        for (Client *client = wm->clients; client; client = client->next) {
            windows[i++] = client->window;
        }
        for (SpecialWindow *special = wm->special_windows; special; special = special->next)
            windows[i++] = special->window;
        i = 0;
        for (SpecialWindow *special = wm->special_windows; special; special = special->next)
            if (special->type == WINDOW_TYPE_DESKTOP) stacking[i++] = special->window;
        for (unsigned int monitor = 0; monitor < wm->monitor_count; ++monitor) {
            for (unsigned int workspace = 0; workspace < wm->config.workspace_count;
                 ++workspace) {
                for (Client *client = wm->monitors[monitor].workspaces[workspace].stack_head;
                     client; client = client->stack_next) {
                    if (!client->fullscreen) stacking[i++] = client->window;
                }
            }
        }
        for (SpecialWindow *special = wm->special_windows; special; special = special->next)
            if (special->type != WINDOW_TYPE_DESKTOP) stacking[i++] = special->window;
        for (unsigned int monitor = 0; monitor < wm->monitor_count; ++monitor)
            for (unsigned int workspace = 0; workspace < wm->config.workspace_count;
                 ++workspace)
                for (Client *client = wm->monitors[monitor].workspaces[workspace].stack_head;
                     client; client = client->stack_next)
                    if (client->fullscreen) stacking[i++] = client->window;
    }

    XChangeProperty(wm->display, wm->root, wm->atoms.net_client_list,
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)windows,
                    (int)count);
    XChangeProperty(wm->display, wm->root, wm->atoms.net_client_list_stacking,
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)stacking,
                    (int)count);
    free(windows);
    free(stacking);
}

void x11_update_active_window(WM *wm)
{
    Window active = wm->focused_client ? wm->focused_client->window : None;
    XChangeProperty(wm->display, wm->root, wm->atoms.net_active_window,
                    XA_WINDOW, 32, PropModeReplace, (unsigned char *)&active, 1);
}
