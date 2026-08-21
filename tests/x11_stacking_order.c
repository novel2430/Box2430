#include <X11/Xlib.h>

#include <stdio.h>
#include <stdlib.h>

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s LOWER_WINDOW UPPER_WINDOW\n", program);
    exit(2);
}

int main(int argc, char **argv)
{
    if (argc != 3) usage(argv[0]);
    char *end = NULL;
    Window lower = (Window)strtoul(argv[1], &end, 0);
    if (!end || *end) usage(argv[0]);
    Window upper = (Window)strtoul(argv[2], &end, 0);
    if (!end || *end) usage(argv[0]);

    Display *display = XOpenDisplay(NULL);
    if (!display) return 2;
    Window root = DefaultRootWindow(display);
    Window root_return;
    Window parent_return;
    Window *children = NULL;
    unsigned int count = 0;
    if (!XQueryTree(display, root, &root_return, &parent_return,
                    &children, &count)) {
        XCloseDisplay(display);
        return 2;
    }

    /* XQueryTree returns root children from bottom-most to top-most. */
    int lower_index = -1;
    int upper_index = -1;
    for (unsigned int i = 0; i < count; ++i) {
        if (children[i] == lower) lower_index = (int)i;
        if (children[i] == upper) upper_index = (int)i;
    }
    if (children) XFree(children);
    XCloseDisplay(display);
    if (lower_index < 0 || upper_index < 0) return 2;
    return lower_index < upper_index ? 0 : 1;
}
