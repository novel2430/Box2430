#include <X11/Xlib.h>

#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s #RRGGBB\n", argv[0]);
        return 2;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }

    int screen = DefaultScreen(display);
    Colormap colormap = DefaultColormap(display, screen);
    XColor color;
    XColor exact;
    if (!XAllocNamedColor(display, colormap, argv[1], &color, &exact)) {
        fprintf(stderr, "cannot allocate color %s\n", argv[1]);
        XCloseDisplay(display);
        return 1;
    }

    Window root = RootWindow(display, screen);
    XSetWindowBackground(display, root, color.pixel);
    XClearWindow(display, root);
    XSync(display, False);
    XCloseDisplay(display);
    return 0;
}
