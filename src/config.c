#include "box2430.h"

#include "../vendor/tomlc17/tomlc17.h"

#include <X11/keysym.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_default_binding(Config *config, unsigned int modifiers,
                                KeySym symbol, int argc,
                                const char *const *argv)
{
    if (config->key_binding_count >= BOX2430_MAX_KEY_BINDINGS) return;
    KeyBinding *binding = &config->key_bindings[config->key_binding_count++];
    binding->modifiers = modifiers;
    binding->symbol = symbol;
    binding->argc = argc;
    for (int i = 0; i < argc; ++i)
        snprintf(binding->argv[i], sizeof(binding->argv[i]), "%s", argv[i]);
}

static void set_default_bindings(Config *config)
{
    static const char *spawn[] = {"spawn", "kitty"};
    static const char *close[] = {"window", "close"};
    static const char *next[] = {"focus", "next"};
    static const char *mode[] = {"mode", "monocle", "toggle"};
    static const char *prev[] = {"focus", "prev"};
    static const char *snap_left[] = {"snap", "left"};
    static const char *snap_right[] = {"snap", "right"};
    static const char *maximize[] = {"maximize", "toggle"};
    static const char *fullscreen[] = {"fullscreen", "toggle"};
    static const char *monitor_prev[] = {"monitor", "prev"};
    static const char *monitor_next[] = {"monitor", "next"};
    add_default_binding(config, Mod4Mask, XK_Return, 2, spawn);
    add_default_binding(config, Mod4Mask, XK_q, 2, close);
    add_default_binding(config, Mod1Mask, XK_Tab, 2, next);
    add_default_binding(config, Mod4Mask, XK_m, 3, mode);
    add_default_binding(config, Mod4Mask, XK_j, 2, next);
    add_default_binding(config, Mod4Mask, XK_k, 2, prev);
    add_default_binding(config, Mod4Mask, XK_Left, 2, snap_left);
    add_default_binding(config, Mod4Mask, XK_Right, 2, snap_right);
    add_default_binding(config, Mod4Mask, XK_Up, 2, maximize);
    add_default_binding(config, Mod4Mask, XK_f, 2, fullscreen);
    add_default_binding(config, Mod4Mask | ControlMask, XK_Left, 2, monitor_prev);
    add_default_binding(config, Mod4Mask | ControlMask, XK_Right, 2, monitor_next);
    for (unsigned int number = 1; number <= 9; ++number) {
        char workspace[2] = {(char)('0' + number), '\0'};
        const char *focus[] = {"workspace", workspace};
        const char *move[] = {"window", "move-workspace", workspace};
        add_default_binding(config, Mod4Mask, XStringToKeysym(workspace), 2, focus);
        add_default_binding(config, Mod4Mask | ShiftMask,
                            XStringToKeysym(workspace), 3, move);
    }
    MouseBinding *move = &config->mouse_bindings[config->mouse_binding_count++];
    *move = (MouseBinding){.modifiers = Mod4Mask, .button = Button1, .argc = 2};
    strcpy(move->argv[0], "mouse");
    strcpy(move->argv[1], "move-window");
    MouseBinding *resize = &config->mouse_bindings[config->mouse_binding_count++];
    *resize = (MouseBinding){.modifiers = Mod4Mask, .button = Button3, .argc = 2};
    strcpy(resize->argv[0], "mouse");
    strcpy(resize->argv[1], "resize-window");
    static const struct {
        unsigned int button;
        const char *first;
        const char *second;
    } tabs[] = {
        {Button1, "tab", "focus"}, {Button2, "tab", "close"},
        {Button4, "focus", "prev"}, {Button5, "focus", "next"},
    };
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i) {
        MouseBinding *binding = &config->tab_bindings[config->tab_binding_count++];
        *binding = (MouseBinding){.button = tabs[i].button, .argc = 2};
        strcpy(binding->argv[0], tabs[i].first);
        strcpy(binding->argv[1], tabs[i].second);
    }
}

static void set_label_format(UILabelFormat *format, const char *prefix,
                             const char *suffix)
{
    snprintf(format->prefix, sizeof(format->prefix), "%s", prefix);
    snprintf(format->suffix, sizeof(format->suffix), "%s", suffix);
}

static void set_style(UIStyle *style, const char *fg, const char *bg,
                      UIFontStyle font_style, const char *prefix,
                      const char *suffix)
{
    memcpy(style->fg, fg, 8);
    memcpy(style->bg, bg, 8);
    style->font_style = font_style;
    set_label_format(&style->format, prefix, suffix);
}

static void set_style_override(UIStyleOverride *style, const char *fg,
                               const char *bg, UIFontStyle font_style,
                               const char *prefix, const char *suffix)
{
    style->has_fg = fg != NULL;
    style->has_bg = bg != NULL;
    style->has_font_style = true;
    style->has_format = prefix != NULL && suffix != NULL;
    if (fg) memcpy(style->fg, fg, 8);
    if (bg) memcpy(style->bg, bg, 8);
    style->font_style = font_style;
    if (style->has_format) set_label_format(&style->format, prefix, suffix);
}

void config_set_defaults(Config *config)
{
    *config = (Config){
        .workspace_count = BOX2430_DEFAULT_WORKSPACE_COUNT,
        .focus_mode = FOCUS_CLICK,
        .active_window_policy = ACTIVE_WINDOW_URGENT,
        .raise_on_focus = false,
        .focus_on_map = true,
        .raise_on_map = true,
        .normal_placement = PLACEMENT_CENTER,
        .dialog_placement = PLACEMENT_CENTER,
        .client_fullscreen_policy = CLIENT_FULLSCREEN_FAKE,
        .border = {
            .free = {.width = 2},
            .monocle = {.width = 0},
        },
        .snap_preview_width = 2,
        .snap_enabled = true,
        .snap_edge_zone = 16,
        .snap_preview = true,
        .snap_side_ratio = 0.5,
        .snap_corner_width_ratio = 0.5,
        .snap_corner_height_ratio = 0.5,
        .tabs = {
            .enabled = true,
            .position = UI_EDGE_TOP,
            .height = 24,
            .padding = 8,
            .source = UI_LABEL_TITLE,
        },
        .bar = {
            .enabled = true,
            .position = UI_EDGE_TOP,
            .height = 24,
            .padding = 8,
            .gap = 8,
            .left = {UI_WIDGET_WORKSPACES, UI_WIDGET_MODE},
            .left_count = 2,
            .center = {UI_WIDGET_TITLE},
            .center_count = 1,
            .right = {UI_WIDGET_STATUS, UI_WIDGET_CLOCK, UI_WIDGET_TRAY},
            .right_count = 3,
            .title = {.source = UI_LABEL_TITLE},
        },
        .inherit_default_bindings = true,
    };
    memcpy(config->background, "#000000", 8);
    memcpy(config->border.free.focused, "#89b4fa", 8);
    memcpy(config->border.free.unfocused, "#45475a", 8);
    memcpy(config->border.free.urgent, "#f38ba8", 8);
    memcpy(config->border.monocle.focused, "#89b4fa", 8);
    memcpy(config->border.monocle.unfocused, "#45475a", 8);
    memcpy(config->border.monocle.urgent, "#f38ba8", 8);
    memcpy(config->snap_preview_color, "#89b4fa", 8);

    snprintf(config->tabs.font, sizeof(config->tabs.font), "monospace:size=10");
    snprintf(config->tabs.font_bold, sizeof(config->tabs.font_bold),
             "monospace:style=Bold:size=10");
    set_style(&config->tabs.style, "#aaaaaa", "#222222", UI_FONT_NORMAL,
              "", "");
    set_style_override(&config->tabs.active, "#ffffff", "#3b4252",
                       UI_FONT_BOLD, NULL, NULL);
    set_style_override(&config->tabs.urgent, "#ffffff", "#bf616a",
                       UI_FONT_BOLD, NULL, NULL);

    snprintf(config->bar.font, sizeof(config->bar.font), "monospace:size=10");
    snprintf(config->bar.font_bold, sizeof(config->bar.font_bold),
             "monospace:style=Bold:size=10");
    set_style(&config->bar.style, "#aaaaaa", "#222222", UI_FONT_NORMAL,
              "", "");

    set_style_override(&config->bar.workspaces.style, NULL, NULL,
                       UI_FONT_NORMAL, " ", " ");
    set_style_override(&config->bar.workspaces.empty, "#666666", "#222222",
                       UI_FONT_NORMAL, " ", " ");
    set_style_override(&config->bar.workspaces.occupied, "#aaaaaa", "#222222",
                       UI_FONT_NORMAL, " ", " ");
    set_style_override(&config->bar.workspaces.active, "#ffffff", "#3b4252",
                       UI_FONT_BOLD, "[ ", " ]");
    set_style_override(&config->bar.workspaces.urgent, "#ffffff", "#bf616a",
                       UI_FONT_BOLD, "! ", " !");
    set_style_override(&config->bar.workspaces.active_urgent,
                       "#ffffff", "#bf616a", UI_FONT_BOLD, "[! ", " !]");

    set_style_override(&config->bar.mode.style, NULL, NULL,
                       UI_FONT_NORMAL, "", "");
    snprintf(config->bar.mode.free.label, sizeof(config->bar.mode.free.label), "F");
    set_style_override(&config->bar.mode.free.style, NULL, NULL,
                       UI_FONT_NORMAL, "[ ", " ]");
    snprintf(config->bar.mode.monocle.label,
             sizeof(config->bar.mode.monocle.label), "M");
    set_style_override(&config->bar.mode.monocle.style, NULL, NULL,
                       UI_FONT_BOLD, "[ ", " ]");

    set_style_override(&config->bar.title.style, NULL, NULL,
                       UI_FONT_NORMAL, "", "");
    set_style_override(&config->bar.status.style, NULL, NULL,
                       UI_FONT_NORMAL, "", "");
    snprintf(config->bar.clock.format, sizeof(config->bar.clock.format), "%%H:%%M");
    config->bar.clock.style.has_font_style = true;
    config->bar.clock.style.font_style = UI_FONT_NORMAL;

    set_default_bindings(config);
}

static bool key_allowed(const char *key, const char *const *allowed, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (strcmp(key, allowed[i]) == 0) return true;
    return false;
}

static bool validate_keys(toml_datum_t table, const char *prefix,
                          const char *const *allowed, size_t count)
{
    if (table.type == TOML_UNKNOWN) return true;
    if (table.type != TOML_TABLE) {
        fprintf(stderr, "box2430: config option %s must be a table\n", prefix);
        return false;
    }
    for (int32_t i = 0; i < table.u.tab.size; ++i) {
        if (!key_allowed(table.u.tab.key[i], allowed, count)) {
            fprintf(stderr, "box2430: unknown config option %s.%s\n",
                    prefix, table.u.tab.key[i]);
            return false;
        }
    }
    return true;
}

static bool read_bool(toml_datum_t table, const char *prefix, const char *key,
                      bool *value)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_BOOLEAN) {
        fprintf(stderr, "box2430: config option %s.%s must be boolean\n", prefix, key);
        return false;
    }
    *value = datum.u.boolean;
    return true;
}

static bool read_uint(toml_datum_t table, const char *prefix, const char *key,
                      unsigned int minimum, unsigned int maximum,
                      unsigned int *value)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_INT64 || datum.u.int64 < minimum ||
        datum.u.int64 > maximum) {
        fprintf(stderr, "box2430: config option %s.%s must be in %u..%u\n",
                prefix, key, minimum, maximum);
        return false;
    }
    *value = (unsigned int)datum.u.int64;
    return true;
}

static bool read_ratio(toml_datum_t table, const char *key, double *value)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    double number;
    if (datum.type == TOML_FP64) number = datum.u.fp64;
    else if (datum.type == TOML_INT64) number = (double)datum.u.int64;
    else number = -1.0;
    if (number <= 0.0 || number >= 1.0) {
        fprintf(stderr, "box2430: config option snap.%s must be between 0 and 1\n", key);
        return false;
    }
    *value = number;
    return true;
}

static bool read_enum(toml_datum_t table, const char *prefix, const char *key,
                      const char *const *values, size_t count, unsigned int *selected)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type == TOML_STRING) {
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(datum.u.s, values[i]) == 0) {
                *selected = (unsigned int)i;
                return true;
            }
        }
    }
    fprintf(stderr, "box2430: invalid value for config option %s.%s\n", prefix, key);
    return false;
}

static bool read_ui_edge(toml_datum_t table, const char *prefix, const char *key,
                         UIEdge *edge)
{
    static const char *values[] = {"top", "bottom"};
    unsigned int choice = (unsigned int)*edge;
    if (!read_enum(table, prefix, key, values, 2, &choice)) return false;
    *edge = (UIEdge)choice;
    return true;
}

static bool read_color(toml_datum_t table, const char *prefix, const char *key,
                       char output[8])
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_STRING || strlen(datum.u.s) != 7 || datum.u.s[0] != '#') {
        fprintf(stderr, "box2430: %s.%s must be #RRGGBB\n", prefix, key);
        return false;
    }
    for (size_t i = 1; i < 7; ++i) {
        char c = datum.u.s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            fprintf(stderr, "box2430: %s.%s must be #RRGGBB\n", prefix, key);
            return false;
        }
    }
    memcpy(output, datum.u.s, 8);
    return true;
}

static bool read_text(toml_datum_t table, const char *prefix, const char *key,
                      char *output, size_t capacity)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_STRING || !datum.u.s[0] || strlen(datum.u.s) >= capacity) {
        fprintf(stderr, "box2430: config option %s.%s must be a non-empty string\n",
                prefix, key);
        return false;
    }
    strcpy(output, datum.u.s);
    return true;
}

static bool read_label_format(toml_datum_t table, const char *prefix,
                              const char *key, UILabelFormat *format)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_STRING) {
        fprintf(stderr, "box2430: config option %s.%s must be a string with exactly one %%s\n",
                prefix, key);
        return false;
    }
    const char *placeholder = strstr(datum.u.s, "%s");
    if (!placeholder || strstr(placeholder + 2, "%s")) {
        fprintf(stderr, "box2430: config option %s.%s must contain exactly one %%s\n",
                prefix, key);
        return false;
    }
    size_t prefix_length = (size_t)(placeholder - datum.u.s);
    size_t suffix_length = strlen(placeholder + 2);
    if (prefix_length >= sizeof(format->prefix) ||
        suffix_length >= sizeof(format->suffix)) {
        fprintf(stderr, "box2430: config option %s.%s is too long\n", prefix, key);
        return false;
    }
    memcpy(format->prefix, datum.u.s, prefix_length);
    format->prefix[prefix_length] = '\0';
    memcpy(format->suffix, placeholder + 2, suffix_length + 1);
    return true;
}

static bool read_label_source(toml_datum_t table, const char *prefix,
                              const char *key, UILabelSource *source)
{
    static const char *sources[] = {"title", "class", "instance"};
    unsigned int choice = (unsigned int)*source;
    if (!read_enum(table, prefix, key, sources, 3, &choice)) return false;
    *source = (UILabelSource)choice;
    return true;
}

static bool read_font_style(toml_datum_t table, const char *prefix,
                            const char *key, UIFontStyle *style)
{
    static const char *styles[] = {"normal", "bold"};
    unsigned int choice = (unsigned int)*style;
    if (!read_enum(table, prefix, key, styles, 2, &choice)) return false;
    *style = (UIFontStyle)choice;
    return true;
}

static bool read_style_override(toml_datum_t table, const char *prefix,
                                UIStyleOverride *style, bool allow_format)
{
    toml_datum_t datum = toml_get(table, "fg");
    if (datum.type != TOML_UNKNOWN) {
        if (!read_color(table, prefix, "fg", style->fg)) return false;
        style->has_fg = true;
    }
    datum = toml_get(table, "bg");
    if (datum.type != TOML_UNKNOWN) {
        if (!read_color(table, prefix, "bg", style->bg)) return false;
        style->has_bg = true;
    }
    datum = toml_get(table, "font_style");
    if (datum.type != TOML_UNKNOWN) {
        if (!read_font_style(table, prefix, "font_style", &style->font_style))
            return false;
        style->has_font_style = true;
    }
    datum = toml_get(table, "format");
    if (datum.type != TOML_UNKNOWN && allow_format) {
        if (!read_label_format(table, prefix, "format", &style->format)) return false;
        style->has_format = true;
    }
    return true;
}

static bool read_base_style(toml_datum_t table, const char *prefix, UIStyle *style)
{
    return read_color(table, prefix, "fg", style->fg) &&
           read_color(table, prefix, "bg", style->bg) &&
           read_font_style(table, prefix, "font_style", &style->font_style) &&
           read_label_format(table, prefix, "format", &style->format);
}

static bool parse_style_state(toml_datum_t parent, const char *name,
                              const char *prefix, UIStyleOverride *style)
{
    toml_datum_t table = toml_get(parent, name);
    static const char *keys[] = {"fg", "bg", "font_style", "format"};
    char path[128];
    snprintf(path, sizeof(path), "%s.%s", prefix, name);
    return validate_keys(table, path, keys, 4) &&
           read_style_override(table, path, style, true);
}

static int bar_widget_from_name(const char *name)
{
    static const char *names[] = {
        "workspaces", "mode", "title", "status", "clock", "tray",
    };
    for (int i = 0; i < UI_WIDGET_COUNT; ++i)
        if (strcmp(name, names[i]) == 0) return i;
    return -1;
}

static bool read_widget_list(toml_datum_t bar, const char *key,
                             UIBarWidget output[BOX2430_MAX_BAR_WIDGETS],
                             unsigned int *count)
{
    toml_datum_t datum = toml_get(bar, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_ARRAY || datum.u.arr.size > BOX2430_MAX_BAR_WIDGETS) {
        fprintf(stderr, "box2430: config option appearance.bar.%s must be an array of at most %d widget names\n",
                key, BOX2430_MAX_BAR_WIDGETS);
        return false;
    }
    *count = 0;
    for (int32_t i = 0; i < datum.u.arr.size; ++i) {
        toml_datum_t item = datum.u.arr.elem[i];
        if (item.type != TOML_STRING) {
            fprintf(stderr, "box2430: config option appearance.bar.%s must contain only widget names\n",
                    key);
            return false;
        }
        int widget = bar_widget_from_name(item.u.s);
        if (widget < 0) {
            fprintf(stderr, "box2430: unknown bar widget %s\n", item.u.s);
            return false;
        }
        output[(*count)++] = (UIBarWidget)widget;
    }
    return true;
}

static bool validate_widget_uniqueness(const BarConfig *bar)
{
    bool seen[UI_WIDGET_COUNT] = {false};
    const UIBarWidget *lists[] = {bar->left, bar->center, bar->right};
    const unsigned int counts[] = {
        bar->left_count, bar->center_count, bar->right_count,
    };
    for (size_t list = 0; list < 3; ++list) {
        for (unsigned int i = 0; i < counts[list]; ++i) {
            UIBarWidget widget = lists[list][i];
            if (seen[widget]) {
                static const char *names[] = {
                    "workspaces", "mode", "title", "status", "clock", "tray",
                };
                fprintf(stderr, "box2430: duplicate bar widget %s\n", names[widget]);
                return false;
            }
            seen[widget] = true;
        }
    }
    return true;
}

static bool parse_key_spec(const char *spec, unsigned int *modifiers, KeySym *symbol)
{
    char copy[128];
    if (strlen(spec) >= sizeof(copy)) return false;
    strcpy(copy, spec);
    *modifiers = 0;
    char *part = copy;
    for (;;) {
        char *plus = strchr(part, '+');
        if (!plus) break;
        *plus = '\0';
        unsigned int modifier;
        if (strcmp(part, "Super") == 0) modifier = Mod4Mask;
        else if (strcmp(part, "Shift") == 0) modifier = ShiftMask;
        else if (strcmp(part, "Ctrl") == 0) modifier = ControlMask;
        else if (strcmp(part, "Alt") == 0) modifier = Mod1Mask;
        else return false;
        if (*modifiers & modifier) return false;
        *modifiers |= modifier;
        part = plus + 1;
    }
    if (!*part) return false;
    *symbol = XStringToKeysym(part);
    return *symbol != NoSymbol;
}

static bool parse_mouse_spec(const char *spec, unsigned int *modifiers,
                             unsigned int *button)
{
    char copy[128];
    if (strlen(spec) >= sizeof(copy)) return false;
    strcpy(copy, spec);
    *modifiers = 0;
    char *part = copy;
    for (;;) {
        char *plus = strchr(part, '+');
        if (!plus) break;
        *plus = '\0';
        unsigned int modifier;
        if (strcmp(part, "Super") == 0) modifier = Mod4Mask;
        else if (strcmp(part, "Shift") == 0) modifier = ShiftMask;
        else if (strcmp(part, "Ctrl") == 0) modifier = ControlMask;
        else if (strcmp(part, "Alt") == 0) modifier = Mod1Mask;
        else return false;
        if (*modifiers & modifier) return false;
        *modifiers |= modifier;
        part = plus + 1;
    }
    if (strncmp(part, "Button", 6) != 0 || part[6] < '1' || part[6] > '5' || part[7])
        return false;
    *button = (unsigned int)(part[6] - '0');
    return true;
}

static bool parse_tab_spec(const char *spec, unsigned int *button)
{
    unsigned int modifiers = 0;
    if (strcmp(spec, "WheelUp") == 0) { *button = Button4; return true; }
    if (strcmp(spec, "WheelDown") == 0) { *button = Button5; return true; }
    return parse_mouse_spec(spec, &modifiers, button) && modifiers == 0;
}

static bool parse_command_text(const char *text, KeyBinding *binding)
{
    binding->argc = 0;
    const char *cursor = text;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (!*cursor) break;
        if (binding->argc >= BOX2430_MAX_COMMAND_ARGS) return false;
        char *output = binding->argv[binding->argc];
        size_t length = 0;
        char quote = 0;
        if (*cursor == '\'' || *cursor == '"') quote = *cursor++;
        while (*cursor && (quote ? *cursor != quote : *cursor != ' ' && *cursor != '\t')) {
            if (*cursor == '\\' && cursor[1]) ++cursor;
            if (length + 1 >= BOX2430_MAX_COMMAND_ARG_LENGTH) return false;
            output[length++] = *cursor++;
        }
        if (quote) {
            if (*cursor != quote) return false;
            ++cursor;
            if (*cursor && *cursor != ' ' && *cursor != '\t') return false;
        }
        output[length] = '\0';
        ++binding->argc;
    }
    return binding->argc > 0;
}

static void remove_binding(Config *config, unsigned int modifiers, KeySym symbol)
{
    for (unsigned int i = 0; i < config->key_binding_count; ++i) {
        KeyBinding *binding = &config->key_bindings[i];
        if (binding->modifiers == modifiers && binding->symbol == symbol) {
            memmove(binding, binding + 1,
                    (config->key_binding_count - i - 1) * sizeof(*binding));
            --config->key_binding_count;
            return;
        }
    }
}

static bool parse_key_bindings(Config *candidate, toml_datum_t keys)
{
    if (keys.type == TOML_UNKNOWN) return true;
    if (keys.type != TOML_TABLE) {
        fprintf(stderr, "box2430: config option bindings.keys must be a table\n");
        return false;
    }
    for (int32_t i = 0; i < keys.u.tab.size; ++i) {
        toml_datum_t value = keys.u.tab.value[i];
        unsigned int modifiers;
        KeySym symbol;
        if (!parse_key_spec(keys.u.tab.key[i], &modifiers, &symbol)) {
            fprintf(stderr, "box2430: invalid key binding %s\n", keys.u.tab.key[i]);
            return false;
        }
        if (value.type != TOML_STRING) {
            fprintf(stderr, "box2430: binding %s must be a command string\n",
                    keys.u.tab.key[i]);
            return false;
        }
        remove_binding(candidate, modifiers, symbol);
        if (strcmp(value.u.s, "none") == 0) continue;
        if (candidate->key_binding_count >= BOX2430_MAX_KEY_BINDINGS) {
            fprintf(stderr, "box2430: too many key bindings\n");
            return false;
        }
        KeyBinding binding = {.modifiers = modifiers, .symbol = symbol};
        if (!parse_command_text(value.u.s, &binding)) {
            fprintf(stderr, "box2430: invalid command syntax for binding %s\n",
                    keys.u.tab.key[i]);
            return false;
        }
        const char *argv[BOX2430_MAX_COMMAND_ARGS];
        for (int j = 0; j < binding.argc; ++j) argv[j] = binding.argv[j];
        if (!command_validate(candidate, COMMAND_CONTEXT_KEYBOARD,
                              binding.argc, argv)) {
            fprintf(stderr, "box2430: invalid command for binding %s\n",
                    keys.u.tab.key[i]);
            return false;
        }
        candidate->key_bindings[candidate->key_binding_count++] = binding;
    }
    return true;
}

static void remove_mouse_binding(Config *config, unsigned int modifiers,
                                 unsigned int button)
{
    for (unsigned int i = 0; i < config->mouse_binding_count; ++i) {
        MouseBinding *binding = &config->mouse_bindings[i];
        if (binding->modifiers == modifiers && binding->button == button) {
            memmove(binding, binding + 1,
                    (config->mouse_binding_count - i - 1) * sizeof(*binding));
            --config->mouse_binding_count;
            return;
        }
    }
}

static bool parse_mouse_bindings(Config *candidate, toml_datum_t mouse)
{
    if (mouse.type == TOML_UNKNOWN) return true;
    if (mouse.type != TOML_TABLE) {
        fprintf(stderr, "box2430: config option bindings.mouse must be a table\n");
        return false;
    }
    for (int32_t i = 0; i < mouse.u.tab.size; ++i) {
        toml_datum_t value = mouse.u.tab.value[i];
        unsigned int modifiers, button;
        if (!parse_mouse_spec(mouse.u.tab.key[i], &modifiers, &button) ||
            value.type != TOML_STRING) {
            fprintf(stderr, "box2430: invalid mouse binding %s\n", mouse.u.tab.key[i]);
            return false;
        }
        remove_mouse_binding(candidate, modifiers, button);
        if (strcmp(value.u.s, "none") == 0) continue;
        if (candidate->mouse_binding_count >= 16) {
            fprintf(stderr, "box2430: too many mouse bindings\n");
            return false;
        }
        KeyBinding parsed = {0};
        if (!parse_command_text(value.u.s, &parsed)) return false;
        const char *argv[BOX2430_MAX_COMMAND_ARGS];
        for (int j = 0; j < parsed.argc; ++j) argv[j] = parsed.argv[j];
        if (!command_validate(candidate, COMMAND_CONTEXT_MOUSE, parsed.argc, argv)) {
            fprintf(stderr, "box2430: invalid command for mouse binding %s\n",
                    mouse.u.tab.key[i]);
            return false;
        }
        MouseBinding *binding = &candidate->mouse_bindings[candidate->mouse_binding_count++];
        binding->modifiers = modifiers;
        binding->button = button;
        binding->argc = parsed.argc;
        for (int j = 0; j < parsed.argc; ++j) strcpy(binding->argv[j], parsed.argv[j]);
    }
    return true;
}

static bool parse_tab_bindings(Config *candidate, toml_datum_t table)
{
    if (table.type == TOML_UNKNOWN) return true;
    if (table.type != TOML_TABLE) {
        fprintf(stderr, "box2430: config option bindings.tabbar must be a table\n");
        return false;
    }
    for (int32_t i = 0; i < table.u.tab.size; ++i) {
        toml_datum_t value = table.u.tab.value[i];
        unsigned int button;
        if (!parse_tab_spec(table.u.tab.key[i], &button) || value.type != TOML_STRING) {
            fprintf(stderr, "box2430: invalid tabbar binding %s\n", table.u.tab.key[i]);
            return false;
        }
        for (unsigned int j = 0; j < candidate->tab_binding_count; ++j) {
            if (candidate->tab_bindings[j].button == button) {
                memmove(&candidate->tab_bindings[j], &candidate->tab_bindings[j + 1],
                        (candidate->tab_binding_count - j - 1) *
                        sizeof(candidate->tab_bindings[j]));
                --candidate->tab_binding_count;
                break;
            }
        }
        if (strcmp(value.u.s, "none") == 0) continue;
        if (candidate->tab_binding_count >= 16) return false;
        KeyBinding parsed = {0};
        if (!parse_command_text(value.u.s, &parsed)) return false;
        const char *argv[BOX2430_MAX_COMMAND_ARGS];
        for (int j = 0; j < parsed.argc; ++j) argv[j] = parsed.argv[j];
        if (!command_validate(candidate, COMMAND_CONTEXT_TABBAR, parsed.argc, argv)) {
            fprintf(stderr, "box2430: invalid command for tabbar binding %s\n",
                    table.u.tab.key[i]);
            return false;
        }
        MouseBinding *binding = &candidate->tab_bindings[candidate->tab_binding_count++];
        *binding = (MouseBinding){.button = button, .argc = parsed.argc};
        for (int j = 0; j < parsed.argc; ++j) strcpy(binding->argv[j], parsed.argv[j]);
    }
    return true;
}

static void prune_invalid_default_bindings(Config *candidate)
{
    for (unsigned int i = 0; i < candidate->key_binding_count;) {
        KeyBinding *binding = &candidate->key_bindings[i];
        const char *argv[BOX2430_MAX_COMMAND_ARGS];
        for (int j = 0; j < binding->argc; ++j) argv[j] = binding->argv[j];
        if (command_validate(candidate, COMMAND_CONTEXT_KEYBOARD,
                             binding->argc, argv)) {
            ++i;
        } else {
            memmove(binding, binding + 1,
                    (candidate->key_binding_count - i - 1) * sizeof(*binding));
            --candidate->key_binding_count;
        }
    }
}

static bool read_rule_pattern(toml_datum_t table, const char *key, bool *present,
                              char output[BOX2430_MAX_RULE_PATTERN])
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_STRING || !datum.u.s[0] ||
        strlen(datum.u.s) >= BOX2430_MAX_RULE_PATTERN) {
        fprintf(stderr, "box2430: rule.%s must be a non-empty string shorter than %d bytes\n",
                key, BOX2430_MAX_RULE_PATTERN);
        return false;
    }
    *present = true;
    strcpy(output, datum.u.s);
    return true;
}

static bool read_rule_bool(toml_datum_t table, const char *key, bool *present,
                           bool *value)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_BOOLEAN) {
        fprintf(stderr, "box2430: rule.%s must be boolean\n", key);
        return false;
    }
    *present = true;
    *value = datum.u.boolean;
    return true;
}

static bool read_rule_uint(toml_datum_t table, const char *key, unsigned int maximum,
                           bool *present, unsigned int *value)
{
    toml_datum_t datum = toml_get(table, key);
    if (datum.type == TOML_UNKNOWN) return true;
    if (datum.type != TOML_INT64 || datum.u.int64 < 1 || datum.u.int64 > maximum) {
        fprintf(stderr, "box2430: rule.%s must be in 1..%u\n", key, maximum);
        return false;
    }
    *present = true;
    *value = (unsigned int)datum.u.int64;
    return true;
}

static bool parse_rule(Config *candidate, toml_datum_t table, Rule *rule)
{
    static const char *keys[] = {
        "class", "instance", "title", "window_type", "workspace", "monitor",
        "focus_on_map", "raise_on_map", "border", "fullscreen_policy", "placement",
    };
    if (!validate_keys(table, "rule", keys, 11) ||
        !read_rule_pattern(table, "class", &rule->has_class, rule->class_pattern) ||
        !read_rule_pattern(table, "instance", &rule->has_instance,
                           rule->instance_pattern) ||
        !read_rule_pattern(table, "title", &rule->has_title, rule->title_pattern) ||
        !read_rule_uint(table, "workspace", candidate->workspace_count,
                        &rule->has_workspace, &rule->workspace) ||
        !read_rule_uint(table, "monitor", 32, &rule->has_monitor, &rule->monitor) ||
        !read_rule_bool(table, "focus_on_map", &rule->has_focus_on_map,
                        &rule->focus_on_map) ||
        !read_rule_bool(table, "raise_on_map", &rule->has_raise_on_map,
                        &rule->raise_on_map) ||
        !read_rule_bool(table, "border", &rule->has_border, &rule->border)) return false;

    toml_datum_t datum = toml_get(table, "window_type");
    if (datum.type != TOML_UNKNOWN) {
        static const char *names[] = {"normal", "dialog", "dock", "desktop", "notification"};
        unsigned int selected = 0;
        if (!read_enum(table, "rule", "window_type", names, 5, &selected)) return false;
        rule->has_window_type = true;
        rule->window_type = (WindowType)selected;
    }
    datum = toml_get(table, "fullscreen_policy");
    if (datum.type != TOML_UNKNOWN) {
        static const char *names[] = {"allow", "fake", "deny"};
        unsigned int selected = 0;
        if (!read_enum(table, "rule", "fullscreen_policy", names, 3, &selected)) return false;
        rule->has_fullscreen_policy = true;
        rule->fullscreen_policy = (ClientFullscreenPolicy)selected;
    }
    datum = toml_get(table, "placement");
    if (datum.type != TOML_UNKNOWN) {
        static const char *names[] = {"center", "client"};
        unsigned int selected = 0;
        if (!read_enum(table, "rule", "placement", names, 2, &selected)) return false;
        rule->has_placement = true;
        rule->placement = (PlacementPolicy)selected;
    }
    bool has_match = rule->has_class || rule->has_instance || rule->has_title ||
                     rule->has_window_type;
    bool has_action = rule->has_workspace || rule->has_monitor ||
                      rule->has_focus_on_map || rule->has_raise_on_map ||
                      rule->has_border || rule->has_fullscreen_policy ||
                      rule->has_placement;
    return has_match && has_action;
}

static bool parse_rules(Config *candidate, toml_datum_t rules)
{
    if (rules.type == TOML_UNKNOWN) return true;
    if (rules.type != TOML_ARRAY) {
        fprintf(stderr, "box2430: config option rules must be an array of tables\n");
        return false;
    }
    if (rules.u.arr.size > BOX2430_MAX_RULES) {
        fprintf(stderr, "box2430: too many rules (maximum %d)\n", BOX2430_MAX_RULES);
        return false;
    }
    for (int32_t i = 0; i < rules.u.arr.size; ++i) {
        if (rules.u.arr.elem[i].type != TOML_TABLE ||
            !parse_rule(candidate, rules.u.arr.elem[i],
                        &candidate->rules[candidate->rule_count])) {
            fprintf(stderr, "box2430: invalid rule %d\n", i + 1);
            return false;
        }
        ++candidate->rule_count;
    }
    return true;
}

static bool parse_tabs(Config *candidate, toml_datum_t tabs)
{
    static const char *keys[] = {
        "enabled", "position", "height", "padding", "font", "font_bold",
        "source", "format", "fg", "bg", "font_style", "inactive", "active",
        "urgent",
    };
    return validate_keys(tabs, "appearance.tabs", keys, 14) &&
           read_bool(tabs, "appearance.tabs", "enabled", &candidate->tabs.enabled) &&
           read_ui_edge(tabs, "appearance.tabs", "position",
                        &candidate->tabs.position) &&
           read_uint(tabs, "appearance.tabs", "height", 12, 128,
                     &candidate->tabs.height) &&
           read_uint(tabs, "appearance.tabs", "padding", 0, 128,
                     &candidate->tabs.padding) &&
           read_text(tabs, "appearance.tabs", "font", candidate->tabs.font,
                     sizeof(candidate->tabs.font)) &&
           read_text(tabs, "appearance.tabs", "font_bold",
                     candidate->tabs.font_bold, sizeof(candidate->tabs.font_bold)) &&
           read_label_source(tabs, "appearance.tabs", "source",
                             &candidate->tabs.source) &&
           read_base_style(tabs, "appearance.tabs", &candidate->tabs.style) &&
           parse_style_state(tabs, "inactive", "appearance.tabs",
                             &candidate->tabs.inactive) &&
           parse_style_state(tabs, "active", "appearance.tabs",
                             &candidate->tabs.active) &&
           parse_style_state(tabs, "urgent", "appearance.tabs",
                             &candidate->tabs.urgent);
}

static bool parse_workspace_widget(BarConfig *bar, toml_datum_t table)
{
    static const char *keys[] = {
        "fg", "bg", "font_style", "format", "empty", "occupied",
        "active", "urgent", "active_urgent",
    };
    return validate_keys(table, "appearance.bar.widgets.workspaces", keys, 9) &&
           read_style_override(table, "appearance.bar.widgets.workspaces",
                               &bar->workspaces.style, true) &&
           parse_style_state(table, "empty", "appearance.bar.widgets.workspaces",
                             &bar->workspaces.empty) &&
           parse_style_state(table, "occupied", "appearance.bar.widgets.workspaces",
                             &bar->workspaces.occupied) &&
           parse_style_state(table, "active", "appearance.bar.widgets.workspaces",
                             &bar->workspaces.active) &&
           parse_style_state(table, "urgent", "appearance.bar.widgets.workspaces",
                             &bar->workspaces.urgent) &&
           parse_style_state(table, "active_urgent",
                             "appearance.bar.widgets.workspaces",
                             &bar->workspaces.active_urgent);
}

static bool parse_mode_state(toml_datum_t parent, const char *name,
                             ModeStateConfig *state)
{
    toml_datum_t table = toml_get(parent, name);
    static const char *keys[] = {"label", "fg", "bg", "font_style", "format"};
    char path[128];
    snprintf(path, sizeof(path), "appearance.bar.widgets.mode.%s", name);
    return validate_keys(table, path, keys, 5) &&
           read_text(table, path, "label", state->label, sizeof(state->label)) &&
           read_style_override(table, path, &state->style, true);
}

static bool parse_mode_widget(BarConfig *bar, toml_datum_t table)
{
    static const char *keys[] = {
        "fg", "bg", "font_style", "format", "free", "monocle",
    };
    return validate_keys(table, "appearance.bar.widgets.mode", keys, 6) &&
           read_style_override(table, "appearance.bar.widgets.mode",
                               &bar->mode.style, true) &&
           parse_mode_state(table, "free", &bar->mode.free) &&
           parse_mode_state(table, "monocle", &bar->mode.monocle);
}

static bool parse_title_widget(BarConfig *bar, toml_datum_t table)
{
    static const char *keys[] = {"source", "fg", "bg", "font_style", "format"};
    return validate_keys(table, "appearance.bar.widgets.title", keys, 5) &&
           read_label_source(table, "appearance.bar.widgets.title", "source",
                             &bar->title.source) &&
           read_style_override(table, "appearance.bar.widgets.title",
                               &bar->title.style, true);
}

static bool parse_status_widget(BarConfig *bar, toml_datum_t table)
{
    static const char *keys[] = {"fg", "bg", "font_style", "format"};
    return validate_keys(table, "appearance.bar.widgets.status", keys, 4) &&
           read_style_override(table, "appearance.bar.widgets.status",
                               &bar->status.style, true);
}

static bool parse_clock_widget(BarConfig *bar, toml_datum_t table)
{
    static const char *keys[] = {"fg", "bg", "font_style", "format"};
    return validate_keys(table, "appearance.bar.widgets.clock", keys, 4) &&
           read_text(table, "appearance.bar.widgets.clock", "format",
                     bar->clock.format, sizeof(bar->clock.format)) &&
           read_style_override(table, "appearance.bar.widgets.clock",
                               &bar->clock.style, false);
}

static bool parse_bar_widgets(BarConfig *bar, toml_datum_t widgets)
{
    static const char *keys[] = {
        "workspaces", "mode", "title", "status", "clock", "tray",
    };
    if (!validate_keys(widgets, "appearance.bar.widgets", keys, 6)) return false;
    toml_datum_t tray = toml_get(widgets, "tray");
    return parse_workspace_widget(bar, toml_get(widgets, "workspaces")) &&
           parse_mode_widget(bar, toml_get(widgets, "mode")) &&
           parse_title_widget(bar, toml_get(widgets, "title")) &&
           parse_status_widget(bar, toml_get(widgets, "status")) &&
           parse_clock_widget(bar, toml_get(widgets, "clock")) &&
           validate_keys(tray, "appearance.bar.widgets.tray", NULL, 0);
}

static bool parse_bar(Config *candidate, toml_datum_t bar)
{
    static const char *keys[] = {
        "enabled", "position", "height", "padding", "gap", "font", "font_bold",
        "fg", "bg", "left", "center", "right", "widgets",
    };
    if (!validate_keys(bar, "appearance.bar", keys, 13) ||
        !read_bool(bar, "appearance.bar", "enabled", &candidate->bar.enabled) ||
        !read_ui_edge(bar, "appearance.bar", "position", &candidate->bar.position) ||
        !read_uint(bar, "appearance.bar", "height", 12, 128,
                   &candidate->bar.height) ||
        !read_uint(bar, "appearance.bar", "padding", 0, 128,
                   &candidate->bar.padding) ||
        !read_uint(bar, "appearance.bar", "gap", 0, 128,
                   &candidate->bar.gap) ||
        !read_text(bar, "appearance.bar", "font", candidate->bar.font,
                   sizeof(candidate->bar.font)) ||
        !read_text(bar, "appearance.bar", "font_bold", candidate->bar.font_bold,
                   sizeof(candidate->bar.font_bold)) ||
        !read_color(bar, "appearance.bar", "fg", candidate->bar.style.fg) ||
        !read_color(bar, "appearance.bar", "bg", candidate->bar.style.bg) ||
        !read_widget_list(bar, "left", candidate->bar.left,
                          &candidate->bar.left_count) ||
        !read_widget_list(bar, "center", candidate->bar.center,
                          &candidate->bar.center_count) ||
        !read_widget_list(bar, "right", candidate->bar.right,
                          &candidate->bar.right_count)) return false;
    return validate_widget_uniqueness(&candidate->bar) &&
           parse_bar_widgets(&candidate->bar, toml_get(bar, "widgets"));
}

static bool parse_supported_config(Config *candidate, toml_datum_t root)
{
    static const char *top_keys[] = {
        "workspaces", "focus", "placement", "fullscreen", "appearance", "snap",
        "bindings", "rules",
    };
    if (!validate_keys(root, "root", top_keys, 8)) return false;

    toml_datum_t workspaces = toml_get(root, "workspaces");
    static const char *workspace_keys[] = {"count"};
    if (!validate_keys(workspaces, "workspaces", workspace_keys, 1) ||
        !read_uint(workspaces, "workspaces", "count", 1, BOX2430_MAX_WORKSPACES,
                   &candidate->workspace_count)) return false;

    toml_datum_t focus = toml_get(root, "focus");
    static const char *focus_keys[] = {
        "mode", "active_window", "raise_on_focus", "focus_on_map", "raise_on_map",
    };
    unsigned int choice = (unsigned int)candidate->focus_mode;
    static const char *focus_modes[] = {"click", "sloppy"};
    if (!validate_keys(focus, "focus", focus_keys, 5) ||
        !read_enum(focus, "focus", "mode", focus_modes, 2, &choice)) return false;
    candidate->focus_mode = (FocusMode)choice;
    choice = (unsigned int)candidate->active_window_policy;
    static const char *active_window_policies[] = {"urgent", "focus"};
    if (!read_enum(focus, "focus", "active_window", active_window_policies, 2,
                   &choice)) return false;
    candidate->active_window_policy = (ActiveWindowPolicy)choice;
    if (!read_bool(focus, "focus", "raise_on_focus", &candidate->raise_on_focus) ||
        !read_bool(focus, "focus", "focus_on_map", &candidate->focus_on_map) ||
        !read_bool(focus, "focus", "raise_on_map", &candidate->raise_on_map)) return false;
    toml_datum_t placement = toml_get(root, "placement");
    static const char *placement_keys[] = {"normal", "dialog"};
    static const char *placements[] = {"center", "client"};
    choice = (unsigned int)candidate->normal_placement;
    if (!validate_keys(placement, "placement", placement_keys, 2) ||
        !read_enum(placement, "placement", "normal", placements, 2, &choice)) return false;
    candidate->normal_placement = (PlacementPolicy)choice;
    choice = (unsigned int)candidate->dialog_placement;
    if (!read_enum(placement, "placement", "dialog", placements, 2, &choice)) return false;
    candidate->dialog_placement = (PlacementPolicy)choice;

    toml_datum_t fullscreen = toml_get(root, "fullscreen");
    static const char *fullscreen_keys[] = {"client_policy"};
    static const char *policies[] = {"allow", "fake", "deny"};
    choice = (unsigned int)candidate->client_fullscreen_policy;
    if (!validate_keys(fullscreen, "fullscreen", fullscreen_keys, 1) ||
        !read_enum(fullscreen, "fullscreen", "client_policy", policies, 3, &choice))
        return false;
    candidate->client_fullscreen_policy = (ClientFullscreenPolicy)choice;

    toml_datum_t appearance = toml_get(root, "appearance");
    static const char *appearance_keys[] = {
        "background", "border", "tabs", "bar", "snap_preview",
    };
    if (!validate_keys(appearance, "appearance", appearance_keys, 5) ||
        !read_color(appearance, "appearance", "background", candidate->background))
        return false;
    toml_datum_t border = toml_get(appearance, "border");
    static const char *border_keys[] = {"free", "monocle"};
    static const char *border_style_keys[] = {"width", "focused", "unfocused", "urgent"};
    if (!validate_keys(border, "appearance.border", border_keys, 2)) return false;
    toml_datum_t free_border = toml_get(border, "free");
    if (!validate_keys(free_border, "appearance.border.free", border_style_keys, 4) ||
        !read_uint(free_border, "appearance.border.free", "width", 0, 64,
                   &candidate->border.free.width) ||
        !read_color(free_border, "appearance.border.free", "focused",
                    candidate->border.free.focused) ||
        !read_color(free_border, "appearance.border.free", "unfocused",
                    candidate->border.free.unfocused) ||
        !read_color(free_border, "appearance.border.free", "urgent",
                    candidate->border.free.urgent)) return false;
    toml_datum_t monocle_border = toml_get(border, "monocle");
    if (!validate_keys(monocle_border, "appearance.border.monocle",
                       border_style_keys, 4) ||
        !read_uint(monocle_border, "appearance.border.monocle", "width", 0, 64,
                   &candidate->border.monocle.width) ||
        !read_color(monocle_border, "appearance.border.monocle", "focused",
                    candidate->border.monocle.focused) ||
        !read_color(monocle_border, "appearance.border.monocle", "unfocused",
                    candidate->border.monocle.unfocused) ||
        !read_color(monocle_border, "appearance.border.monocle", "urgent",
                    candidate->border.monocle.urgent)) return false;
    toml_datum_t preview = toml_get(appearance, "snap_preview");
    static const char *preview_keys[] = {"color", "width"};
    if (!validate_keys(preview, "appearance.snap_preview", preview_keys, 2) ||
        !read_color(preview, "appearance.snap_preview", "color",
                    candidate->snap_preview_color) ||
        !read_uint(preview, "appearance.snap_preview", "width", 1, 32,
                   &candidate->snap_preview_width)) return false;
    if (!parse_tabs(candidate, toml_get(appearance, "tabs")) ||
        !parse_bar(candidate, toml_get(appearance, "bar"))) return false;

    toml_datum_t snap = toml_get(root, "snap");
    static const char *snap_keys[] = {
        "enabled", "edge_zone", "side_ratio", "corner_width_ratio",
        "corner_height_ratio", "preview",
    };
    if (!validate_keys(snap, "snap", snap_keys, 6) ||
        !read_bool(snap, "snap", "enabled", &candidate->snap_enabled) ||
        !read_uint(snap, "snap", "edge_zone", 1, 256,
                   &candidate->snap_edge_zone) ||
        !read_ratio(snap, "side_ratio", &candidate->snap_side_ratio) ||
        !read_ratio(snap, "corner_width_ratio", &candidate->snap_corner_width_ratio) ||
        !read_ratio(snap, "corner_height_ratio", &candidate->snap_corner_height_ratio) ||
        !read_bool(snap, "snap", "preview", &candidate->snap_preview))
        return false;

    prune_invalid_default_bindings(candidate);
    toml_datum_t bindings = toml_get(root, "bindings");
    static const char *binding_keys[] = {"inherit_defaults", "keys", "mouse", "tabbar"};
    if (!validate_keys(bindings, "bindings", binding_keys, 4) ||
        !read_bool(bindings, "bindings", "inherit_defaults",
                   &candidate->inherit_default_bindings)) return false;
    if (!candidate->inherit_default_bindings) {
        candidate->key_binding_count = 0;
        candidate->mouse_binding_count = 0;
        candidate->tab_binding_count = 0;
    }
    return parse_key_bindings(candidate, toml_get(bindings, "keys")) &&
           parse_mouse_bindings(candidate, toml_get(bindings, "mouse")) &&
           parse_tab_bindings(candidate, toml_get(bindings, "tabbar")) &&
           parse_rules(candidate, toml_get(root, "rules"));
}

bool config_load(Config *config, const char *explicit_path)
{
    char default_path[4096];
    const char *path = explicit_path;
    if (!path) {
        const char *base = getenv("XDG_CONFIG_HOME");
        if (base && *base) snprintf(default_path, sizeof(default_path), "%s/box2430/config.toml", base);
        else {
            base = getenv("HOME");
            if (!base || !*base) return true;
            snprintf(default_path, sizeof(default_path), "%s/.config/box2430/config.toml", base);
        }
        path = default_path;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        if (!explicit_path && errno == ENOENT) return true;
        fprintf(stderr, "box2430: cannot read config %s: %s; using defaults\n",
                path, strerror(errno));
        return false;
    }
    toml_result_t result = toml_parse_file_named(file, path);
    fclose(file);
    if (!result.ok) {
        fprintf(stderr, "box2430: %s; using defaults\n", result.errmsg);
        toml_free(result);
        return false;
    }
    Config candidate;
    config_set_defaults(&candidate);
    bool valid = parse_supported_config(&candidate, result.toptab);
    toml_free(result);
    if (!valid) {
        fprintf(stderr, "box2430: discarding invalid config %s; using defaults\n", path);
        return false;
    }
    *config = candidate;
    return true;
}
