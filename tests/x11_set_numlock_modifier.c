#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    int modifier = atoi(argv[1]);
    if (modifier < 0 || modifier > 7) return 2;
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    KeyCode numlock = XKeysymToKeycode(display, XK_Num_Lock);
    XModifierKeymap *map = XGetModifierMapping(display);
    if (!numlock || !map) {
        if (map) XFreeModifiermap(map);
        XCloseDisplay(display);
        return 1;
    }
    for (int i = 0; i < 8; ++i)
        map = XDeleteModifiermapEntry(map, numlock, i);
    map = XInsertModifiermapEntry(map, numlock, modifier);
    int result = map ? XSetModifierMapping(display, map) : MappingFailed;
    if (map) XFreeModifiermap(map);
    XSync(display, False);
    XCloseDisplay(display);
    return result == MappingSuccess ? 0 : 1;
}
