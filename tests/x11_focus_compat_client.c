#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

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

static void set_count(Display *display, Window window, Atom property,
                      unsigned long count)
{
    XChangeProperty(display, window, property, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&count, 1);
}

static Window create_client(Display *display, int screen, const char *title,
                            int x)
{
    Window window = XCreateSimpleWindow(
        display, RootWindow(display, screen), x, 80, 240, 140, 1,
        BlackPixel(display, screen), WhitePixel(display, screen));
    XStoreName(display, window, title);
    XClassHint class_hint = {
        .res_name = (char *)title,
        .res_class = "FocusCompatClient",
    };
    XSetClassHint(display, window, &class_hint);
    XWMHints hints = {
        .flags = InputHint,
        .input = True,
    };
    XSetWMHints(display, window, &hints);
    XSelectInput(display, window, StructureNotifyMask | ButtonPressMask);
    return window;
}

static int run_pair(Display *display)
{
    int screen = DefaultScreen(display);
    Window windows[2] = {
        create_client(display, screen, "FocusCompatA", 40),
        create_client(display, screen, "FocusCompatB", 400),
    };
    Atom count_property = XInternAtom(display, "_BOX2430_TEST_BUTTON_COUNT", False);
    unsigned long counts[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        set_count(display, windows[i], count_property, counts[i]);
        XMapWindow(display, windows[i]);
    }
    printf("0x%lx 0x%lx\n", windows[0], windows[1]);
    fflush(stdout);
    XFlush(display);

    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == ButtonPress) {
            int index = event.xbutton.window == windows[1] ? 1 : 0;
            ++counts[index];
            set_count(display, windows[index], count_property, counts[index]);
            XFlush(display);
        } else if (event.type == DestroyNotify) {
            return 0;
        }
    }
}

static int set_focus(Display *display, Window window)
{
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
    XSync(display, False);
    return 0;
}

static int request_active(Display *display, Window window)
{
    Window root = DefaultRootWindow(display);
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1;
    event.xclient.data.l[1] = CurrentTime;
    XSendEvent(display, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XSync(display, False);
    return 0;
}

static int probe_button_grab(Display *display, Window window)
{
    grab_failed = false;
    XErrorHandler previous = XSetErrorHandler(grab_error_handler);
    XGrabButton(display, Button1, 0, window, False, ButtonPressMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XSync(display, False);
    if (!grab_failed) {
        XUngrabButton(display, Button1, 0, window);
        XSync(display, False);
    }
    XSetErrorHandler(previous);
    return grab_failed ? 1 : 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s pair\n", program);
    fprintf(stderr, "       %s focus|activate|probe-grab WINDOW\n", program);
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        usage(argv[0]);
        return 2;
    }
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    int result;
    if (argc == 2 && strcmp(argv[1], "pair") == 0) {
        result = run_pair(display);
    } else if (argc == 3) {
        Window window = (Window)strtoul(argv[2], NULL, 0);
        if (strcmp(argv[1], "focus") == 0) result = set_focus(display, window);
        else if (strcmp(argv[1], "activate") == 0)
            result = request_active(display, window);
        else if (strcmp(argv[1], "probe-grab") == 0)
            result = probe_button_grab(display, window);
        else { usage(argv[0]); result = 2; }
    } else {
        usage(argv[0]);
        result = 2;
    }
    XCloseDisplay(display);
    return result;
}
