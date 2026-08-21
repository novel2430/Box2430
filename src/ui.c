#include "ui.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UIStyle ui_resolve_style(UIStyle base, const UIStyleOverride *override)
{
    if (!override) return base;
    if (override->has_fg) memcpy(base.fg, override->fg, sizeof(base.fg));
    if (override->has_bg) memcpy(base.bg, override->bg, sizeof(base.bg));
    if (override->has_font_style) base.font_style = override->font_style;
    if (override->has_format) base.format = override->format;
    return base;
}

const char *ui_client_label(const Client *client, UILabelSource source)
{
    if (!client) return "";
    switch (source) {
    case UI_LABEL_TITLE:
        return client->title ? client->title : "";
    case UI_LABEL_CLASS:
        return client->class_name ? client->class_name : "";
    case UI_LABEL_INSTANCE:
        return client->instance ? client->instance : "";
    }
    return "";
}

char *ui_format_label(const UILabelFormat *format, const char *value)
{
    if (!format || !value || !value[0]) return strdup("");
    size_t prefix_length = strlen(format->prefix);
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(format->suffix);
    if (prefix_length > SIZE_MAX - value_length ||
        prefix_length + value_length > SIZE_MAX - suffix_length - 1U)
        return NULL;
    size_t length = prefix_length + value_length + suffix_length;
    char *output = malloc(length + 1U);
    if (!output) return NULL;
    memcpy(output, format->prefix, prefix_length);
    memcpy(output + prefix_length, value, value_length);
    memcpy(output + prefix_length + value_length, format->suffix,
           suffix_length + 1U);
    return output;
}

static int utf8_length(const FcChar8 *text, FcChar32 *character)
{
    unsigned char first = text[0];
    if (first < 0x80) {
        *character = first;
        return 1;
    }
    int length = first >= 0xf0 && first <= 0xf4 ? 4
        : first >= 0xe0 ? 3 : first >= 0xc2 ? 2 : 1;
    FcChar32 value = length == 4 ? first & 0x07U
        : length == 3 ? first & 0x0fU : length == 2 ? first & 0x1fU : first;
    if (length == 1) {
        *character = value;
        return 1;
    }
    for (int i = 1; i < length; ++i) {
        if (!text[i] || (text[i] & 0xc0U) != 0x80U) {
            *character = first;
            return 1;
        }
        value = (value << 6) | (text[i] & 0x3fU);
    }
    FcChar32 minimum = length == 4 ? 0x10000U : length == 3 ? 0x800U : 0x80U;
    if (value < minimum || value > 0x10ffffU ||
        (value >= 0xd800U && value <= 0xdfffU)) {
        *character = first;
        return 1;
    }
    *character = value;
    return length;
}

static XftFont *font_for_character(Display *display, XftFont *const *fonts,
                                   unsigned int font_count,
                                   FcChar32 character)
{
    if (!fonts || !font_count) return NULL;
    for (unsigned int i = 0; i < font_count; ++i)
        if (XftCharExists(display, fonts[i], character)) return fonts[i];
    return fonts[0];
}

static unsigned int character_width(Display *display, XftFont *const *fonts,
                                    unsigned int font_count,
                                    const FcChar8 *text, int length,
                                    FcChar32 character)
{
    XftFont *font = font_for_character(display, fonts, font_count, character);
    if (!font) return 0;
    XGlyphInfo extents = {0};
    XftTextExtentsUtf8(display, font, text, length, &extents);
    return (unsigned int)extents.xOff;
}

unsigned int ui_text_width(Display *display, XftFont *const *fonts,
                           unsigned int font_count, const char *text)
{
    if (!display || !fonts || !font_count || !text) return 0;
    const FcChar8 *cursor = (const FcChar8 *)text;
    unsigned int width = 0;
    while (*cursor) {
        FcChar32 character;
        int length = utf8_length(cursor, &character);
        unsigned int advance = character_width(display, fonts, font_count,
                                                cursor, length, character);
        if (UINT_MAX - width < advance) return UINT_MAX;
        width += advance;
        cursor += length;
    }
    return width;
}

static size_t prefix_for_width(Display *display, XftFont *const *fonts,
                               unsigned int font_count, const char *text,
                               unsigned int available_width)
{
    const FcChar8 *start = (const FcChar8 *)text;
    const FcChar8 *cursor = start;
    unsigned int width = 0;
    while (*cursor) {
        FcChar32 character;
        int length = utf8_length(cursor, &character);
        unsigned int advance = character_width(display, fonts, font_count,
                                                cursor, length, character);
        if (advance > available_width - width) break;
        width += advance;
        cursor += length;
    }
    return (size_t)(cursor - start);
}

static int draw_prefix(Display *display, XftDraw *draw, const XftColor *color,
                       XftFont *const *fonts, unsigned int font_count,
                       int x, int baseline, const char *text, size_t byte_count)
{
    const FcChar8 *cursor = (const FcChar8 *)text;
    const FcChar8 *end = cursor + byte_count;
    while (cursor < end) {
        FcChar32 character;
        int length = utf8_length(cursor, &character);
        XftFont *font = font_for_character(display, fonts, font_count, character);
        if (!font) break;
        XftDrawStringUtf8(draw, color, font, x, baseline, cursor, length);
        XGlyphInfo extents = {0};
        XftTextExtentsUtf8(display, font, cursor, length, &extents);
        x += extents.xOff;
        cursor += length;
    }
    return x;
}

void ui_draw_text(Display *display, XftDraw *draw, const XftColor *color,
                  XftFont *const *fonts, unsigned int font_count,
                  int x, int y, unsigned int width, unsigned int height,
                  unsigned int padding, const char *text)
{
    if (!display || !draw || !color || !fonts || !font_count || !text ||
        !width || !height) return;

    XRectangle clip = {
        .x = (short)x,
        .y = (short)y,
        .width = (unsigned short)width,
        .height = (unsigned short)height,
    };
    XftDrawSetClipRectangles(draw, 0, 0, &clip, 1);

    if (padding > width / 2U) {
        XftDrawSetClip(draw, NULL);
        return;
    }
    unsigned int available_width = width - 2U * padding;
    if (!available_width) {
        XftDrawSetClip(draw, NULL);
        return;
    }

    XftFont *base_font = fonts[0];
    int baseline = y + ((int)height - base_font->ascent - base_font->descent) / 2 +
                   base_font->ascent;
    int text_x = x + (int)padding;
    unsigned int full_width = ui_text_width(display, fonts, font_count, text);
    size_t prefix_bytes;
    bool ellipsize = full_width > available_width;
    unsigned int ellipsis_width = 0;
    if (ellipsize) {
        ellipsis_width = ui_text_width(display, fonts, font_count, "...");
        unsigned int prefix_width = ellipsis_width <= available_width
            ? available_width - ellipsis_width : available_width;
        prefix_bytes = prefix_for_width(display, fonts, font_count, text,
                                        prefix_width);
    } else {
        const FcChar8 *cursor = (const FcChar8 *)text;
        while (*cursor) {
            FcChar32 character;
            cursor += utf8_length(cursor, &character);
        }
        prefix_bytes = (size_t)(cursor - (const FcChar8 *)text);
    }

    text_x = draw_prefix(display, draw, color, fonts, font_count,
                         text_x, baseline, text, prefix_bytes);
    if (ellipsize && ellipsis_width <= available_width) {
        size_t ellipsis_bytes = sizeof("...") - 1U;
        draw_prefix(display, draw, color, fonts, font_count,
                    text_x, baseline, "...", ellipsis_bytes);
    }
    XftDrawSetClip(draw, NULL);
}

typedef enum TabVisualState {
    TAB_VISUAL_INACTIVE,
    TAB_VISUAL_ACTIVE,
    TAB_VISUAL_URGENT,
} TabVisualState;

static XftFont *const *tab_fonts(const WM *wm, UIFontStyle style,
                                 unsigned int *count_return)
{
    if (style == UI_FONT_BOLD) {
        *count_return = wm->tab_font_bold_count;
        return wm->tab_fonts_bold;
    }
    *count_return = wm->tab_font_count;
    return wm->tab_fonts;
}

static TabVisualState tab_visual_state(const Client *client)
{
    if (client->urgent) return TAB_VISUAL_URGENT;
    if (workspace_focus_target(client->workspace) == client)
        return TAB_VISUAL_ACTIVE;
    return TAB_VISUAL_INACTIVE;
}

static UIStyle tab_style(const WM *wm, TabVisualState state)
{
    const UIStyleOverride *override = state == TAB_VISUAL_URGENT
        ? &wm->config.tabs.urgent
        : state == TAB_VISUAL_ACTIVE ? &wm->config.tabs.active
        : &wm->config.tabs.inactive;
    return ui_resolve_style(wm->config.tabs.style, override);
}

static char *tab_label(const WM *wm, const Client *client, const UIStyle *style)
{
    const char *value = ui_client_label(client, wm->config.tabs.source);
    return ui_format_label(&style->format, value);
}

static unsigned int natural_tab_width(WM *wm, const Client *client)
{
    UIStyle style = tab_style(wm, tab_visual_state(client));
    char *label = tab_label(wm, client, &style);
    if (!label) return UINT_MAX;
    unsigned int font_count;
    XftFont *const *fonts = tab_fonts(wm, style.font_style, &font_count);
    unsigned int text_width = ui_text_width(wm->display, fonts, font_count, label);
    free(label);
    unsigned int padding = 2U * wm->config.tabs.padding;
    return UINT_MAX - text_width < padding ? UINT_MAX : text_width + padding;
}

static unsigned int tab_count(const Workspace *workspace)
{
    unsigned int count = 0;
    for (const Client *client = workspace->tab_head; client; client = client->tab_next)
        ++count;
    return count;
}

static void tab_bounds(WM *wm, Monitor *monitor, Client *wanted,
                       int *x_return, unsigned int *width_return)
{
    Workspace *workspace = monitor->active_workspace;
    unsigned int count = tab_count(workspace);
    unsigned int total = 0;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        unsigned int natural = natural_tab_width(wm, client);
        total = UINT_MAX - total < natural ? UINT_MAX : total + natural;
    }
    int x = 0;
    unsigned int index = 0;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        unsigned int width = total <= (unsigned int)monitor->workarea.width
            ? natural_tab_width(wm, client)
            : (unsigned int)monitor->workarea.width / count +
              (index < (unsigned int)monitor->workarea.width % count ? 1U : 0U);
        if (client == wanted) {
            *x_return = x;
            *width_return = width;
            return;
        }
        x += (int)width;
        ++index;
    }
    *x_return = x;
    *width_return = 0;
}

unsigned int ui_tab_height(const WM *wm, const Monitor *monitor)
{
    int minimum_content_height = 2 * (int)wm->config.border_width + 1;
    if (!wm->config.tabs.enabled ||
        monitor->workarea.height <= minimum_content_height) return 0;
    int available = monitor->workarea.height - minimum_content_height;
    return wm->config.tabs.height <= (unsigned int)available
        ? wm->config.tabs.height : (unsigned int)available;
}

Monitor *ui_tab_monitor_for_window(WM *wm, Window window)
{
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        if (wm->monitors[i].tab_bar == window) return &wm->monitors[i];
    return NULL;
}

Client *ui_tab_hit_test(WM *wm, Monitor *monitor, int x)
{
    Workspace *workspace = monitor->active_workspace;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        int left;
        unsigned int width;
        tab_bounds(wm, monitor, client, &left, &width);
        if (x >= left && x < left + (int)width) return client;
    }
    return NULL;
}

void ui_tab_draw(WM *wm, Monitor *monitor)
{
    if (!monitor->tab_draw) return;
    unsigned int height = ui_tab_height(wm, monitor);
    if (!height) height = 1;
    XClearWindow(wm->display, monitor->tab_bar);
    Workspace *workspace = monitor->active_workspace;
    for (Client *client = workspace->tab_head; client; client = client->tab_next) {
        int x;
        unsigned int width;
        tab_bounds(wm, monitor, client, &x, &width);
        TabVisualState state = tab_visual_state(client);
        UIStyle style = tab_style(wm, state);
        XftColor *fg = state == TAB_VISUAL_URGENT ? &wm->tab_urgent_fg
            : state == TAB_VISUAL_ACTIVE ? &wm->tab_active_fg
            : &wm->tab_inactive_fg;
        XftColor *bg = state == TAB_VISUAL_URGENT ? &wm->tab_urgent_bg
            : state == TAB_VISUAL_ACTIVE ? &wm->tab_active_bg
            : &wm->tab_inactive_bg;
        XSetForeground(wm->display, DefaultGC(wm->display, wm->screen), bg->pixel);
        XFillRectangle(wm->display, monitor->tab_bar,
                       DefaultGC(wm->display, wm->screen), x, 0, width, height);
        char *label = tab_label(wm, client, &style);
        if (!label) continue;
        unsigned int font_count;
        XftFont *const *fonts = tab_fonts(wm, style.font_style, &font_count);
        ui_draw_text(wm->display, monitor->tab_draw, fg, fonts, font_count,
                     x, 0, width, height, wm->config.tabs.padding, label);
        free(label);
    }
}

void ui_tab_update(WM *wm)
{
    if (!wm->tab_resources_ready) return;
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Monitor *monitor = &wm->monitors[i];
        unsigned int height = ui_tab_height(wm, monitor);
        bool visible = wm->config.tabs.enabled &&
            monitor->active_workspace->mode == WORKSPACE_MONOCLE && height;
        unsigned int window_height = height ? height : 1;
        XMoveResizeWindow(wm->display, monitor->tab_bar,
                          monitor->workarea.x, monitor->workarea.y,
                          (unsigned int)monitor->workarea.width, window_height);
        if (visible) {
            XMapWindow(wm->display, monitor->tab_bar);
            ui_tab_draw(wm, monitor);
        } else {
            XUnmapWindow(wm->display, monitor->tab_bar);
        }
    }
}

void ui_tab_name_monitor(WM *wm, Monitor *monitor)
{
    if (!monitor->tab_bar) return;
    char name[64];
    snprintf(name, sizeof(name), "box2430-tabbar-%u", monitor->index);
    XStoreName(wm->display, monitor->tab_bar, name);
}

bool ui_tab_create_monitor(WM *wm, Monitor *monitor)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    XSetWindowAttributes attributes = {
        .override_redirect = True,
        .background_pixel = wm->tab_inactive_bg.pixel,
        .event_mask = ExposureMask | ButtonPressMask,
    };
    unsigned int height = ui_tab_height(wm, monitor);
    if (!height) height = 1;
    monitor->tab_bar = XCreateWindow(
        wm->display, wm->root, monitor->workarea.x, monitor->workarea.y,
        (unsigned int)monitor->workarea.width, height, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput, visual,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    ui_tab_name_monitor(wm, monitor);
    monitor->tab_draw = XftDrawCreate(
        wm->display, monitor->tab_bar, visual,
        DefaultColormap(wm->display, wm->screen));
    if (!monitor->tab_draw) {
        XDestroyWindow(wm->display, monitor->tab_bar);
        monitor->tab_bar = None;
        return false;
    }
    return true;
}

void ui_tab_destroy_monitor(WM *wm, Monitor *monitor)
{
    if (monitor->tab_draw) {
        XftDrawDestroy(monitor->tab_draw);
        monitor->tab_draw = NULL;
    }
    if (monitor->tab_bar) {
        XDestroyWindow(wm->display, monitor->tab_bar);
        monitor->tab_bar = None;
    }
}

static unsigned int load_fonts(WM *wm, const char *name, bool bold,
                               XftFont **fonts)
{
    unsigned int count = 0;
    fonts[count] = XftFontOpenName(wm->display, wm->screen, name);
    if (!fonts[count]) return 0;
    ++count;
    const char *regular[] = {
        "sans:lang=zh-cn", "sans:lang=ja", "sans:lang=ko", "sans",
    };
    const char *heavy[] = {
        "sans:style=Bold:lang=zh-cn", "sans:style=Bold:lang=ja",
        "sans:style=Bold:lang=ko", "sans:style=Bold",
    };
    const char *const *fallbacks = bold ? heavy : regular;
    for (size_t i = 0; i < sizeof(regular) / sizeof(regular[0]) &&
                       count < BOX2430_MAX_TAB_FONTS; ++i) {
        XftFont *font = XftFontOpenName(wm->display, wm->screen, fallbacks[i]);
        if (font) fonts[count++] = font;
    }
    return count;
}

static void close_fonts(WM *wm)
{
    for (unsigned int i = 0; i < wm->tab_font_count; ++i)
        XftFontClose(wm->display, wm->tab_fonts[i]);
    for (unsigned int i = 0; i < wm->tab_font_bold_count; ++i)
        XftFontClose(wm->display, wm->tab_fonts_bold[i]);
    wm->tab_font_count = 0;
    wm->tab_font_bold_count = 0;
}

static void free_colors(WM *wm)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    Colormap colormap = DefaultColormap(wm->display, wm->screen);
    XftColor *colors[] = {
        &wm->tab_active_fg, &wm->tab_active_bg,
        &wm->tab_inactive_fg, &wm->tab_inactive_bg,
        &wm->tab_urgent_fg, &wm->tab_urgent_bg,
    };
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i)
        XftColorFree(wm->display, visual, colormap, colors[i]);
}

bool ui_init(WM *wm)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    Colormap colormap = DefaultColormap(wm->display, wm->screen);
    wm->tab_font_count = load_fonts(wm, wm->config.tabs.font, false,
                                    wm->tab_fonts);
    wm->tab_font_bold_count = load_fonts(wm, wm->config.tabs.font_bold, true,
                                         wm->tab_fonts_bold);
    if (!wm->tab_font_count || !wm->tab_font_bold_count) {
        fprintf(stderr, "box2430: cannot open configured tab bar fonts\n");
        close_fonts(wm);
        return false;
    }

    XftColor *colors[] = {
        &wm->tab_active_fg, &wm->tab_active_bg,
        &wm->tab_inactive_fg, &wm->tab_inactive_bg,
        &wm->tab_urgent_fg, &wm->tab_urgent_bg,
    };
    UIStyle active = tab_style(wm, TAB_VISUAL_ACTIVE);
    UIStyle inactive = tab_style(wm, TAB_VISUAL_INACTIVE);
    UIStyle urgent = tab_style(wm, TAB_VISUAL_URGENT);
    const char *names[] = {
        active.fg, active.bg,
        inactive.fg, inactive.bg,
        urgent.fg, urgent.bg,
    };
    size_t color_count = sizeof(colors) / sizeof(colors[0]);
    size_t allocated = 0;
    for (; allocated < color_count; ++allocated) {
        if (!XftColorAllocName(wm->display, visual, colormap,
                               names[allocated], colors[allocated])) {
            fprintf(stderr, "box2430: cannot allocate tab bar color %s\n",
                    names[allocated]);
            for (size_t j = 0; j < allocated; ++j)
                XftColorFree(wm->display, visual, colormap, colors[j]);
            close_fonts(wm);
            return false;
        }
    }

    unsigned int created = 0;
    for (; created < wm->monitor_count; ++created) {
        if (!ui_tab_create_monitor(wm, &wm->monitors[created])) {
            fprintf(stderr, "box2430: cannot create tab bar drawing context\n");
            for (unsigned int i = 0; i < created; ++i)
                ui_tab_destroy_monitor(wm, &wm->monitors[i]);
            free_colors(wm);
            close_fonts(wm);
            return false;
        }
    }
    wm->tab_resources_ready = true;
    return true;
}

void ui_destroy(WM *wm)
{
    if (!wm->display) return;
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        ui_tab_destroy_monitor(wm, &wm->monitors[i]);
    if (wm->tab_resources_ready) {
        free_colors(wm);
        wm->tab_resources_ready = false;
    }
    close_fonts(wm);
}
