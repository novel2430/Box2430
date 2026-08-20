#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[1], "take") != 0 && strcmp(argv[1], "none") != 0))
        return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), 40, 40, 220, 100, 0,
        BlackPixel(display, screen), WhitePixel(display, screen));
    XStoreName(display, window, argv[2]);
    XWMHints *hints = XAllocWMHints();
    if (!hints) return 1;
    hints->flags = InputHint;
    hints->input = False;
    XSetWMHints(display, window, hints);
    XFree(hints);
    Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom take_focus = XInternAtom(display, "WM_TAKE_FOCUS", False);
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    Atom supported[2] = {delete_window, take_focus};
    XSetWMProtocols(display, window, supported,
                    strcmp(argv[1], "take") == 0 ? 2 : 1);
    XSelectInput(display, window, StructureNotifyMask);
    XMapWindow(display, window);
    XFlush(display);
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == DestroyNotify) break;
        if (event.type == ClientMessage && event.xclient.message_type == protocols) {
            if (event.xclient.data.l[0] == (long)take_focus) {
                FILE *marker = fopen(argv[3], "w");
                if (marker) { fputs("WM_TAKE_FOCUS\n", marker); fclose(marker); }
            } else if (event.xclient.data.l[0] == (long)delete_window) {
                break;
            }
        }
    }
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
