#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_window(Display *display, const char *text, Window *window)
{
    if (strcmp(text, "root") == 0) {
        *window = RootWindow(display, DefaultScreen(display));
        return true;
    }
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (!end || *end != '\0') return false;
    *window = (Window)value;
    return true;
}

static int set_input_hint(Display *display, Window window, bool accepts_input)
{
    XWMHints *hints = XGetWMHints(display, window);
    if (!hints) hints = XAllocWMHints();
    if (!hints) return 1;
    hints->flags |= InputHint;
    hints->input = accepts_input ? True : False;
    XSetWMHints(display, window, hints);
    XFree(hints);
    return 0;
}

static int set_protocols(Display *display, Window window, bool takes_focus)
{
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    Atom take_focus = XInternAtom(display, "WM_TAKE_FOCUS", False);
    Atom protocols[2] = {delete_window, take_focus};
    XSetWMProtocols(display, window, protocols, takes_focus ? 2 : 1);
    return 0;
}

static int set_transient(Display *display, Window window, const char *parent_text)
{
    if (strcmp(parent_text, "none") == 0) {
        XDeleteProperty(display, window, XA_WM_TRANSIENT_FOR);
        return 0;
    }
    Window parent;
    if (!parse_window(display, parent_text, &parent)) return 2;
    XSetTransientForHint(display, window, parent);
    return 0;
}

static int set_window_type(Display *display, Window window, const char *type)
{
    Atom property = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    if (strcmp(type, "none") == 0) {
        XDeleteProperty(display, window, property);
        return 0;
    }

    const char *suffix = NULL;
    if (strcmp(type, "normal") == 0) suffix = "NORMAL";
    else if (strcmp(type, "dialog") == 0) suffix = "DIALOG";
    else if (strcmp(type, "dock") == 0) suffix = "DOCK";
    else if (strcmp(type, "desktop") == 0) suffix = "DESKTOP";
    else if (strcmp(type, "notification") == 0) suffix = "NOTIFICATION";
    else return 2;

    char name[64];
    snprintf(name, sizeof(name), "_NET_WM_WINDOW_TYPE_%s", suffix);
    Atom value = XInternAtom(display, name, False);
    XChangeProperty(display, window, property, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&value, 1);
    return 0;
}

static int set_name(Display *display, Window window, bool net_name,
                    const char *text)
{
    Atom property = net_name ? XInternAtom(display, "_NET_WM_NAME", False)
                             : XA_WM_NAME;
    if (strcmp(text, "none") == 0) {
        XDeleteProperty(display, window, property);
        return 0;
    }
    if (net_name) {
        Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
        XChangeProperty(display, window, property, utf8, 8, PropModeReplace,
                        (const unsigned char *)text, (int)strlen(text));
    } else {
        XChangeProperty(display, window, property, XA_STRING, 8, PropModeReplace,
                        (const unsigned char *)text, (int)strlen(text));
    }
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s input WINDOW true|false\n", program);
    fprintf(stderr, "       %s take-focus WINDOW true|false\n", program);
    fprintf(stderr, "       %s transient WINDOW PARENT|none\n", program);
    fprintf(stderr, "       %s type WINDOW normal|dialog|dock|desktop|notification|none\n",
            program);
    fprintf(stderr, "       %s wm-name WINDOW|root TEXT|none\n", program);
    fprintf(stderr, "       %s net-name WINDOW|root TEXT|none\n", program);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        usage(argv[0]);
        return 2;
    }
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Window window;
    if (!parse_window(display, argv[2], &window)) {
        XCloseDisplay(display);
        return 2;
    }
    int result = 2;
    if (strcmp(argv[1], "input") == 0) {
        if (strcmp(argv[3], "true") == 0) result = set_input_hint(display, window, true);
        else if (strcmp(argv[3], "false") == 0)
            result = set_input_hint(display, window, false);
    } else if (strcmp(argv[1], "take-focus") == 0) {
        if (strcmp(argv[3], "true") == 0) result = set_protocols(display, window, true);
        else if (strcmp(argv[3], "false") == 0)
            result = set_protocols(display, window, false);
    } else if (strcmp(argv[1], "transient") == 0) {
        result = set_transient(display, window, argv[3]);
    } else if (strcmp(argv[1], "type") == 0) {
        result = set_window_type(display, window, argv[3]);
    } else if (strcmp(argv[1], "wm-name") == 0) {
        result = set_name(display, window, false, argv[3]);
    } else if (strcmp(argv[1], "net-name") == 0) {
        result = set_name(display, window, true, argv[3]);
    }
    if (result == 2) usage(argv[0]);
    XSync(display, False);
    XCloseDisplay(display);
    return result;
}
