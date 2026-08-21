#ifndef BOX2430_UI_H
#define BOX2430_UI_H

#include "box2430.h"

bool ui_init(WM *wm);
void ui_destroy(WM *wm);

unsigned int ui_text_width(Display *display, XftFont *const *fonts,
                           unsigned int font_count, const char *text);
void ui_draw_text(Display *display, XftDraw *draw, const XftColor *color,
                  XftFont *const *fonts, unsigned int font_count,
                  int x, int y, unsigned int width, unsigned int height,
                  unsigned int padding, const char *text);

unsigned int ui_tab_height(const WM *wm, const Monitor *monitor);
bool ui_tab_create_monitor(WM *wm, Monitor *monitor);
void ui_tab_destroy_monitor(WM *wm, Monitor *monitor);
void ui_tab_name_monitor(WM *wm, Monitor *monitor);
void ui_tab_update(WM *wm);
void ui_tab_draw(WM *wm, Monitor *monitor);
Monitor *ui_tab_monitor_for_window(WM *wm, Window window);
Client *ui_tab_hit_test(WM *wm, Monitor *monitor, int x);

#endif
