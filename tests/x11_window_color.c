#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Window window = (Window)strtoul(argv[1], NULL, 0);
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, window, &attrs) || attrs.width <= 0 ||
        attrs.height <= 0) {
        XCloseDisplay(display);
        return 1;
    }
    XColor exact;
    XColor screen;
    if (!XAllocNamedColor(display, attrs.colormap, argv[2], &screen, &exact)) {
        XCloseDisplay(display);
        return 2;
    }
    XImage *image = XGetImage(display, window, 0, 0, (unsigned int)attrs.width,
                              (unsigned int)attrs.height, AllPlanes, ZPixmap);
    if (!image) {
        XCloseDisplay(display);
        return 1;
    }

    int min_x = attrs.width;
    int min_y = attrs.height;
    int max_x = -1;
    int max_y = -1;
    unsigned long count = 0;
    for (int y = 0; y < attrs.height; ++y) {
        for (int x = 0; x < attrs.width; ++x) {
            if (XGetPixel(image, x, y) != screen.pixel) continue;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
            ++count;
        }
    }
    XDestroyImage(image);
    XCloseDisplay(display);
    if (!count) return 1;
    printf("%d %d %d %d %lu\n", min_x, min_y, max_x, max_y, count);
    return 0;
}
