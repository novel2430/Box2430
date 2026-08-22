#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define XEMBED_EMBEDDED_NOTIFY 0L
#define XEMBED_MAPPED (1UL << 0)
#define SYSTEM_TRAY_REQUEST_DOCK 0L

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t toggle_requested;
static volatile sig_atomic_t resize_requested;
static volatile sig_atomic_t reparent_requested;

static void on_signal(int signal_number)
{
    if (signal_number == SIGUSR1) toggle_requested = 1;
    else if (signal_number == SIGUSR2) resize_requested = 1;
    else if (signal_number == SIGHUP) reparent_requested = 1;
    else stop_requested = 1;
}

static Atom tray_selection(Display *display, int screen)
{
    char name[64];
    snprintf(name, sizeof(name), "_NET_SYSTEM_TRAY_S%d", screen);
    return XInternAtom(display, name, False);
}

static int run_watch(Display *display, int screen)
{
    Window root = RootWindow(display, screen);
    Atom selection = tray_selection(display, screen);
    Atom manager = XInternAtom(display, "MANAGER", False);
    XSelectInput(display, root, StructureNotifyMask);
    XSync(display, False);

    for (unsigned int attempts = 0; attempts < 500; ++attempts) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == ClientMessage &&
                event.xclient.message_type == manager &&
                event.xclient.format == 32 &&
                (Atom)event.xclient.data.l[1] == selection) {
                printf("0x%lx %lu\n", (Window)event.xclient.data.l[2],
                       (unsigned long)event.xclient.data.l[0]);
                fflush(stdout);
                return 0;
            }
        }
        struct timespec delay = {.tv_nsec = 10000000L};
        nanosleep(&delay, NULL);
    }
    return 1;
}

static int run_hold_selection(Display *display, int screen)
{
    Window root = RootWindow(display, screen);
    Atom selection = tray_selection(display, screen);
    Window owner = XCreateSimpleWindow(display, root, -1, -1, 1, 1, 0, 0, 0);
    XStoreName(display, owner, "box2430-test-tray-owner");
    XSetSelectionOwner(display, selection, owner, CurrentTime);
    XSync(display, False);
    if (XGetSelectionOwner(display, selection) != owner) return 1;
    printf("0x%lx\n", owner);
    fflush(stdout);
    while (!stop_requested) pause();
    if (XGetSelectionOwner(display, selection) == owner)
        XSetSelectionOwner(display, selection, None, CurrentTime);
    XDestroyWindow(display, owner);
    XSync(display, False);
    return 0;
}

static bool wait_embedded(Display *display, Window icon, Atom xembed,
                          Window *host_return)
{
    for (unsigned int attempts = 0; attempts < 500; ++attempts) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == ClientMessage && event.xclient.window == icon &&
                event.xclient.message_type == xembed &&
                event.xclient.data.l[1] == XEMBED_EMBEDDED_NOTIFY) {
                *host_return = (Window)event.xclient.data.l[3];
                return true;
            }
        }
        struct timespec delay = {.tv_nsec = 10000000L};
        nanosleep(&delay, NULL);
    }
    return false;
}

static void set_xembed_info(Display *display, Window icon, Atom xembed_info,
                            bool mapped)
{
    unsigned long values[2] = {0, mapped ? XEMBED_MAPPED : 0};
    XChangeProperty(display, icon, xembed_info, xembed_info, 32,
                    PropModeReplace, (unsigned char *)values, 2);
}

static void send_dock_request(Display *display, Window owner, Atom opcode,
                              Window icon)
{
    XEvent request = {0};
    request.xclient.type = ClientMessage;
    request.xclient.window = icon;
    request.xclient.message_type = opcode;
    request.xclient.format = 32;
    request.xclient.data.l[0] = CurrentTime;
    request.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
    request.xclient.data.l[2] = (long)icon;
    XSendEvent(display, owner, False, NoEventMask, &request);
    XFlush(display);
}

static int run_icon(Display *display, int screen, const char *name,
                    unsigned int width, unsigned int height,
                    unsigned int resize_width, unsigned int resize_height,
                    unsigned int minimum_width)
{
    Window root = RootWindow(display, screen);
    Atom selection = tray_selection(display, screen);
    Window owner = XGetSelectionOwner(display, selection);
    if (owner == None) {
        fprintf(stderr, "no system tray owner\n");
        return 1;
    }
    Atom opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    Atom xembed = XInternAtom(display, "_XEMBED", False);
    Atom xembed_info = XInternAtom(display, "_XEMBED_INFO", False);

    Window icon = XCreateSimpleWindow(display, root, 20, 20,
                                      width ? width : 1U,
                                      height ? height : 1U,
                                      0, 0, 0x335577);
    XStoreName(display, icon, name);
    XSelectInput(display, icon, StructureNotifyMask | PropertyChangeMask);
    bool mapped = true;
    set_xembed_info(display, icon, xembed_info, mapped);
    if (minimum_width) {
        XSizeHints hints = {0};
        hints.flags = PMinSize;
        hints.min_width = minimum_width > 0x7fffffffU
            ? 0x7fffffff : (int)minimum_width;
        hints.min_height = 1;
        XSetWMNormalHints(display, icon, &hints);
    }

    send_dock_request(display, owner, opcode, icon);

    Window host = None;
    if (!wait_embedded(display, icon, xembed, &host)) {
        fprintf(stderr, "did not receive XEMBED_EMBEDDED_NOTIFY\n");
        XDestroyWindow(display, icon);
        return 1;
    }
    printf("0x%lx 0x%lx\n", icon, host);
    fflush(stdout);

    while (!stop_requested) {
        if (toggle_requested) {
            toggle_requested = 0;
            mapped = !mapped;
            set_xembed_info(display, icon, xembed_info, mapped);
            XFlush(display);
        }
        if (resize_requested) {
            resize_requested = 0;
            XResizeWindow(display, icon,
                          resize_width ? resize_width : width,
                          resize_height ? resize_height : height);
            XFlush(display);
        }
        if (reparent_requested) {
            reparent_requested = 0;
            XReparentWindow(display, icon, root, 0, 0);
            XFlush(display);
        }
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            (void)event;
        }
        struct timespec delay = {.tv_nsec = 10000000L};
        nanosleep(&delay, NULL);
    }
    XDestroyWindow(display, icon);
    XSync(display, False);
    return 0;
}

static int run_request_dock(Display *display, int screen, Window window)
{
    Window owner = XGetSelectionOwner(display, tray_selection(display, screen));
    if (owner == None) return 1;
    Atom opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    send_dock_request(display, owner, opcode, window);
    XSync(display, False);
    return 0;
}

static int run_destroy_window(Display *display, Window window)
{
    XDestroyWindow(display, window);
    XSync(display, False);
    return 0;
}

static int run_visual(Display *display, int screen)
{
    Window owner = XGetSelectionOwner(display, tray_selection(display, screen));
    if (owner == None) return 1;
    Atom visual_atom = XInternAtom(display, "_NET_SYSTEM_TRAY_VISUAL", False);
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = NULL;
    int status = XGetWindowProperty(display, owner, visual_atom, 0, 1, False,
                                    XA_VISUALID, &actual_type, &actual_format,
                                    &count, &remaining, &data);
    unsigned long expected = XVisualIDFromVisual(DefaultVisual(display, screen));
    bool valid = status == Success && data && actual_type == XA_VISUALID &&
        actual_format == 32 && count == 1 &&
        ((unsigned long *)data)[0] == expected;
    if (data) XFree(data);
    if (valid) printf("0x%lx\n", expected);
    return valid ? 0 : 1;
}

static int run_synthetic_configure(Display *display, Window host, Window icon,
                                   int width, int height)
{
    XEvent event = {0};
    event.xconfigurerequest.type = ConfigureRequest;
    event.xconfigurerequest.display = display;
    event.xconfigurerequest.parent = host;
    event.xconfigurerequest.window = icon;
    event.xconfigurerequest.width = width;
    event.xconfigurerequest.height = height;
    event.xconfigurerequest.value_mask = CWWidth | CWHeight;
    XSendEvent(display, host, False, SubstructureRedirectMask, &event);
    XSync(display, False);
    return 0;
}

static int run_storm(Display *display, int screen, unsigned int count)
{
    Window root = RootWindow(display, screen);
    Atom selection = tray_selection(display, screen);
    Window owner = XGetSelectionOwner(display, selection);
    if (owner == None) return 1;
    Atom opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    Atom xembed = XInternAtom(display, "_XEMBED", False);
    Atom xembed_info = XInternAtom(display, "_XEMBED_INFO", False);

    for (unsigned int i = 0; i < count; ++i) {
        Window icon = XCreateSimpleWindow(display, root, 0, 0, 32, 32, 0, 0,
                                          0x224466);
        XSelectInput(display, icon, StructureNotifyMask | PropertyChangeMask);
        set_xembed_info(display, icon, xembed_info, true);
        send_dock_request(display, owner, opcode, icon);
        Window host = None;
        if (!wait_embedded(display, icon, xembed, &host)) return 1;

        if ((i & 1U) == 0) {
            XResizeWindow(display, icon, 4096, 1);
            set_xembed_info(display, icon, xembed_info, false);
            XDestroyWindow(display, icon);
        } else {
            set_xembed_info(display, icon, xembed_info, false);
            set_xembed_info(display, icon, xembed_info, true);
            XReparentWindow(display, icon, root, 0, 0);
            XDestroyWindow(display, icon);
        }
        XSync(display, False);
    }
    printf("%u\n", count);
    return 0;
}

static int run_ordinary_xembed(Display *display, int screen, const char *name,
                               unsigned int width, unsigned int height)
{
    Window root = RootWindow(display, screen);
    Atom xembed_info = XInternAtom(display, "_XEMBED_INFO", False);
    Window window = XCreateSimpleWindow(display, root, 20, 20,
                                        width ? width : 1U,
                                        height ? height : 1U,
                                        0, 0, 0x557733);
    XStoreName(display, window, name);
    set_xembed_info(display, window, xembed_info, true);
    XMapWindow(display, window);
    XFlush(display);
    printf("0x%lx\n", window);
    fflush(stdout);

    while (!stop_requested) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            (void)event;
        }
        struct timespec delay = {.tv_nsec = 10000000L};
        nanosleep(&delay, NULL);
    }
    XDestroyWindow(display, window);
    XSync(display, False);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);
    sigaction(SIGHUP, &action, NULL);

    Display *display = XOpenDisplay(NULL);
    if (!display) return 2;
    int screen = DefaultScreen(display);
    int result = 2;
    if (strcmp(argv[1], "watch") == 0) {
        result = run_watch(display, screen);
    } else if (strcmp(argv[1], "hold-selection") == 0) {
        result = run_hold_selection(display, screen);
    } else if (strcmp(argv[1], "owner") == 0) {
        Window owner = XGetSelectionOwner(display, tray_selection(display, screen));
        if (owner != None) printf("0x%lx\n", owner);
        result = owner != None ? 0 : 1;
    } else if (strcmp(argv[1], "visual") == 0) {
        result = run_visual(display, screen);
    } else if (strcmp(argv[1], "request-dock") == 0 && argc == 3) {
        result = run_request_dock(display, screen,
                                  (Window)strtoul(argv[2], NULL, 0));
    } else if (strcmp(argv[1], "destroy") == 0 && argc == 3) {
        result = run_destroy_window(display,
                                    (Window)strtoul(argv[2], NULL, 0));
    } else if (strcmp(argv[1], "synthetic-configure") == 0 && argc == 6) {
        result = run_synthetic_configure(
            display, (Window)strtoul(argv[2], NULL, 0),
            (Window)strtoul(argv[3], NULL, 0), atoi(argv[4]), atoi(argv[5]));
    } else if (strcmp(argv[1], "storm") == 0 && argc == 3) {
        result = run_storm(display, screen,
                           (unsigned int)strtoul(argv[2], NULL, 10));
    } else if (strcmp(argv[1], "ordinary-xembed") == 0 && argc >= 5) {
        unsigned int width = (unsigned int)strtoul(argv[3], NULL, 10);
        unsigned int height = (unsigned int)strtoul(argv[4], NULL, 10);
        result = run_ordinary_xembed(display, screen, argv[2], width, height);
    } else if (strcmp(argv[1], "icon") == 0 && argc >= 6) {
        unsigned int width = (unsigned int)strtoul(argv[3], NULL, 10);
        unsigned int height = (unsigned int)strtoul(argv[4], NULL, 10);
        unsigned int resize_width = (unsigned int)strtoul(argv[5], NULL, 10);
        unsigned int resize_height = argc >= 7
            ? (unsigned int)strtoul(argv[6], NULL, 10) : height;
        unsigned int minimum_width = argc >= 8
            ? (unsigned int)strtoul(argv[7], NULL, 10) : 0U;
        result = run_icon(display, screen, argv[2], width, height,
                          resize_width, resize_height, minimum_width);
    }
    XCloseDisplay(display);
    return result;
}
