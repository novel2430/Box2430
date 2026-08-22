#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s WINDOW add|remove|toggle STATE1 [STATE2]\n", program);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) usage(argv[0]);

    char *end = NULL;
    Window window = (Window)strtoul(argv[1], &end, 0);
    if (!end || *end != '\0' || window == None) usage(argv[0]);

    long action;
    if (strcmp(argv[2], "remove") == 0) action = 0;
    else if (strcmp(argv[2], "add") == 0) action = 1;
    else if (strcmp(argv[2], "toggle") == 0) action = 2;
    else usage(argv[0]);

    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Window root = DefaultRootWindow(display);
    Atom net_wm_state = XInternAtom(display, "_NET_WM_STATE", False);
    Atom state1 = XInternAtom(display, argv[3], False);
    Atom state2 = argc == 5 ? XInternAtom(display, argv[4], False) : None;

    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = net_wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = action;
    event.xclient.data.l[1] = (long)state1;
    event.xclient.data.l[2] = (long)state2;
    event.xclient.data.l[3] = 1; /* normal application */

    int ok = XSendEvent(display, root, False,
                        SubstructureRedirectMask | SubstructureNotifyMask,
                        &event);
    XFlush(display);
    XCloseDisplay(display);
    return ok ? 0 : 1;
}
