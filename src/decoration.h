#ifndef BOX2430_DECORATION_H
#define BOX2430_DECORATION_H

#include "box2430.h"

bool client_should_decorate(const WM *wm, const Client *client);
Rect client_content_rect(const WM *wm, const Client *client, Rect outer);
Rect client_outer_rect(const WM *wm, const Client *client, Rect content);
Rect client_decoration_rect(const WM *wm, const Client *client);
Client *decoration_client_for_window(const WM *wm, Window window);
void decoration_reconcile(WM *wm, Client *client);
void decoration_draw(WM *wm, Client *client);
void decoration_destroy(WM *wm, Client *client);

#endif
