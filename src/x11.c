#include "microbox.h"

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
    fprintf(stderr, "microbox: X11 error: %s (request %u.%u, resource 0x%lx)\n",
            message, event->request_code, event->minor_code, event->resourceid);
    return 0;
}

bool x11_acquire_wm_ownership(WM *wm)
{
    ownership_failed = false;
    XSetErrorHandler(ownership_error_handler);
    XSelectInput(wm->display, wm->root,
                 SubstructureRedirectMask | SubstructureNotifyMask |
                     StructureNotifyMask | PropertyChangeMask);
    XSync(wm->display, False);
    XSetErrorHandler(runtime_error_handler);

    if (ownership_failed) {
        fprintf(stderr, "microbox: another window manager owns display %s\n",
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
            fprintf(stderr, "microbox: out of memory updating client list\n");
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
