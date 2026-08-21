#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void set_default_hints(Display *display, Window window)
{
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
}

static void set_increment_hints(Display *display, Window window, bool updated)
{
    XSizeHints hints = {0};
    hints.flags = PMinSize | PMaxSize | PBaseSize | PResizeInc;
    hints.base_width = updated ? 80 : 100;
    hints.base_height = updated ? 80 : 100;
    hints.min_width = hints.base_width;
    hints.min_height = hints.base_height;
    hints.max_width = 700;
    hints.max_height = 500;
    hints.width_inc = updated ? 30 : 20;
    hints.height_inc = updated ? 25 : 10;
    XSetWMNormalHints(display, window, &hints);
}

int main(int argc, char **argv)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    if (argc == 3 && strcmp(argv[1], "update") == 0) {
        char *end = NULL;
        Window window = (Window)strtoul(argv[2], &end, 0);
        if (!end || *end != '\0') {
            XCloseDisplay(display);
            return 2;
        }
        set_increment_hints(display, window, true);
        XSync(display, False);
        XCloseDisplay(display);
        return 0;
    }

    const char *mode = argc > 1 ? argv[1] : "default";
    int x = 50;
    int y = 50;
    unsigned int width = 300;
    unsigned int height = 200;
    const char *title = "SizeHintsClient";
    if (strcmp(mode, "increment") == 0) {
        title = "IncrementHintsClient";
    } else if (strcmp(mode, "offscreen") == 0) {
        title = "OffscreenHintsClient";
        x = 1200;
        y = 900;
        width = 200;
        height = 120;
    } else if (strcmp(mode, "partial") == 0) {
        title = "PartialHintsClient";
        x = 750;
        y = 550;
        width = 200;
        height = 120;
    } else if (strcmp(mode, "default") != 0) {
        XCloseDisplay(display);
        return 2;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    Window window = XCreateSimpleWindow(
        display, root, x, y, width, height, 0,
        BlackPixel(display, screen), WhitePixel(display, screen));
    XStoreName(display, window, title);
    XClassHint class_hint = {.res_name = (char *)title,
                             .res_class = "Box2430Fixture"};
    XSetClassHint(display, window, &class_hint);
    if (strcmp(mode, "default") == 0)
        set_default_hints(display, window);
    else if (strcmp(mode, "increment") == 0)
        set_increment_hints(display, window, false);
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
