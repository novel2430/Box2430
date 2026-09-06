#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 6) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Window window = (Window)strtoul(argv[1], NULL, 0);
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, window, &attrs) || attrs.width <= 0 ||
        attrs.height <= 0) {
        XCloseDisplay(display);
        return 1;
    }
    int x_offset = 0, y_offset = 0;
    if (argc == 6) {
        x_offset = atoi(argv[2]); y_offset = atoi(argv[3]);
        int width = atoi(argv[4]), height = atoi(argv[5]);
        if (x_offset < 0 || y_offset < 0 || width <= 0 || height <= 0 ||
            width > attrs.width - x_offset || height > attrs.height - y_offset) return 2;
        attrs.width = width; attrs.height = height;
    }
    XImage *image = XGetImage(display, window, x_offset, y_offset, (unsigned int)attrs.width,
                              (unsigned int)attrs.height, AllPlanes, ZPixmap);
    if (!image) {
        XCloseDisplay(display);
        return 1;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int y = 0; y < attrs.height; ++y) {
        for (int x = 0; x < attrs.width; ++x) {
            unsigned long pixel = XGetPixel(image, x, y);
            for (unsigned int byte = 0; byte < sizeof(pixel); ++byte) {
                hash ^= (pixel >> (byte * 8U)) & 0xffU;
                hash *= UINT64_C(1099511628211);
            }
        }
    }
    XDestroyImage(image);
    XCloseDisplay(display);
    printf("%016llx\n", (unsigned long long)hash);
    return 0;
}
