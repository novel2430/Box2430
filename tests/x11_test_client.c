#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s TYPE TITLE X Y WIDTH HEIGHT [TOP_STRUT]\n", program);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc != 7 && argc != 8) usage(argv[0]);
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    int x = atoi(argv[3]);
    int y = atoi(argv[4]);
    unsigned int width = (unsigned int)strtoul(argv[5], NULL, 10);
    unsigned int height = (unsigned int)strtoul(argv[6], NULL, 10);
    Window window = XCreateSimpleWindow(display, root, x, y, width, height,
                                         0, BlackPixel(display, screen),
                                         WhitePixel(display, screen));
    XStoreName(display, window, argv[2]);
    Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    XChangeProperty(display, window, net_wm_name, utf8, 8, PropModeReplace,
                    (unsigned char *)argv[2], (int)strlen(argv[2]));
    XClassHint class_hint = {.res_name = argv[2], .res_class = "MicroboxFixture"};
    XSetClassHint(display, window, &class_hint);

    char atom_name[128];
    snprintf(atom_name, sizeof(atom_name), "_NET_WM_WINDOW_TYPE_%s", argv[1]);
    Atom type_property = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom type = XInternAtom(display, atom_name, False);
    XChangeProperty(display, window, type_property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&type, 1);
    if (argc == 8) {
        unsigned long strut[12] = {0};
        strut[2] = strtoul(argv[7], NULL, 10);
        strut[8] = 0;
        strut[9] = (unsigned long)DisplayWidth(display, screen) - 1;
        Atom property = XInternAtom(display, "_NET_WM_STRUT_PARTIAL", False);
        XChangeProperty(display, window, property, XA_CARDINAL, 32,
                        PropModeReplace, (unsigned char *)strut, 12);
    }
    Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &delete_window, 1);
    XSelectInput(display, window, StructureNotifyMask);
    XMapWindow(display, window);
    XFlush(display);

    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == DestroyNotify) break;
        if (event.type == ClientMessage &&
            event.xclient.message_type == protocols &&
            event.xclient.data.l[0] == (long)delete_window) break;
    }
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
