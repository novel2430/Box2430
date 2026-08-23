#include "box2430.h"

#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fputs("FAIL: cannot open test display\n", stderr);
        return 1;
    }
    WM wm = {
        .display = display,
        .screen = DefaultScreen(display),
    };
    wm.root = RootWindow(display, wm.screen);
    if (!randr_check_version(&wm)) {
        XCloseDisplay(display);
        return 1;
    }

    RandRMonitorSnapshot snapshot = {0};
    if (!randr_query_monitor_snapshot(&wm, &snapshot)) {
        XCloseDisplay(display);
        return 1;
    }
    if (snapshot.count != 1 || snapshot.monitors[0].synthetic) {
        fprintf(stderr,
                "FAIL: controlled Xvfb returned %u observed monitors\n",
                snapshot.count);
        randr_free_monitor_snapshot(&snapshot);
        XCloseDisplay(display);
        return 1;
    }

    int direct_count = -1;
    XRRMonitorInfo *direct = XRRGetMonitors(display, wm.root, True,
                                            &direct_count);
    if (!direct || direct_count != 1 ||
        snapshot.monitors[0].geometry.x != direct[0].x ||
        snapshot.monitors[0].geometry.y != direct[0].y ||
        snapshot.monitors[0].geometry.width != direct[0].width ||
        snapshot.monitors[0].geometry.height != direct[0].height) {
        fputs("FAIL: owned snapshot geometry differs from RandR monitor\n",
              stderr);
        if (direct) XRRFreeMonitors(direct);
        randr_free_monitor_snapshot(&snapshot);
        XCloseDisplay(display);
        return 1;
    }
    XRRFreeMonitors(direct);

    if (!snapshot.monitors[0].name_string ||
        snapshot.monitors[0].name_string[0] == '\0') {
        fputs("FAIL: logical monitor name was not retained\n", stderr);
        randr_free_monitor_snapshot(&snapshot);
        XCloseDisplay(display);
        return 1;
    }
    for (unsigned int i = 0; i < snapshot.monitors[0].output_count; ++i) {
        if (!snapshot.monitors[0].outputs[i].name ||
            snapshot.monitors[0].outputs[i].name[0] == '\0') {
            fputs("FAIL: output connector name was not retained\n", stderr);
            randr_free_monitor_snapshot(&snapshot);
            XCloseDisplay(display);
            return 1;
        }
    }

    RandRMonitorSnapshot second = {0};
    if (!randr_query_monitor_snapshot(&wm, &second) ||
        !randr_monitor_snapshots_equal(&snapshot, &second)) {
        fputs("FAIL: repeated RandR snapshot changed unexpectedly\n", stderr);
        randr_free_monitor_snapshot(&second);
        randr_free_monitor_snapshot(&snapshot);
        XCloseDisplay(display);
        return 1;
    }

    randr_free_monitor_snapshot(&second);
    randr_free_monitor_snapshot(&snapshot);
    XCloseDisplay(display);
    puts("PASS: owned RandR 1.5 logical-monitor snapshot");
    return 0;
}
