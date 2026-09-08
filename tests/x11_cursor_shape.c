#include <X11/Xlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/extensions/Xfixes.h>
#include <stdio.h>

/* Compare the actual server cursor with a named image from the selected theme. */
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    XFixesCursorImage *actual = XFixesGetCursorImage(display);
    XcursorImage *expected = XcursorLibraryLoadImage(argv[1], XcursorGetTheme(display),
                                                    XcursorGetDefaultSize(display));
    if (!actual || !expected) return 1;
    int matches = actual->width == expected->width && actual->height == expected->height &&
                  actual->xhot == expected->xhot && actual->yhot == expected->yhot;
    for (unsigned int i = 0; matches && i < expected->width * expected->height; ++i)
        if ((unsigned int)actual->pixels[i] != expected->pixels[i]) matches = 0;
    XFree(actual);
    XcursorImageDestroy(expected);
    XCloseDisplay(display);
    return matches ? 0 : 1;
}
