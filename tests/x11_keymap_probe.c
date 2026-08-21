#include <X11/Xlib.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool grab_failed;

static int grab_error_handler(Display *display, XErrorEvent *event)
{
    (void)display;
    if (event->error_code == BadAccess) grab_failed = true;
    return 0;
}

static bool keycode_is_modifier(Display *display, KeyCode keycode)
{
    XModifierKeymap *map = XGetModifierMapping(display);
    if (!map) return false;
    bool found = false;
    for (int modifier = 0; modifier < 8 && !found; ++modifier) {
        for (int key = 0; key < map->max_keypermod; ++key) {
            if (map->modifiermap[modifier * map->max_keypermod + key] == keycode) {
                found = true;
                break;
            }
        }
    }
    XFreeModifiermap(map);
    return found;
}

static int add_duplicate_mapping(Display *display, const char *name)
{
    KeySym symbol = XStringToKeysym(name);
    if (symbol == NoSymbol) return 2;

    int first = 0;
    int last = 0;
    int per_keycode = 0;
    XDisplayKeycodes(display, &first, &last);
    KeySym *mapping = XGetKeyboardMapping(
        display, (KeyCode)first, last - first + 1, &per_keycode);
    if (!mapping || per_keycode <= 0) {
        if (mapping) XFree(mapping);
        return 1;
    }

    KeyCode chosen = 0;
    for (int keycode = last; keycode >= first; --keycode) {
        bool empty = true;
        for (int slot = 0; slot < per_keycode; ++slot) {
            if (mapping[(keycode - first) * per_keycode + slot] != NoSymbol) {
                empty = false;
                break;
            }
        }
        if (empty && !keycode_is_modifier(display, (KeyCode)keycode)) {
            chosen = (KeyCode)keycode;
            break;
        }
    }
    XFree(mapping);
    if (!chosen) return 1;

    KeySym *replacement = calloc((size_t)per_keycode, sizeof(*replacement));
    if (!replacement) return 1;
    replacement[0] = symbol;
    XChangeKeyboardMapping(display, chosen, per_keycode, replacement, 1);
    free(replacement);
    XSync(display, False);
    printf("%u\n", (unsigned int)chosen);
    return 0;
}

static int probe_grab(Display *display, const char *keycode_text,
                      const char *modifier_text)
{
    char *end = NULL;
    unsigned long keycode = strtoul(keycode_text, &end, 0);
    if (!end || *end != '\0' || keycode > 255) return 2;
    end = NULL;
    unsigned long modifiers = strtoul(modifier_text, &end, 0);
    if (!end || *end != '\0') return 2;

    grab_failed = false;
    XErrorHandler previous = XSetErrorHandler(grab_error_handler);
    XGrabKey(display, (int)keycode, (unsigned int)modifiers,
             DefaultRootWindow(display), True,
             GrabModeAsync, GrabModeAsync);
    XSync(display, False);
    if (!grab_failed) {
        XUngrabKey(display, (int)keycode, (unsigned int)modifiers,
                   DefaultRootWindow(display));
        XSync(display, False);
    }
    XSetErrorHandler(previous);

    /* Success means another client already owns this exact passive grab. */
    return grab_failed ? 0 : 1;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s add-duplicate KEYSYM\n", program);
    fprintf(stderr, "       %s probe-grab KEYCODE MODIFIERS\n", program);
}

int main(int argc, char **argv)
{
    if ((argc != 3 || strcmp(argv[1], "add-duplicate") != 0) &&
        (argc != 4 || strcmp(argv[1], "probe-grab") != 0)) {
        usage(argv[0]);
        return 2;
    }

    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    int result;
    if (strcmp(argv[1], "add-duplicate") == 0)
        result = add_duplicate_mapping(display, argv[2]);
    else
        result = probe_grab(display, argv[2], argv[3]);
    XCloseDisplay(display);
    return result;
}
