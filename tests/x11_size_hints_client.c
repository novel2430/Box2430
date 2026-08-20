#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <string.h>

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(
        display, root, 50, 50, 300, 200, 0,
        BlackPixel(display, screen), WhitePixel(display, screen));
    const char *title = "SizeHintsClient";
    XStoreName(display, window, title);
    XClassHint class_hint = {.res_name = (char *)title,
                             .res_class = "Box2430Fixture"};
    XSetClassHint(display, window, &class_hint);
    XSizeHints hints = {0};
    hints.flags = PMinSize | PMaxSize | PBaseSize | PResizeInc | PAspect;
    hints.min_width = 110;
    hints.min_height = 80;
    hints.max_width = 510;
    hints.max_height = 410;
    hints.base_width = 10;
    hints.base_height = 10;
    hints.width_inc = 10;
    hints.height_inc = 10;
    hints.min_aspect.x = 4;
    hints.min_aspect.y = 3;
    hints.max_aspect.x = 16;
    hints.max_aspect.y = 9;
    XSetWMNormalHints(display, window, &hints);
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
