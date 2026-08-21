#include <X11/Xlib.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s WINDOW MASK X Y WIDTH HEIGHT BORDER\n"
            "MASK uses any of: x y w h b\n",
            program);
    exit(2);
}

static unsigned int parse_mask(const char *text)
{
    unsigned int mask = 0;
    for (const char *p = text; *p; ++p) {
        switch (*p) {
        case 'x': mask |= CWX; break;
        case 'y': mask |= CWY; break;
        case 'w': mask |= CWWidth; break;
        case 'h': mask |= CWHeight; break;
        case 'b': mask |= CWBorderWidth; break;
        default: return 0;
        }
    }
    return mask;
}

int main(int argc, char **argv)
{
    if (argc != 8) usage(argv[0]);

    char *end = NULL;
    Window window = (Window)strtoul(argv[1], &end, 0);
    if (!end || *end != '\0') usage(argv[0]);
    unsigned int mask = parse_mask(argv[2]);
    if (mask == 0) usage(argv[0]);

    Display *display = XOpenDisplay(NULL);
    if (!display) return 1;

    XWindowAttributes attrs;
    if (!XGetWindowAttributes(display, window, &attrs)) {
        XCloseDisplay(display);
        return 1;
    }

    XSelectInput(display, window, attrs.your_event_mask | StructureNotifyMask);
    XSync(display, False);

    XWindowChanges changes = {
        .x = atoi(argv[3]),
        .y = atoi(argv[4]),
        .width = atoi(argv[5]),
        .height = atoi(argv[6]),
        .border_width = atoi(argv[7]),
    };
    XConfigureWindow(display, window, mask, &changes);
    XFlush(display);

    struct pollfd pfd = {
        .fd = ConnectionNumber(display),
        .events = POLLIN,
    };
    for (;;) {
        while (XPending(display)) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type != ConfigureNotify ||
                event.xconfigure.window != window) continue;
            printf("%d %d %d %d %d %d\n",
                   event.xconfigure.x, event.xconfigure.y,
                   event.xconfigure.width, event.xconfigure.height,
                   event.xconfigure.border_width,
                   event.xconfigure.send_event ? 1 : 0);
            XCloseDisplay(display);
            return 0;
        }
        int status = poll(&pfd, 1, 2000);
        if (status <= 0) {
            fprintf(stderr, "timed out waiting for ConfigureNotify\n");
            XCloseDisplay(display);
            return 1;
        }
    }
}
