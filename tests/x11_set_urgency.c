#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Window window = (Window)strtoul(argv[1], NULL, 0);
    XWMHints *hints = XGetWMHints(display, window);
    if (!hints) hints = XAllocWMHints();
    if (!hints) { XCloseDisplay(display); return 1; }
    if (atoi(argv[2])) hints->flags |= XUrgencyHint;
    else hints->flags &= ~XUrgencyHint;
    XSetWMHints(display, window, hints);
    XFree(hints);
    XSync(display, False);
    XCloseDisplay(display);
    return 0;
}
