#include <X11/Xlib.h>

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s OUTGOING_WINDOW INCOMING_WINDOW [FORBIDDEN_MAP_WINDOW]\n",
            program);
    exit(2);
}

static Window parse_window(const char *text)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 0);
    if (errno || !end || *end) return None;
    return (Window)value;
}

static long milliseconds_until(struct timespec deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    long seconds = deadline.tv_sec - now.tv_sec;
    long nanoseconds = deadline.tv_nsec - now.tv_nsec;
    long milliseconds = seconds * 1000 + nanoseconds / 1000000;
    return milliseconds > 0 ? milliseconds : 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) usage(argv[0]);
    Window outgoing = parse_window(argv[1]);
    Window incoming = parse_window(argv[2]);
    Window forbidden_map = argc == 4 ? parse_window(argv[3]) : None;
    if (outgoing == None || incoming == None ||
        (argc == 4 && forbidden_map == None))
        usage(argv[0]);

    Display *display = XOpenDisplay(NULL);
    if (!display) return 2;
    Window root = DefaultRootWindow(display);
    XSelectInput(display, root, SubstructureNotifyMask);
    XSelectInput(display, incoming, FocusChangeMask);
    XSync(display, False);
    puts("READY");
    fflush(stdout);

    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) return 2;
    deadline.tv_sec += 5;
    bool incoming_mapped = false;
    bool outgoing_unmapped = false;
    struct pollfd descriptor = {.fd = ConnectionNumber(display), .events = POLLIN};

    for (;;) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == MapNotify && forbidden_map != None &&
                event.xmap.window == forbidden_map) {
                puts("FORBIDDEN_MAPPED");
                fflush(stdout);
                XCloseDisplay(display);
                return 1;
            } else if (event.type == MapNotify && event.xmap.window == incoming) {
                incoming_mapped = true;
                puts("INCOMING_MAPPED");
                fflush(stdout);
            } else if (event.type == UnmapNotify &&
                       event.xunmap.window == outgoing) {
                if (!incoming_mapped) {
                    XCloseDisplay(display);
                    return 1;
                }
                outgoing_unmapped = true;
                puts("OUTGOING_UNMAPPED");
                fflush(stdout);
            } else if (event.type == FocusIn && event.xfocus.window == incoming) {
                puts("INCOMING_FOCUSED");
                fflush(stdout);
                XCloseDisplay(display);
                return outgoing_unmapped ? 0 : 1;
            }
        }

        long remaining = milliseconds_until(deadline);
        if (!remaining) {
            fprintf(stderr, "timed out waiting for workspace transition events\n");
            XCloseDisplay(display);
            return 2;
        }
        int result = poll(&descriptor, 1, (int)remaining);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            fprintf(stderr, "timed out waiting for workspace transition events\n");
            XCloseDisplay(display);
            return 2;
        }
    }
}
