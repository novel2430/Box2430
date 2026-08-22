#ifndef BOX2430_TRAY_H
#define BOX2430_TRAY_H

#include "box2430.h"

typedef enum TrayEventResult {
    TRAY_EVENT_NONE = 0,
    TRAY_EVENT_CONSUMED = 1U << 0,
    TRAY_EVENT_CHANGED = 1U << 1,
} TrayEventResult;

bool tray_init(WM *wm);
void tray_destroy(WM *wm);
TrayEventResult tray_handle_event(WM *wm, XEvent *event);
void tray_prepare_layout(WM *wm, const Monitor *monitor);
unsigned int tray_widget_width(const WM *wm, const Monitor *monitor);
void tray_set_allocation(WM *wm, const Monitor *monitor, Rect rect);
Window tray_host_window(const WM *wm);

#endif
