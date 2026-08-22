#ifndef BOX2430_UI_H
#define BOX2430_UI_H

#include "box2430.h"

bool ui_init(WM *wm);
void ui_destroy(WM *wm);
void ui_update(WM *wm);
bool ui_is_internal_window(const WM *wm, Window window);
void ui_snap_preview_show(WM *wm, Rect outer);
void ui_snap_preview_hide(WM *wm);

UIStyle ui_resolve_style(UIStyle base, const UIStyleOverride *override);
const char *ui_client_label(const Client *client, UILabelSource source);
char *ui_format_label(const UILabelFormat *format, const char *value);

unsigned int ui_text_width(Display *display, XftFont *const *fonts,
                           unsigned int font_count, const char *text);
void ui_draw_text(Display *display, XftDraw *draw, const XftColor *color,
                  XftFont *const *fonts, unsigned int font_count,
                  int x, int y, unsigned int width, unsigned int height,
                  unsigned int padding, const char *text);

bool ui_bar_create_monitor(WM *wm, Monitor *monitor);
void ui_bar_destroy_monitor(WM *wm, Monitor *monitor);
void ui_bar_name_monitor(WM *wm, Monitor *monitor);
void ui_bar_update(WM *wm);
void ui_bar_draw(WM *wm, Monitor *monitor);
void ui_status_refresh(WM *wm);
bool ui_clock_visible(const WM *wm);
void ui_clock_tick(WM *wm);
void ui_client_border_refresh(WM *wm, Client *client);
Monitor *ui_bar_monitor_for_window(WM *wm, Window window);
Workspace *ui_bar_workspace_hit_test(WM *wm, Monitor *monitor, int x);
UIWorkspaceVisualState ui_workspace_visual_state(const Monitor *monitor,
                                                 const Workspace *workspace);

bool ui_tabs_should_materialize(const WM *wm, const Workspace *workspace);
unsigned int ui_tab_height(const WM *wm, const Monitor *monitor);
bool ui_tab_create_monitor(WM *wm, Monitor *monitor);
void ui_tab_destroy_monitor(WM *wm, Monitor *monitor);
void ui_tab_name_monitor(WM *wm, Monitor *monitor);
void ui_tab_update(WM *wm);
void ui_tab_draw(WM *wm, Monitor *monitor);
Monitor *ui_tab_monitor_for_window(WM *wm, Window window);
Client *ui_tab_hit_test(WM *wm, Monitor *monitor, int x);

#endif
