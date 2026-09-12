#include "decoration.h"
#include "ui.h"

#include <stdio.h>
#include <stdint.h>

DecorationLayout decoration_layout_calculate(const DecorationConfig *config,
    int width, const unsigned int preferred[DECORATION_ITEM_COUNT])
{
    DecorationLayout layout = {.bounds = {0, 0, width > 0 ? width : 0,
                                          (int)config->height}};
    unsigned int remaining = (unsigned int)layout.bounds.width;
    unsigned int widths[DECORATION_ITEM_COUNT] = {0};
    bool title = false, space = false;
    /* Fixed buttons get first claim, in configured order. Under extreme
     * pressure later buttons are clipped to the remaining width (or omitted). */
    for (unsigned int i = 0; i < config->layout_count; ++i) {
        DecorationItem item = config->layout[i];
        if (item == DECORATION_ITEM_TITLE) title = true;
        else if (item == DECORATION_ITEM_SPACE) space = true;
        else {
            widths[item] = preferred[item] < remaining ? preferred[item] : remaining;
            remaining -= widths[item];
        }
    }
    if (title) {
        widths[DECORATION_ITEM_TITLE] = !space || preferred[DECORATION_ITEM_TITLE] > remaining
            ? remaining : preferred[DECORATION_ITEM_TITLE];
        remaining -= widths[DECORATION_ITEM_TITLE];
    }
    if (space) widths[DECORATION_ITEM_SPACE] = remaining;
    int x = 0;
    for (unsigned int i = 0; i < config->layout_count; ++i) {
        DecorationItem item = config->layout[i];
        layout.items[item] = (Rect){x, 0, (int)widths[item], (int)config->height};
        x += (int)widths[item];
    }
    return layout;
}

static unsigned int label_width(const WM *wm, const char *label)
{
    unsigned int width = ui_text_width(wm->display, wm->decoration_fonts,
                                       wm->decoration_font_count, label);
    unsigned int padding = 2 * wm->config.decoration.padding;
    return width > INT32_MAX - padding ? INT32_MAX : width + padding;
}

DecorationLayout decoration_layout(const WM *wm, const Client *client)
{
    const DecorationConfig *config = &wm->config.decoration;
    unsigned int preferred[DECORATION_ITEM_COUNT] = {
        [DECORATION_ITEM_TITLE] = label_width(wm, ui_client_label(client, UI_LABEL_TITLE)),
        [DECORATION_ITEM_MAXIMIZE] = config->height,
        [DECORATION_ITEM_CLOSE] = config->height,
    };
    const char *labels[] = {config->maximize_label, config->restore_label, config->close_label};
    for (unsigned int i = 0; i < 3; ++i) {
        DecorationItem item = i == 2 ? DECORATION_ITEM_CLOSE : DECORATION_ITEM_MAXIMIZE;
        unsigned int width = labels[i][0] ? label_width(wm, labels[i]) : 0;
        if (width > preferred[item]) preferred[item] = width;
    }
    /* Reserve both maximize labels so a state change never shifts the buttons. */
    return decoration_layout_calculate(config, client_decoration_rect(wm, client).width,
                                        preferred);
}

static bool contains(Rect rect, int x, int y)
{
    return x >= rect.x && y >= rect.y && x - rect.x < rect.width && y - rect.y < rect.height;
}

DecorationPart decoration_hit_test(const DecorationLayout *layout, int x, int y)
{
    if (!contains(layout->bounds, x, y)) return DECORATION_PART_NONE;
    if (contains(layout->items[DECORATION_ITEM_MAXIMIZE], x, y)) return DECORATION_PART_MAXIMIZE;
    if (contains(layout->items[DECORATION_ITEM_CLOSE], x, y)) return DECORATION_PART_CLOSE;
    return DECORATION_PART_TITLE;
}

DecorationPart decoration_part_at(const WM *wm, const Client *client, int root_x, int root_y)
{
    if (!x11_client_const(client)->decoration_mapped) return DECORATION_PART_NONE;
    Rect rect = client_decoration_rect(wm, client);
    DecorationLayout layout = decoration_layout(wm, client);
    return decoration_hit_test(&layout, root_x - rect.x, root_y - rect.y);
}

void decoration_pointer_cursor(WM *wm, Client *client, int root_x, int root_y)
{
    DecorationPart part = decoration_part_at(wm, client, root_x, root_y);
    Cursor cursor = part == DECORATION_PART_MAXIMIZE || part == DECORATION_PART_CLOSE
        ? wm->cursor_pointer : wm->cursor_normal;
    XDefineCursor(wm->display, x11_client(client)->decoration, cursor);
    if (wm->decoration_input.client == client && !wm->decoration_input.dragging)
        XChangeActivePointerGrab(wm->display,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask, cursor, CurrentTime);
}

bool decoration_drag_threshold_reached(int press_x, int press_y, int x, int y)
{
    int64_t dx = (int64_t)x - press_x, dy = (int64_t)y - press_y;
    return dx >= DECORATION_DRAG_THRESHOLD || dx <= -DECORATION_DRAG_THRESHOLD ||
           dy >= DECORATION_DRAG_THRESHOLD || dy <= -DECORATION_DRAG_THRESHOLD;
}

bool decoration_is_double_click(const DecorationInputState *input, Time time)
{
    /* X timestamps wrap at 32 bits, including on LP64 Xlib. */
    return input->client && input->part == DECORATION_PART_TITLE &&
           input->button == Button1 && !input->dragging &&
           input->client == input->last_client &&
           input->titlebar == input->last_titlebar &&
           (uint32_t)(time - input->last_time) <= DECORATION_DOUBLE_CLICK_MS &&
           !decoration_drag_threshold_reached(input->last_x, input->last_y,
                                              input->press_x, input->press_y);
}

DecorationAction decoration_click_action(const DecorationBindings *bindings,
                                          unsigned int button, bool double_click)
{
    switch (button) {
    case Button1: return double_click ? bindings->double_click : bindings->click;
    case Button2: return bindings->middle_click;
    case Button3: return bindings->right_click;
    default: return DECORATION_ACTION_NONE;
    }
}

bool client_should_decorate(const WM *wm, const Client *client)
{
    if (!wm->config.decoration.enabled || !client || !client->workspace ||
        client->workspace->mode != WORKSPACE_FREE || client->fullscreen)
        return false;
    if (client->decoration_policy == CLIENT_DECORATION_NONE) return false;
    if (client->decoration_policy == CLIENT_DECORATION_FORCE) return true;
    return client->auto_decoration_eligible && !client->requests_no_decoration;
}

static int decoration_height(const WM *wm, const Client *client)
{
    return client_should_decorate(wm, client)
        ? (int)wm->config.decoration.height : 0;
}

/* X11 x/y remain the original window's border origin, and width/height its
 * content size. The title strip extends above that unchanged rectangle. */
Rect client_outer_rect(const WM *wm, const Client *client, Rect content)
{
    int border = (int)ui_client_border_width(wm, client);
    int height = decoration_height(wm, client);
    return (Rect){content.x, content.y - height,
                  content.width + 2 * border,
                  content.height + 2 * border + height};
}

Rect client_content_rect(const WM *wm, const Client *client, Rect outer)
{
    int border = (int)ui_client_border_width(wm, client);
    int height = decoration_height(wm, client);
    int width = outer.width - 2 * border;
    int content_height = outer.height - 2 * border - height;
    return (Rect){outer.x, outer.y + height,
                  width > 0 ? width : 1,
                  content_height > 0 ? content_height : 1};
}

Rect client_decoration_rect(const WM *wm, const Client *client)
{
    Rect rect = client_outer_rect(wm, client, client->geometry);
    rect.height = decoration_height(wm, client);
    return rect;
}

Client *decoration_client_for_window(const WM *wm, Window window)
{
    if (!window) return NULL;
    for (Client *client = wm->model.clients; client; client = client->next)
        if (x11_client(client)->decoration == window) return client;
    return NULL;
}

void decoration_destroy(WM *wm, Client *client)
{
    decoration_input_cancel(wm, client);
    if (x11_client(client)->decoration_draw) XftDrawDestroy(x11_client(client)->decoration_draw);
    if (x11_client(client)->decoration) XDestroyWindow(wm->display, x11_client(client)->decoration);
    x11_client(client)->decoration_draw = NULL;
    x11_client(client)->decoration = None;
    x11_client(client)->decoration_mapped = false;
}

static bool decoration_create(WM *wm, Client *client, Rect rect)
{
    XSetWindowAttributes attributes = {
        .override_redirect = True,
        .background_pixel = wm->decoration_bg.pixel,
        .event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask | EnterWindowMask | LeaveWindowMask,
        .cursor = wm->cursor_normal,
    };
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    x11_client(client)->decoration = XCreateWindow(
        wm->display, wm->root, rect.x, rect.y,
        (unsigned int)rect.width, (unsigned int)rect.height, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput, visual,
        CWOverrideRedirect | CWBackPixel | CWEventMask | CWCursor, &attributes);
    if (!x11_client(client)->decoration) return false;
    x11_client(client)->decoration_draw = XftDrawCreate(
        wm->display, x11_client(client)->decoration, visual,
        DefaultColormap(wm->display, wm->screen));
    if (!x11_client(client)->decoration_draw) {
        decoration_destroy(wm, client);
        return false;
    }
    char name[64];
    snprintf(name, sizeof(name), "box2430-decoration-0x%lx", x11_client(client)->window);
    XStoreName(wm->display, x11_client(client)->decoration, name);
    return true;
}

static void outline(XftDraw *draw, const XftColor *fg, int x, int y, unsigned int size)
{
    XftDrawRect(draw, fg, x, y, size, 1);
    XftDrawRect(draw, fg, x, y + (int)size - 1, size, 1);
    XftDrawRect(draw, fg, x, y, 1, size);
    XftDrawRect(draw, fg, x + (int)size - 1, y, 1, size);
}

static void draw_button(WM *wm, Client *client, const XftColor *fg,
                         DecorationItem item, Rect rect)
{
    if (rect.width <= 0 || rect.height <= 0) return;
    const DecorationConfig *config = &wm->config.decoration;
    const char *label = item == DECORATION_ITEM_CLOSE ? config->close_label :
        client->maximized ? config->restore_label : config->maximize_label;
    if (label[0]) {
        unsigned int text_width = ui_text_width(wm->display, wm->decoration_fonts,
                                                wm->decoration_font_count, label);
        unsigned int padding = text_width < (unsigned int)rect.width
            ? ((unsigned int)rect.width - text_width) / 2 : 0;
        ui_draw_text(wm->display, x11_client(client)->decoration_draw, fg, wm->decoration_fonts,
                     wm->decoration_font_count, rect.x, rect.y, (unsigned int)rect.width,
                     (unsigned int)rect.height, padding, label);
        return;
    }
    /* Font-independent, single-pixel primitives, entirely inside the item. */
    int size = rect.height / 2;
    if (size > rect.width - 4) size = rect.width - 4;
    if (size < 4) return;
    int x = rect.x + (rect.width - size) / 2, y = (rect.height - size) / 2;
    if (item == DECORATION_ITEM_CLOSE) {
        for (int i = 0; i < size; ++i) {
            XftDrawRect(x11_client(client)->decoration_draw, fg, x + i, y + i, 1, 1);
            XftDrawRect(x11_client(client)->decoration_draw, fg, x + size - 1 - i, y + i, 1, 1);
        }
    } else if (client->maximized) {
        outline(x11_client(client)->decoration_draw, fg, x + 3, y, (unsigned int)size - 3);
        outline(x11_client(client)->decoration_draw, fg, x, y + 3, (unsigned int)size - 3);
    } else outline(x11_client(client)->decoration_draw, fg, x, y, (unsigned int)size);
}

void decoration_draw(WM *wm, Client *client)
{
    if (!x11_client(client)->decoration_mapped || !x11_client(client)->decoration_draw) return;
    bool focused = wm->model.focused_client == client;
    const XftColor *bg = focused ? &wm->decoration_focused_bg : &wm->decoration_bg;
    const XftColor *fg = focused ? &wm->decoration_focused_fg : &wm->decoration_fg;
    Rect rect = client_decoration_rect(wm, client);
    if (rect.height <= 0) return;
    XftDrawRect(x11_client(client)->decoration_draw, bg, 0, 0,
                (unsigned int)rect.width, (unsigned int)rect.height);
    DecorationLayout layout = decoration_layout(wm, client);
    Rect title = layout.items[DECORATION_ITEM_TITLE];
    ui_draw_text(wm->display, x11_client(client)->decoration_draw, fg,
                 wm->decoration_fonts, wm->decoration_font_count, title.x, title.y,
                 (unsigned int)title.width, (unsigned int)title.height,
                 wm->config.decoration.padding, ui_client_label(client, UI_LABEL_TITLE));
    draw_button(wm, client, fg, DECORATION_ITEM_MAXIMIZE, layout.items[DECORATION_ITEM_MAXIMIZE]);
    draw_button(wm, client, fg, DECORATION_ITEM_CLOSE, layout.items[DECORATION_ITEM_CLOSE]);
    /* Geometry/title/state changes can move a region under a stationary pointer. */
    Window root, child;
    int root_x, root_y, x, y;
    unsigned int mask;
    if (XQueryPointer(wm->display, x11_client(client)->decoration, &root, &child,
                      &root_x, &root_y, &x, &y, &mask))
        decoration_pointer_cursor(wm, client, root_x, root_y);
}

void decoration_reconcile(WM *wm, Client *client)
{
    bool eligible = client_should_decorate(wm, client);
    bool visible = eligible && x11_client(client)->mapped &&
        client->workspace == client->workspace->monitor->active_workspace;
    if (!visible) {
        decoration_input_cancel(wm, client);
        if (x11_client(client)->decoration) XDefineCursor(wm->display, x11_client(client)->decoration, wm->cursor_normal);
        if (x11_client(client)->decoration_mapped) XUnmapWindow(wm->display, x11_client(client)->decoration);
        x11_client(client)->decoration_mapped = false;
        return;
    }
    if (!wm->decoration_resources_ready) return;
    Rect rect = client_decoration_rect(wm, client);
    if (!x11_client(client)->decoration && !decoration_create(wm, client, rect)) {
        fprintf(stderr, "box2430: cannot create client decoration\n");
        wm->running = false;
        return;
    }
    XMoveResizeWindow(wm->display, x11_client(client)->decoration, rect.x, rect.y,
                      (unsigned int)rect.width, (unsigned int)rect.height);
    if (!x11_client(client)->decoration_mapped) {
        /* The owner may have moved in the X stack while its strip was hidden
         * (MONOCLE/fullscreen). Reinsert it adjacent to the owner before map. */
        XWindowChanges changes = {.sibling = x11_client(client)->window, .stack_mode = Above};
        XConfigureWindow(wm->display, x11_client(client)->decoration,
                         CWSibling | CWStackMode, &changes);
        XMapWindow(wm->display, x11_client(client)->decoration);
    }
    x11_client(client)->decoration_mapped = true;
    decoration_draw(wm, client);
}
