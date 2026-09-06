#ifndef BOX2430_DECORATION_H
#define BOX2430_DECORATION_H

#include "box2430.h"

enum {
    DECORATION_DRAG_THRESHOLD = 4,
    DECORATION_DOUBLE_CLICK_MS = 300,
};

typedef struct DecorationLayout {
    Rect bounds;
    Rect items[DECORATION_ITEM_COUNT]; /* Local coordinates; absent items have zero width. */
} DecorationLayout;

DecorationLayout decoration_layout_calculate(const DecorationConfig *config,
    int width, const unsigned int preferred[DECORATION_ITEM_COUNT]);
DecorationLayout decoration_layout(const WM *wm, const Client *client);
DecorationPart decoration_hit_test(const DecorationLayout *layout, int x, int y);
DecorationPart decoration_part_at(const WM *wm, const Client *client, int root_x, int root_y);
void decoration_pointer_cursor(WM *wm, Client *client, int root_x, int root_y);

bool decoration_drag_threshold_reached(int press_x, int press_y, int x, int y);
bool decoration_is_double_click(const DecorationInputState *input, Time time);
DecorationAction decoration_click_action(const DecorationBindings *bindings,
                                          unsigned int button, bool double_click);
void decoration_input_cancel(WM *wm, Client *client);

bool client_should_decorate(const WM *wm, const Client *client);
Rect client_content_rect(const WM *wm, const Client *client, Rect outer);
Rect client_outer_rect(const WM *wm, const Client *client, Rect content);
Rect client_decoration_rect(const WM *wm, const Client *client);
Client *decoration_client_for_window(const WM *wm, Window window);
void decoration_reconcile(WM *wm, Client *client);
void decoration_draw(WM *wm, Client *client);
void decoration_destroy(WM *wm, Client *client);

#endif
