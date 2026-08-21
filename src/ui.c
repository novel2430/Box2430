#include "ui.h"
#include "tray.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static XftFont *const *bar_fonts(const WM *wm, UIFontStyle style,
                                 unsigned int *count_return)
{
    if (style == UI_FONT_BOLD) {
        *count_return = wm->bar_font_bold_count;
        return wm->bar_fonts_bold;
    }
    *count_return = wm->bar_font_count;
    return wm->bar_fonts;
}

static bool configured_bar_widget(const WM *wm, UIBarWidget wanted)
{
    const UIBarWidget *groups[] = {
        wm->config.bar.left, wm->config.bar.center, wm->config.bar.right,
    };
    const unsigned int counts[] = {
        wm->config.bar.left_count, wm->config.bar.center_count,
        wm->config.bar.right_count,
    };
    for (size_t group = 0; group < sizeof(groups) / sizeof(groups[0]); ++group)
        for (unsigned int i = 0; i < counts[group]; ++i)
            if (groups[group][i] == wanted) return true;
    return false;
}

bool ui_clock_visible(const WM *wm)
{
    if (!wm || !wm->config.bar.enabled ||
        !configured_bar_widget(wm, UI_WIDGET_CLOCK)) return false;
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        if (wm->monitors[i].bar_geometry.width > 0 &&
            wm->monitors[i].bar_geometry.height > 0) return true;
    return false;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static size_t format_clock_text(char *output, size_t output_size,
                                const char *format, const struct tm *local)
{
    return strftime(output, output_size, format, local);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static bool update_clock_text(WM *wm)
{
    if (!ui_clock_visible(wm)) return false;
    time_t now = time(NULL);
    struct tm local;
    if (now == (time_t)-1 || !localtime_r(&now, &local)) return false;
    char next[BOX2430_MAX_CLOCK_TEXT] = {0};
    if (!format_clock_text(next, sizeof(next), wm->config.bar.clock.format,
                           &local))
        next[0] = '\0';
    if (strcmp(next, wm->clock_text) == 0) return false;
    memcpy(wm->clock_text, next, sizeof(wm->clock_text));
    wm->clock_text[sizeof(wm->clock_text) - 1U] = '\0';
    return true;
}

void ui_clock_tick(WM *wm)
{
    if (update_clock_text(wm)) ui_bar_update(wm);
}

void ui_status_refresh(WM *wm)
{
    if (!wm || !wm->display) return;
    char *next = x11_read_root_status(wm);
    if (!next) return;
    if (wm->status_text && strcmp(wm->status_text, next) == 0) {
        free(next);
        return;
    }
    free(wm->status_text);
    wm->status_text = next;
    if (wm->config.bar.enabled && configured_bar_widget(wm, UI_WIDGET_STATUS))
        ui_bar_update(wm);
}

static const UIStyleOverride *workspace_state_override(
    const WM *wm, UIWorkspaceVisualState state)
{
    switch (state) {
    case UI_WORKSPACE_EMPTY: return &wm->config.bar.workspaces.empty;
    case UI_WORKSPACE_OCCUPIED: return &wm->config.bar.workspaces.occupied;
    case UI_WORKSPACE_ACTIVE: return &wm->config.bar.workspaces.active;
    case UI_WORKSPACE_URGENT: return &wm->config.bar.workspaces.urgent;
    case UI_WORKSPACE_ACTIVE_URGENT:
        return &wm->config.bar.workspaces.active_urgent;
    case UI_WORKSPACE_STATE_COUNT: break;
    }
    return NULL;
}

UIWorkspaceVisualState ui_workspace_visual_state(const Monitor *monitor,
                                                 const Workspace *workspace)
{
    bool urgent = false;
    bool occupied = workspace && workspace->clients;
    if (workspace) {
        for (const Client *client = workspace->clients; client;
             client = client->workspace_next) {
            if (client->urgent) {
                urgent = true;
                break;
            }
        }
    }
    bool active = monitor && workspace && monitor->active_workspace == workspace;
    if (active && urgent) return UI_WORKSPACE_ACTIVE_URGENT;
    if (active) return UI_WORKSPACE_ACTIVE;
    if (urgent) return UI_WORKSPACE_URGENT;
    if (occupied) return UI_WORKSPACE_OCCUPIED;
    return UI_WORKSPACE_EMPTY;
}

static UIStyle workspace_style_for_state(const WM *wm,
                                          UIWorkspaceVisualState state)
{
    UIStyle style = ui_resolve_style(wm->config.bar.style,
                                     &wm->config.bar.workspaces.style);
    return ui_resolve_style(style, workspace_state_override(wm, state));
}

static UIModeVisualState mode_visual_state(const Workspace *workspace)
{
    return workspace && workspace->mode == WORKSPACE_MONOCLE
        ? UI_MODE_MONOCLE : UI_MODE_FREE;
}

static UIStyle mode_style_for_state(const WM *wm, UIModeVisualState state)
{
    UIStyle style = ui_resolve_style(wm->config.bar.style,
                                     &wm->config.bar.mode.style);
    const UIStyleOverride *override = state == UI_MODE_MONOCLE
        ? &wm->config.bar.mode.monocle.style : &wm->config.bar.mode.free.style;
    return ui_resolve_style(style, override);
}

static UIStyle title_style(const WM *wm)
{
    return ui_resolve_style(wm->config.bar.style, &wm->config.bar.title.style);
}

static UIStyle status_style(const WM *wm)
{
    return ui_resolve_style(wm->config.bar.style, &wm->config.bar.status.style);
}

static UIStyle clock_style(const WM *wm)
{
    return ui_resolve_style(wm->config.bar.style, &wm->config.bar.clock.style);
}

static char *workspace_label(const WM *wm, const Monitor *monitor,
                             const Workspace *workspace)
{
    UIWorkspaceVisualState state = ui_workspace_visual_state(monitor, workspace);
    UIStyle style = workspace_style_for_state(wm, state);
    char value[32];
    snprintf(value, sizeof(value), "%u", workspace->index + 1U);
    return ui_format_label(&style.format, value);
}

static char *mode_label(const WM *wm, const Monitor *monitor)
{
    UIModeVisualState state = mode_visual_state(monitor->active_workspace);
    UIStyle style = mode_style_for_state(wm, state);
    const char *value = state == UI_MODE_MONOCLE
        ? wm->config.bar.mode.monocle.label : wm->config.bar.mode.free.label;
    return ui_format_label(&style.format, value);
}

static char *title_label(const WM *wm, const Monitor *monitor)
{
    UIStyle style = title_style(wm);
    Client *target = workspace_focus_target(monitor->active_workspace);
    const char *value = ui_client_label(target, wm->config.bar.title.source);
    return ui_format_label(&style.format, value);
}

static char *status_label(const WM *wm)
{
    UIStyle style = status_style(wm);
    return ui_format_label(&style.format, wm->status_text ? wm->status_text : "");
}

static const char *clock_label(const WM *wm)
{
    return wm->clock_text;
}

static unsigned int label_width(WM *wm, const UIStyle *style, const char *label)
{
    unsigned int font_count;
    XftFont *const *fonts = bar_fonts(wm, style->font_style, &font_count);
    return ui_text_width(wm->display, fonts, font_count, label);
}

static unsigned int workspace_item_width(WM *wm, Monitor *monitor,
                                         Workspace *workspace)
{
    UIWorkspaceVisualState state = ui_workspace_visual_state(monitor, workspace);
    UIStyle style = workspace_style_for_state(wm, state);
    char *label = workspace_label(wm, monitor, workspace);
    if (!label) return UINT_MAX;
    unsigned int width = label_width(wm, &style, label);
    free(label);
    return width;
}

static unsigned int widget_natural_width(WM *wm, Monitor *monitor,
                                         UIBarWidget widget)
{
    if (widget == UI_WIDGET_WORKSPACES) {
        unsigned int width = 0;
        for (unsigned int i = 0; i < wm->config.workspace_count; ++i) {
            unsigned int item = workspace_item_width(wm, monitor,
                                                     &monitor->workspaces[i]);
            if (UINT_MAX - width < item) return UINT_MAX;
            width += item;
        }
        return width;
    }
    if (widget == UI_WIDGET_MODE) {
        UIModeVisualState state = mode_visual_state(monitor->active_workspace);
        UIStyle style = mode_style_for_state(wm, state);
        char *label = mode_label(wm, monitor);
        if (!label) return UINT_MAX;
        unsigned int width = label_width(wm, &style, label);
        free(label);
        return width;
    }
    if (widget == UI_WIDGET_TITLE) {
        UIStyle style = title_style(wm);
        char *label = title_label(wm, monitor);
        if (!label) return UINT_MAX;
        unsigned int width = label_width(wm, &style, label);
        free(label);
        return width;
    }
    if (widget == UI_WIDGET_STATUS) {
        UIStyle style = status_style(wm);
        char *label = status_label(wm);
        if (!label) return UINT_MAX;
        unsigned int width = label_width(wm, &style, label);
        free(label);
        return width;
    }
    if (widget == UI_WIDGET_CLOCK) {
        UIStyle style = clock_style(wm);
        return label_width(wm, &style, clock_label(wm));
    }
    if (widget == UI_WIDGET_TRAY) return tray_widget_width(wm, monitor);
    return 0;
}

static unsigned int saturating_add(unsigned int left, unsigned int right)
{
    return UINT_MAX - left < right ? UINT_MAX : left + right;
}

static unsigned int group_width(const UIBarWidget *widgets, unsigned int count,
                                const unsigned int widths[UI_WIDGET_COUNT],
                                unsigned int gap)
{
    unsigned int total = 0;
    bool any = false;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned int width = widths[widgets[i]];
        if (!width) continue;
        if (any) total = saturating_add(total, gap);
        total = saturating_add(total, width);
        any = true;
    }
    return total;
}

static bool group_contains(const UIBarWidget *widgets, unsigned int count,
                           UIBarWidget wanted)
{
    for (unsigned int i = 0; i < count; ++i)
        if (widgets[i] == wanted) return true;
    return false;
}

static void shrink_widget(unsigned int widths[UI_WIDGET_COUNT],
                          UIBarWidget widget, unsigned int amount)
{
    if (amount >= widths[widget]) widths[widget] = 0;
    else widths[widget] -= amount;
}

static unsigned int shrink_group_widget(const UIBarWidget *widgets,
                                        unsigned int count,
                                        unsigned int widths[UI_WIDGET_COUNT],
                                        unsigned int gap, UIBarWidget widget,
                                        unsigned int excess)
{
    if (!excess || !group_contains(widgets, count, widget) || !widths[widget])
        return excess;
    unsigned int before = group_width(widgets, count, widths, gap);
    shrink_widget(widths, widget, excess);
    unsigned int after = group_width(widgets, count, widths, gap);
    return before > after && excess > before - after
        ? excess - (before - after) : 0;
}

static unsigned int proportional_left_width(unsigned int available,
                                            unsigned int left,
                                            unsigned int right)
{
    uint64_t total = (uint64_t)left + (uint64_t)right;
    if (!total) return 0;
    return (unsigned int)(((uint64_t)available * left) / total);
}

static void layout_group_left(Monitor *monitor, const UIBarWidget *widgets,
                              unsigned int count,
                              const unsigned int widths[UI_WIDGET_COUNT],
                              unsigned int gap, Rect bounds)
{
    int cursor = bounds.x;
    int end = bounds.x + bounds.width;
    bool any = false;
    for (unsigned int i = 0; i < count; ++i) {
        UIBarWidget widget = widgets[i];
        unsigned int natural = widths[widget];
        if (!natural) continue;
        if (any) {
            int remaining = end - cursor;
            int step = remaining > 0 && gap < (unsigned int)remaining
                ? (int)gap : remaining > 0 ? remaining : 0;
            cursor += step;
        }
        int remaining = end - cursor;
        int width = remaining > 0 && natural < (unsigned int)remaining
            ? (int)natural : remaining > 0 ? remaining : 0;
        monitor->bar_widget_rects[widget] = (Rect){cursor, 0, width,
                                                   monitor->bar_geometry.height};
        cursor += width;
        any = true;
    }
}

static void layout_group_right(Monitor *monitor, const UIBarWidget *widgets,
                               unsigned int count,
                               const unsigned int widths[UI_WIDGET_COUNT],
                               unsigned int gap, Rect bounds)
{
    int cursor = bounds.x + bounds.width;
    bool any = false;
    for (unsigned int i = count; i > 0; --i) {
        UIBarWidget widget = widgets[i - 1U];
        unsigned int natural = widths[widget];
        if (!natural) continue;
        if (any) {
            int remaining = cursor - bounds.x;
            int step = remaining > 0 && gap < (unsigned int)remaining
                ? (int)gap : remaining > 0 ? remaining : 0;
            cursor -= step;
        }
        int remaining = cursor - bounds.x;
        int width = remaining > 0 && natural < (unsigned int)remaining
            ? (int)natural : remaining > 0 ? remaining : 0;
        cursor -= width;
        monitor->bar_widget_rects[widget] = (Rect){cursor, 0, width,
                                                   monitor->bar_geometry.height};
        any = true;
    }
}

static void layout_bar(WM *wm, Monitor *monitor)
{
    memset(monitor->bar_widget_rects, 0, sizeof(monitor->bar_widget_rects));
    memset(monitor->bar_workspace_rects, 0, sizeof(monitor->bar_workspace_rects));
    int bar_width = monitor->bar_geometry.width;
    if (bar_width <= 0) return;

    unsigned int widths[UI_WIDGET_COUNT] = {0};
    for (unsigned int i = 0; i < UI_WIDGET_COUNT; ++i)
        widths[i] = widget_natural_width(wm, monitor, (UIBarWidget)i);

    unsigned int padding = wm->config.bar.padding;
    if (padding > (unsigned int)bar_width / 2U)
        padding = (unsigned int)bar_width / 2U;
    Rect content = {(int)padding, 0, bar_width - 2 * (int)padding,
                    monitor->bar_geometry.height};
    if (content.width <= 0) return;

    unsigned int left_width = group_width(wm->config.bar.left,
                                          wm->config.bar.left_count,
                                          widths, wm->config.bar.gap);
    unsigned int right_width = group_width(wm->config.bar.right,
                                           wm->config.bar.right_count,
                                           widths, wm->config.bar.gap);
    uint64_t edge_total = (uint64_t)left_width + (uint64_t)right_width;
    if (edge_total > (unsigned int)content.width) {
        unsigned int excess = edge_total - (unsigned int)content.width > UINT_MAX
            ? UINT_MAX : (unsigned int)(edge_total - (unsigned int)content.width);
        excess = shrink_group_widget(wm->config.bar.left,
                                     wm->config.bar.left_count, widths,
                                     wm->config.bar.gap, UI_WIDGET_STATUS, excess);
        excess = shrink_group_widget(wm->config.bar.right,
                                     wm->config.bar.right_count, widths,
                                     wm->config.bar.gap, UI_WIDGET_STATUS, excess);
        (void)excess;
        left_width = group_width(wm->config.bar.left, wm->config.bar.left_count,
                                 widths, wm->config.bar.gap);
        right_width = group_width(wm->config.bar.right, wm->config.bar.right_count,
                                  widths, wm->config.bar.gap);
    }

    unsigned int left_alloc = left_width;
    unsigned int right_alloc = right_width;
    if ((uint64_t)left_alloc + (uint64_t)right_alloc >
        (unsigned int)content.width) {
        left_alloc = proportional_left_width((unsigned int)content.width,
                                             left_alloc, right_alloc);
        right_alloc = (unsigned int)content.width - left_alloc;
    }
    if (left_alloc > (unsigned int)content.width)
        left_alloc = (unsigned int)content.width;
    if (right_alloc > (unsigned int)content.width - left_alloc)
        right_alloc = (unsigned int)content.width - left_alloc;

    Rect left_bounds = {content.x, 0, (int)left_alloc, content.height};
    Rect right_bounds = {content.x + content.width - (int)right_alloc, 0,
                         (int)right_alloc, content.height};
    layout_group_left(monitor, wm->config.bar.left, wm->config.bar.left_count,
                      widths, wm->config.bar.gap, left_bounds);
    layout_group_right(monitor, wm->config.bar.right, wm->config.bar.right_count,
                       widths, wm->config.bar.gap, right_bounds);

    unsigned int center_width = group_width(wm->config.bar.center,
                                            wm->config.bar.center_count,
                                            widths, wm->config.bar.gap);
    int center = bar_width / 2;
    int left_limit = left_bounds.x + left_bounds.width;
    if (left_limit < content.x) left_limit = content.x;
    int right_limit = right_bounds.x;
    int content_right = content.x + content.width;
    if (right_limit > content_right) right_limit = content_right;
    int left_space = center - left_limit;
    int right_space = right_limit - center;
    int half = left_space < right_space ? left_space : right_space;
    if (half < 0) half = 0;
    unsigned int max_center = (unsigned int)half * 2U;
    if (center_width > max_center) {
        unsigned int excess = center_width - max_center;
        excess = shrink_group_widget(wm->config.bar.center,
                                     wm->config.bar.center_count, widths,
                                     wm->config.bar.gap, UI_WIDGET_TITLE, excess);
        (void)excess;
        center_width = group_width(wm->config.bar.center,
                                   wm->config.bar.center_count,
                                   widths, wm->config.bar.gap);
        if (center_width > max_center) center_width = max_center;
    }
    Rect center_bounds = {(bar_width - (int)center_width) / 2, 0,
                          (int)center_width, content.height};
    layout_group_left(monitor, wm->config.bar.center,
                      wm->config.bar.center_count, widths, wm->config.bar.gap,
                      center_bounds);
}

static void fill_bar_rect(WM *wm, Monitor *monitor, Rect rect,
                          const XftColor *color)
{
    if (!monitor->bar || rect.width <= 0 || rect.height <= 0) return;
    XSetForeground(wm->display, DefaultGC(wm->display, wm->screen), color->pixel);
    XFillRectangle(wm->display, monitor->bar,
                   DefaultGC(wm->display, wm->screen), rect.x, rect.y,
                   (unsigned int)rect.width, (unsigned int)rect.height);
}

static void draw_bar_label(WM *wm, Monitor *monitor, Rect rect,
                           const UIStyle *style, const XftColor *fg,
                           const XftColor *bg, const char *label)
{
    if (rect.width <= 0 || rect.height <= 0 || !label || !label[0]) return;
    fill_bar_rect(wm, monitor, rect, bg);
    unsigned int font_count;
    XftFont *const *fonts = bar_fonts(wm, style->font_style, &font_count);
    ui_draw_text(wm->display, monitor->bar_draw, fg, fonts, font_count,
                 rect.x, rect.y, (unsigned int)rect.width,
                 (unsigned int)rect.height, 0, label);
}

static void draw_workspaces(WM *wm, Monitor *monitor, Rect rect)
{
    int cursor = rect.x;
    int end = rect.x + rect.width;
    for (unsigned int i = 0; i < wm->config.workspace_count; ++i) {
        Workspace *workspace = &monitor->workspaces[i];
        unsigned int natural = workspace_item_width(wm, monitor, workspace);
        int remaining = end - cursor;
        int width = remaining > 0 && natural < (unsigned int)remaining
            ? (int)natural : remaining > 0 ? remaining : 0;
        Rect item = {cursor, 0, width, monitor->bar_geometry.height};
        monitor->bar_workspace_rects[i] = item;
        if (width > 0) {
            UIWorkspaceVisualState state = ui_workspace_visual_state(monitor,
                                                                      workspace);
            UIStyle style = workspace_style_for_state(wm, state);
            char *label = workspace_label(wm, monitor, workspace);
            if (label) {
                draw_bar_label(wm, monitor, item, &style,
                               &wm->bar_workspace_fg[state],
                               &wm->bar_workspace_bg[state], label);
                free(label);
            }
        }
        cursor += width;
    }
}

static void draw_mode(WM *wm, Monitor *monitor, Rect rect)
{
    UIModeVisualState state = mode_visual_state(monitor->active_workspace);
    UIStyle style = mode_style_for_state(wm, state);
    char *label = mode_label(wm, monitor);
    if (!label) return;
    draw_bar_label(wm, monitor, rect, &style, &wm->bar_mode_fg[state],
                   &wm->bar_mode_bg[state], label);
    free(label);
}

static void draw_title(WM *wm, Monitor *monitor, Rect rect)
{
    UIStyle style = title_style(wm);
    char *label = title_label(wm, monitor);
    if (!label) return;
    draw_bar_label(wm, monitor, rect, &style, &wm->bar_title_fg,
                   &wm->bar_title_bg, label);
    free(label);
}

static void draw_status(WM *wm, Monitor *monitor, Rect rect)
{
    UIStyle style = status_style(wm);
    char *label = status_label(wm);
    if (!label) return;
    draw_bar_label(wm, monitor, rect, &style, &wm->bar_status_fg,
                   &wm->bar_status_bg, label);
    free(label);
}

static void draw_clock(WM *wm, Monitor *monitor, Rect rect)
{
    UIStyle style = clock_style(wm);
    draw_bar_label(wm, monitor, rect, &style, &wm->bar_clock_fg,
                   &wm->bar_clock_bg, clock_label(wm));
}

void ui_bar_draw(WM *wm, Monitor *monitor)
{
    if (!wm->bar_resources_ready || !monitor || !monitor->bar_draw) return;
    XClearWindow(wm->display, monitor->bar);
    tray_prepare_layout(wm, monitor);
    layout_bar(wm, monitor);
    tray_set_allocation(wm, monitor, monitor->bar_widget_rects[UI_WIDGET_TRAY]);
    for (unsigned int widget = 0; widget < UI_WIDGET_COUNT; ++widget) {
        Rect rect = monitor->bar_widget_rects[widget];
        switch ((UIBarWidget)widget) {
        case UI_WIDGET_WORKSPACES: draw_workspaces(wm, monitor, rect); break;
        case UI_WIDGET_MODE: draw_mode(wm, monitor, rect); break;
        case UI_WIDGET_TITLE: draw_title(wm, monitor, rect); break;
        case UI_WIDGET_STATUS: draw_status(wm, monitor, rect); break;
        case UI_WIDGET_CLOCK: draw_clock(wm, monitor, rect); break;
        case UI_WIDGET_TRAY:
        case UI_WIDGET_COUNT:
            break;
        }
    }
}

Monitor *ui_bar_monitor_for_window(WM *wm, Window window)
{
    for (unsigned int i = 0; i < wm->monitor_count; ++i)
        if (wm->monitors[i].bar == window) return &wm->monitors[i];
    return NULL;
}

Workspace *ui_bar_workspace_hit_test(WM *wm, Monitor *monitor, int x)
{
    if (!wm || !monitor) return NULL;
    for (unsigned int i = 0; i < wm->config.workspace_count; ++i) {
        Rect rect = monitor->bar_workspace_rects[i];
        if (rect.width > 0 && x >= rect.x && x < rect.x + rect.width)
            return &monitor->workspaces[i];
    }
    return NULL;
}

bool ui_bar_create_monitor(WM *wm, Monitor *monitor)
{
    if (!wm->config.bar.enabled) return true;
    Rect geometry = monitor->bar_geometry;
    unsigned int width = geometry.width > 0 ? (unsigned int)geometry.width : 1U;
    unsigned int height = geometry.height > 0 ? (unsigned int)geometry.height : 1U;
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    XSetWindowAttributes attributes = {
        .override_redirect = True,
        .background_pixel = wm->bar_bg.pixel,
        .event_mask = ExposureMask | ButtonPressMask,
    };
    monitor->bar = XCreateWindow(
        wm->display, wm->root, geometry.x, geometry.y, width, height, 0,
        DefaultDepth(wm->display, wm->screen), InputOutput, visual,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    if (!monitor->bar) return false;
    ui_bar_name_monitor(wm, monitor);
    monitor->bar_draw = XftDrawCreate(
        wm->display, monitor->bar, visual,
        DefaultColormap(wm->display, wm->screen));
    if (!monitor->bar_draw) {
        XDestroyWindow(wm->display, monitor->bar);
        monitor->bar = None;
        return false;
    }
    return true;
}

void ui_bar_destroy_monitor(WM *wm, Monitor *monitor)
{
    if (monitor->bar_draw) {
        XftDrawDestroy(monitor->bar_draw);
        monitor->bar_draw = NULL;
    }
    if (monitor->bar) {
        XDestroyWindow(wm->display, monitor->bar);
        monitor->bar = None;
    }
}

void ui_bar_name_monitor(WM *wm, Monitor *monitor)
{
    if (!monitor->bar) return;
    char name[64];
    snprintf(name, sizeof(name), "box2430-bar-%u", monitor->index);
    XStoreName(wm->display, monitor->bar, name);
}

void ui_bar_update(WM *wm)
{
    if (!wm->bar_resources_ready || !wm->config.bar.enabled) return;
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        Monitor *monitor = &wm->monitors[i];
        if (!monitor->bar) continue;
        Rect geometry = monitor->bar_geometry;
        bool visible = geometry.width > 0 && geometry.height > 0;
        unsigned int width = visible ? (unsigned int)geometry.width : 1U;
        unsigned int height = visible ? (unsigned int)geometry.height : 1U;
        XMoveResizeWindow(wm->display, monitor->bar,
                          geometry.x, geometry.y, width, height);
        if (visible) {
            XMapWindow(wm->display, monitor->bar);
            ui_bar_draw(wm, monitor);
        } else {
            XUnmapWindow(wm->display, monitor->bar);
            tray_set_allocation(wm, monitor, (Rect){0});
        }
    }
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

static int tab_window_y(const WM *wm, const Monitor *monitor,
                        unsigned int height)
{
    if (wm->config.bar.position == UI_BAR_BOTTOM)
        return monitor->workarea.y + monitor->workarea.height - (int)height;
    return monitor->workarea.y;
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
        int y = tab_window_y(wm, monitor, window_height);
        XMoveResizeWindow(wm->display, monitor->tab_bar,
                          monitor->workarea.x, y,
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
    int y = tab_window_y(wm, monitor, height);
    monitor->tab_bar = XCreateWindow(
        wm->display, wm->root, monitor->workarea.x, y,
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

static void close_tab_fonts(WM *wm)
{
    for (unsigned int i = 0; i < wm->tab_font_count; ++i)
        XftFontClose(wm->display, wm->tab_fonts[i]);
    for (unsigned int i = 0; i < wm->tab_font_bold_count; ++i)
        XftFontClose(wm->display, wm->tab_fonts_bold[i]);
    wm->tab_font_count = 0;
    wm->tab_font_bold_count = 0;
}

static void close_bar_fonts(WM *wm)
{
    for (unsigned int i = 0; i < wm->bar_font_count; ++i)
        XftFontClose(wm->display, wm->bar_fonts[i]);
    for (unsigned int i = 0; i < wm->bar_font_bold_count; ++i)
        XftFontClose(wm->display, wm->bar_fonts_bold[i]);
    wm->bar_font_count = 0;
    wm->bar_font_bold_count = 0;
}

static void free_tab_colors(WM *wm)
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

static size_t bar_color_pointers(WM *wm, XftColor **colors)
{
    size_t count = 0;
    colors[count++] = &wm->bar_bg;
    for (unsigned int i = 0; i < UI_WORKSPACE_STATE_COUNT; ++i) {
        colors[count++] = &wm->bar_workspace_fg[i];
        colors[count++] = &wm->bar_workspace_bg[i];
    }
    for (unsigned int i = 0; i < UI_MODE_STATE_COUNT; ++i) {
        colors[count++] = &wm->bar_mode_fg[i];
        colors[count++] = &wm->bar_mode_bg[i];
    }
    colors[count++] = &wm->bar_title_fg;
    colors[count++] = &wm->bar_title_bg;
    colors[count++] = &wm->bar_status_fg;
    colors[count++] = &wm->bar_status_bg;
    colors[count++] = &wm->bar_clock_fg;
    colors[count++] = &wm->bar_clock_bg;
    return count;
}

static void free_color_prefix(WM *wm, XftColor **colors, size_t count)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    Colormap colormap = DefaultColormap(wm->display, wm->screen);
    for (size_t i = 0; i < count; ++i)
        XftColorFree(wm->display, visual, colormap, colors[i]);
}

static void free_bar_colors(WM *wm)
{
    XftColor *colors[1 + UI_WORKSPACE_STATE_COUNT * 2 +
                     UI_MODE_STATE_COUNT * 2 + 6];
    size_t count = bar_color_pointers(wm, colors);
    free_color_prefix(wm, colors, count);
}

static unsigned long allocate_border_color(WM *wm, const char *name,
                                           unsigned long fallback,
                                           bool *allocated)
{
    XColor color = {0};
    XColor exact = {0};
    if (XAllocNamedColor(wm->display, DefaultColormap(wm->display, wm->screen),
                         name, &color, &exact)) {
        *allocated = true;
        return color.pixel;
    }
    *allocated = false;
    return fallback;
}

static void init_border_resources(WM *wm)
{
    wm->focused_border = allocate_border_color(
        wm, wm->config.border_focused, WhitePixel(wm->display, wm->screen),
        &wm->focused_border_allocated);
    wm->unfocused_border = allocate_border_color(
        wm, wm->config.border_unfocused, BlackPixel(wm->display, wm->screen),
        &wm->unfocused_border_allocated);
    wm->urgent_border = allocate_border_color(
        wm, wm->config.border_urgent, WhitePixel(wm->display, wm->screen),
        &wm->urgent_border_allocated);
}

static void free_border_pixel(WM *wm, unsigned long pixel, bool *allocated)
{
    if (!*allocated) return;
    XFreeColors(wm->display, DefaultColormap(wm->display, wm->screen),
                &pixel, 1, 0);
    *allocated = false;
}

static void free_border_resources(WM *wm)
{
    free_border_pixel(wm, wm->focused_border, &wm->focused_border_allocated);
    free_border_pixel(wm, wm->unfocused_border, &wm->unfocused_border_allocated);
    free_border_pixel(wm, wm->urgent_border, &wm->urgent_border_allocated);
}

void ui_client_border_refresh(WM *wm, Client *client)
{
    if (!wm || !client) return;
    unsigned long pixel = client == wm->focused_client ? wm->focused_border
        : client->urgent ? wm->urgent_border : wm->unfocused_border;
    XSetWindowBorder(wm->display, client->window, pixel);
}

static bool init_tab_resources(WM *wm)
{
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    Colormap colormap = DefaultColormap(wm->display, wm->screen);
    wm->tab_font_count = load_fonts(wm, wm->config.tabs.font, false,
                                    wm->tab_fonts);
    wm->tab_font_bold_count = load_fonts(wm, wm->config.tabs.font_bold, true,
                                         wm->tab_fonts_bold);
    if (!wm->tab_font_count || !wm->tab_font_bold_count) {
        fprintf(stderr, "box2430: cannot open configured tab bar fonts\n");
        close_tab_fonts(wm);
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
    size_t allocated = 0;
    size_t color_count = sizeof(colors) / sizeof(colors[0]);
    for (; allocated < color_count; ++allocated) {
        if (!XftColorAllocName(wm->display, visual, colormap,
                               names[allocated], colors[allocated])) {
            fprintf(stderr, "box2430: cannot allocate tab bar color %s\n",
                    names[allocated]);
            free_color_prefix(wm, colors, allocated);
            close_tab_fonts(wm);
            return false;
        }
    }
    return true;
}

static bool init_bar_resources(WM *wm)
{
    if (!wm->config.bar.enabled) return true;
    Visual *visual = DefaultVisual(wm->display, wm->screen);
    Colormap colormap = DefaultColormap(wm->display, wm->screen);
    wm->bar_font_count = load_fonts(wm, wm->config.bar.font, false,
                                    wm->bar_fonts);
    wm->bar_font_bold_count = load_fonts(wm, wm->config.bar.font_bold, true,
                                         wm->bar_fonts_bold);
    if (!wm->bar_font_count || !wm->bar_font_bold_count) {
        fprintf(stderr, "box2430: cannot open configured native bar fonts\n");
        close_bar_fonts(wm);
        return false;
    }

    UIStyle workspace_styles[UI_WORKSPACE_STATE_COUNT];
    for (unsigned int i = 0; i < UI_WORKSPACE_STATE_COUNT; ++i)
        workspace_styles[i] = workspace_style_for_state(
            wm, (UIWorkspaceVisualState)i);
    UIStyle mode_styles[UI_MODE_STATE_COUNT];
    for (unsigned int i = 0; i < UI_MODE_STATE_COUNT; ++i)
        mode_styles[i] = mode_style_for_state(wm, (UIModeVisualState)i);
    UIStyle title = title_style(wm);
    UIStyle status = status_style(wm);
    UIStyle clock = clock_style(wm);

    XftColor *colors[1 + UI_WORKSPACE_STATE_COUNT * 2 +
                     UI_MODE_STATE_COUNT * 2 + 6];
    const char *names[1 + UI_WORKSPACE_STATE_COUNT * 2 +
                      UI_MODE_STATE_COUNT * 2 + 6];
    size_t count = bar_color_pointers(wm, colors);
    size_t n = 0;
    names[n++] = wm->config.bar.style.bg;
    for (unsigned int i = 0; i < UI_WORKSPACE_STATE_COUNT; ++i) {
        names[n++] = workspace_styles[i].fg;
        names[n++] = workspace_styles[i].bg;
    }
    for (unsigned int i = 0; i < UI_MODE_STATE_COUNT; ++i) {
        names[n++] = mode_styles[i].fg;
        names[n++] = mode_styles[i].bg;
    }
    names[n++] = title.fg;
    names[n++] = title.bg;
    names[n++] = status.fg;
    names[n++] = status.bg;
    names[n++] = clock.fg;
    names[n++] = clock.bg;
    if (n != count) {
        close_bar_fonts(wm);
        return false;
    }

    size_t allocated = 0;
    for (; allocated < count; ++allocated) {
        if (!XftColorAllocName(wm->display, visual, colormap,
                               names[allocated], colors[allocated])) {
            fprintf(stderr, "box2430: cannot allocate native bar color %s\n",
                    names[allocated]);
            free_color_prefix(wm, colors, allocated);
            close_bar_fonts(wm);
            return false;
        }
    }
    return true;
}

bool ui_init(WM *wm)
{
    init_border_resources(wm);
    ui_status_refresh(wm);
    update_clock_text(wm);
    if (!init_tab_resources(wm)) {
        free_border_resources(wm);
        free(wm->status_text);
        wm->status_text = NULL;
        return false;
    }
    if (!init_bar_resources(wm)) {
        free_tab_colors(wm);
        close_tab_fonts(wm);
        free_border_resources(wm);
        free(wm->status_text);
        wm->status_text = NULL;
        return false;
    }

    if (wm->config.bar.enabled) {
        unsigned int created = 0;
        for (; created < wm->monitor_count; ++created) {
            if (!ui_bar_create_monitor(wm, &wm->monitors[created])) {
                fprintf(stderr, "box2430: cannot create native bar window\n");
                for (unsigned int i = 0; i < created; ++i)
                    ui_bar_destroy_monitor(wm, &wm->monitors[i]);
                free_bar_colors(wm);
                close_bar_fonts(wm);
                free_tab_colors(wm);
                close_tab_fonts(wm);
                free_border_resources(wm);
                free(wm->status_text);
                wm->status_text = NULL;
                return false;
            }
        }
        wm->bar_resources_ready = true;
    }

    unsigned int created = 0;
    for (; created < wm->monitor_count; ++created) {
        if (!ui_tab_create_monitor(wm, &wm->monitors[created])) {
            fprintf(stderr, "box2430: cannot create tab bar drawing context\n");
            for (unsigned int i = 0; i < created; ++i)
                ui_tab_destroy_monitor(wm, &wm->monitors[i]);
            if (wm->bar_resources_ready) {
                for (unsigned int i = 0; i < wm->monitor_count; ++i)
                    ui_bar_destroy_monitor(wm, &wm->monitors[i]);
                wm->bar_resources_ready = false;
                free_bar_colors(wm);
                close_bar_fonts(wm);
            }
            free_tab_colors(wm);
            close_tab_fonts(wm);
            free_border_resources(wm);
            free(wm->status_text);
            wm->status_text = NULL;
            return false;
        }
    }
    wm->tab_resources_ready = true;
    ui_update(wm);
    return true;
}

void ui_update(WM *wm)
{
    ui_bar_update(wm);
    ui_tab_update(wm);
}

void ui_destroy(WM *wm)
{
    if (!wm->display) return;
    for (unsigned int i = 0; i < wm->monitor_count; ++i) {
        ui_bar_destroy_monitor(wm, &wm->monitors[i]);
        ui_tab_destroy_monitor(wm, &wm->monitors[i]);
    }
    if (wm->bar_resources_ready) {
        free_bar_colors(wm);
        close_bar_fonts(wm);
        wm->bar_resources_ready = false;
    }
    if (wm->tab_resources_ready) {
        free_tab_colors(wm);
        wm->tab_resources_ready = false;
    }
    close_tab_fonts(wm);
    free_border_resources(wm);
    free(wm->status_text);
    wm->status_text = NULL;
}
