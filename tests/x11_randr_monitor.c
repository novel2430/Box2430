#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int x_error;

static int record_error(Display *display, XErrorEvent *event)
{
    (void)display;
    x_error = event->error_code;
    return 0;
}

static bool parse_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || !end || *end != '\0' || parsed < -32768 || parsed > 32767)
        return false;
    *value = (int)parsed;
    return true;
}

static RROutput find_output(Display *display, Window root, const char *name)
{
    XRRScreenResources *resources = XRRGetScreenResources(display, root);
    if (!resources) return None;
    RROutput result = None;
    for (int i = 0; i < resources->noutput; ++i) {
        XRROutputInfo *info = XRRGetOutputInfo(display, resources,
                                              resources->outputs[i]);
        if (info && strlen(name) == (size_t)info->nameLen &&
            memcmp(name, info->name, (size_t)info->nameLen) == 0) {
            result = resources->outputs[i];
        }
        if (info) XRRFreeOutputInfo(info);
        if (result != None) break;
    }
    XRRFreeScreenResources(resources);
    return result;
}

int main(int argc, char **argv)
{
    bool set_one = (argc == 8 || argc == 9) && strcmp(argv[1], "set") == 0 &&
        (argc != 9 || strcmp(argv[8], "hold") == 0);
    bool set_many = argc == 4 && strcmp(argv[1], "many") == 0 &&
        strcmp(argv[3], "hold") == 0;
    bool rename_one = (argc == 9 || argc == 10) &&
        strcmp(argv[1], "rename") == 0 &&
        (argc != 10 || strcmp(argv[9], "hold") == 0);
    bool delete_one = argc == 3 && strcmp(argv[1], "delete") == 0;
    bool notify_root = argc == 2 && strcmp(argv[1], "notify-root") == 0;
    if (!set_one && !set_many && !rename_one && !delete_one && !notify_root) {
        fprintf(stderr,
                "usage: %s set name x y width height output|none [hold]\n"
                "       %s many count hold\n"
                "       %s rename old-name new-name x y width height output|none [hold]\n"
                "       %s delete name\n"
                "       %s notify-root\n",
                argv[0], argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    int many_count = 0;
    if (set_many && (!parse_int(argv[2], &many_count) || many_count < 1)) {
        fputs("invalid monitor count\n", stderr);
        return 2;
    }
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int geometry_arg = rename_one ? 4 : 3;
    if ((set_one || rename_one) &&
        (!parse_int(argv[geometry_arg], &x) ||
         !parse_int(argv[geometry_arg + 1], &y) ||
         !parse_int(argv[geometry_arg + 2], &width) ||
         !parse_int(argv[geometry_arg + 3], &height) ||
         width < 1 || height < 1)) {
        fputs("invalid monitor geometry\n", stderr);
        return 2;
    }
    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;
    Window root = DefaultRootWindow(display);

    XSetErrorHandler(record_error);
    if (delete_one) {
        Atom name = XInternAtom(display, argv[2], True);
        if (name == None) {
            fprintf(stderr, "cannot find logical monitor %s\n", argv[2]);
            XCloseDisplay(display);
            return 1;
        }
        XRRDeleteMonitor(display, root, name);
        XSync(display, False);
        if (x_error)
            fprintf(stderr, "RandR DeleteMonitor failed with X error %d\n",
                    x_error);
        XCloseDisplay(display);
        return x_error ? 1 : 0;
    }
    if (notify_root) {
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display, root, &attributes)) {
            XCloseDisplay(display);
            return 1;
        }
        XEvent event = {0};
        event.xconfigure.type = ConfigureNotify;
        event.xconfigure.display = display;
        event.xconfigure.event = root;
        event.xconfigure.window = root;
        event.xconfigure.x = attributes.x;
        event.xconfigure.y = attributes.y;
        event.xconfigure.width = attributes.width;
        event.xconfigure.height = attributes.height;
        event.xconfigure.border_width = attributes.border_width;
        event.xconfigure.above = None;
        event.xconfigure.override_redirect = attributes.override_redirect;
        if (!XSendEvent(display, root, False, StructureNotifyMask, &event)) {
            fputs("cannot send root ConfigureNotify\n", stderr);
            XCloseDisplay(display);
            return 1;
        }
        XSync(display, False);
        XCloseDisplay(display);
        return 0;
    }

    RROutput output = None;
    int output_count = 0;
    const char *output_name = rename_one ? argv[8] :
        set_one ? argv[7] : NULL;
    if (output_name && strcmp(output_name, "none") != 0) {
        output = find_output(display, root, output_name);
        if (output == None) {
            fprintf(stderr, "cannot find output %s\n", output_name);
            XCloseDisplay(display);
            return 1;
        }
        output_count = 1;
    }

    bool server_grabbed = false;
    if (rename_one || set_many) {
        XGrabServer(display);
        server_grabbed = true;
    }
    if (rename_one) {
        Atom old_name = XInternAtom(display, argv[2], True);
        if (old_name == None) {
            XUngrabServer(display);
            fprintf(stderr, "cannot find logical monitor %s\n", argv[2]);
            XCloseDisplay(display);
            return 1;
        }
        XRRDeleteMonitor(display, root, old_name);
    }

    int count = set_many ? many_count : 1;
    for (int i = 0; i < count; ++i) {
        XRRMonitorInfo *monitor = XRRAllocateMonitor(
            display, set_many ? 0 : output_count);
        if (!monitor) {
            XCloseDisplay(display);
            return 1;
        }
        char generated_name[64];
        const char *name = rename_one ? argv[3] : argv[2];
        if (set_many) {
            snprintf(generated_name, sizeof(generated_name),
                     "box2430-test-monitor-%d", i);
            name = generated_name;
        }
        monitor->name = XInternAtom(display, name, False);
        monitor->x = set_many ? 0 : x;
        monitor->y = set_many ? 0 : y;
        monitor->width = set_many ? 1 : width;
        monitor->height = set_many ? 1 : height;
        if (!set_many && output_count) monitor->outputs[0] = output;
        XRRSetMonitor(display, root, monitor);
        XRRFreeMonitors(monitor);
    }
    if (server_grabbed) XUngrabServer(display);
    XSync(display, False);
    if (x_error) {
        fprintf(stderr, "RandR SetMonitor failed with X error %d\n", x_error);
        XCloseDisplay(display);
        return 1;
    }
    if (set_many || (set_one && argc == 9) ||
        (rename_one && argc == 10)) {
        for (;;) pause();
    }
    XCloseDisplay(display);
    return 0;
}
