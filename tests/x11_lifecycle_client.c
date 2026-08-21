#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_metadata(Display *display, Window window, const char *title,
                         const char *class_name)
{
    XStoreName(display, window, title);
    XClassHint hint = {
        .res_name = (char *)title,
        .res_class = (char *)class_name,
    };
    XSetClassHint(display, window, &hint);
}

static void run(Display *display)
{
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == DestroyNotify) return;
    }
}

static Window create_window(Display *display, int screen, const char *title,
                            const char *class_name, unsigned int border_width)
{
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(
        display, root, 40, 50, 240, 120, border_width,
        BlackPixel(display, screen), WhitePixel(display, screen));
    set_metadata(display, window, title, class_name);
    XSelectInput(display, window, StructureNotifyMask);
    return window;
}

static void set_wm_state(Display *display, Window window, long state)
{
    Atom wm_state = XInternAtom(display, "WM_STATE", False);
    long values[2] = {state, None};
    XChangeProperty(display, window, wm_state, wm_state, 32,
                    PropModeReplace, (unsigned char *)values, 2);
}

static int send_withdrawal(Display *display, Window window)
{
    Window root = DefaultRootWindow(display);
    XEvent event = {0};
    event.xunmap.type = UnmapNotify;
    event.xunmap.display = display;
    event.xunmap.event = root;
    event.xunmap.window = window;
    event.xunmap.from_configure = False;
    XSendEvent(display, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XSync(display, False);
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s border TITLE WIDTH\n"
            "       %s transient PARENT_TITLE DIALOG_TITLE\n"
            "       %s ordinary|iconic TITLE\n"
            "       %s map|withdraw WINDOW\n",
            program, program, program, program);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) usage(argv[0]);
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    int screen = DefaultScreen(display);

    if (argc == 3 && strcmp(argv[1], "map") == 0) {
        XMapWindow(display, (Window)strtoul(argv[2], NULL, 0));
        XSync(display, False);
        XCloseDisplay(display);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "withdraw") == 0) {
        int result = send_withdrawal(
            display, (Window)strtoul(argv[2], NULL, 0));
        XCloseDisplay(display);
        return result;
    }

    if (argc == 4 && strcmp(argv[1], "border") == 0) {
        unsigned int border_width = (unsigned int)strtoul(argv[3], NULL, 10);
        Window window = create_window(display, screen, argv[2],
                                      "LifecycleBorder", border_width);
        XMapWindow(display, window);
        printf("0x%lx\n", window);
    } else if (argc == 4 && strcmp(argv[1], "transient") == 0) {
        Window parent = create_window(display, screen, argv[2],
                                      "LifecycleParent", 1);
        Window dialog = create_window(display, screen, argv[3],
                                      "LifecycleDialog", 1);
        XSetTransientForHint(display, dialog, parent);
        XMapWindow(display, parent);
        XMapWindow(display, dialog);
        /* Put the transient before its parent in root stacking order.  Startup
           discovery must use lifecycle semantics rather than that order. */
        XRaiseWindow(display, parent);
        printf("0x%lx 0x%lx\n", parent, dialog);
    } else if (argc == 3 && strcmp(argv[1], "ordinary") == 0) {
        Window window = create_window(display, screen, argv[2],
                                      "LifecycleOrdinary", 1);
        XMapWindow(display, window);
        printf("0x%lx\n", window);
    } else if (argc == 3 && strcmp(argv[1], "iconic") == 0) {
        Window window = create_window(display, screen, argv[2],
                                      "LifecycleIconic", 3);
        set_wm_state(display, window, IconicState);
        printf("0x%lx\n", window);
    } else {
        usage(argv[0]);
    }
    fflush(stdout);
    XFlush(display);
    run(display);
    XCloseDisplay(display);
    return 0;
}
