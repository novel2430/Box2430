#include "decoration.h"
#include "ui.h"

#include <stdio.h>

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
        if (client->decoration == window) return client;
    return NULL;
}

void decoration_destroy(WM *wm, Client *client)
{
    if (client->decoration_draw) XftDrawDestroy(client->decoration_draw);
    if (client->decoration) XDestroyWindow(wm->display, client->decoration);
    client->decoration_draw = NULL;
    client->decoration = None;
    client->decoration_mapped = false;
}

static bool decoration_create(WM *wm, Client *client, Rect rect)
{
    XSetWindowAttributes attributes = {
        .override_redirect = True,
        .background_pixel = wm->decoration_bg.pixel,
        .event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
                      PointerMotionMask,
        .cursor = wm->cursor_normal,
    };
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    client->decoration = XCreateWindow(
        wm->display, wm->root, rect.x, rect.y,
        (unsigned int)rect.width, (unsigned int)rect.height, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput, visual,
        CWOverrideRedirect | CWBackPixel | CWEventMask | CWCursor, &attributes);
    if (!client->decoration) return false;
    client->decoration_draw = XftDrawCreate(
        wm->display, client->decoration, visual,
        DefaultColormap(wm->display, wm->screen));
    if (!client->decoration_draw) {
        decoration_destroy(wm, client);
        return false;
    }
    char name[64];
    snprintf(name, sizeof(name), "box2430-decoration-0x%lx", client->window);
    XStoreName(wm->display, client->decoration, name);
    return true;
}

void decoration_draw(WM *wm, Client *client)
{
    if (!client->decoration_mapped || !client->decoration_draw) return;
    bool focused = wm->model.focused_client == client;
    const XftColor *bg = focused ? &wm->decoration_focused_bg : &wm->decoration_bg;
    const XftColor *fg = focused ? &wm->decoration_focused_fg : &wm->decoration_fg;
    Rect rect = client_decoration_rect(wm, client);
    if (rect.height <= 0) return;
    XftDrawRect(client->decoration_draw, bg, 0, 0,
                (unsigned int)rect.width, (unsigned int)rect.height);
    /* Reuse the existing normal tab font/fallback set and title cache. */
    ui_draw_text(wm->display, client->decoration_draw, fg,
                 wm->tab_fonts, wm->tab_font_count, 0, 0,
                 (unsigned int)rect.width, (unsigned int)rect.height,
                 wm->config.tabs.padding, ui_client_label(client, UI_LABEL_TITLE));
}

void decoration_reconcile(WM *wm, Client *client)
{
    bool eligible = client_should_decorate(wm, client);
    bool visible = eligible && client->mapped &&
        client->workspace == client->workspace->monitor->active_workspace;
    if (!visible) {
        if (client->decoration_mapped) XUnmapWindow(wm->display, client->decoration);
        client->decoration_mapped = false;
        return;
    }
    if (!wm->decoration_resources_ready) return;
    Rect rect = client_decoration_rect(wm, client);
    if (!client->decoration && !decoration_create(wm, client, rect)) {
        fprintf(stderr, "box2430: cannot create client decoration\n");
        wm->running = false;
        return;
    }
    XMoveResizeWindow(wm->display, client->decoration, rect.x, rect.y,
                      (unsigned int)rect.width, (unsigned int)rect.height);
    if (!client->decoration_mapped) {
        /* The owner may have moved in the X stack while its strip was hidden
         * (MONOCLE/fullscreen). Reinsert it adjacent to the owner before map. */
        XWindowChanges changes = {.sibling = client->window, .stack_mode = Above};
        XConfigureWindow(wm->display, client->decoration,
                         CWSibling | CWStackMode, &changes);
        XMapWindow(wm->display, client->decoration);
    }
    client->decoration_mapped = true;
    decoration_draw(wm, client);
}
