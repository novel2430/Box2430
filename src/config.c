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
    static const char *next_mru[] = {"focus", "next-mru"};
    static const char *mode[] = {"mode", "monocle", "toggle"};
    static const char *next_tab[] = {"focus", "next-tab"};
    static const char *prev_tab[] = {"focus", "prev-tab"};
    static const char *snap_left[] = {"snap", "left"};
    static const char *snap_right[] = {"snap", "right"};
    static const char *maximize[] = {"maximize", "toggle"};
    static const char *fullscreen[] = {"fullscreen", "toggle"};
    static const char *monitor_prev[] = {"monitor", "prev"};
    static const char *monitor_next[] = {"monitor", "next"};
    add_default_binding(config, Mod4Mask, XK_Return, 2, spawn);
    add_default_binding(config, Mod4Mask, XK_q, 2, close);
    add_default_binding(config, Mod1Mask, XK_Tab, 2, next_mru);
    add_default_binding(config, Mod4Mask, XK_m, 3, mode);
    add_default_binding(config, Mod4Mask, XK_j, 2, next_tab);
    add_default_binding(config, Mod4Mask, XK_k, 2, prev_tab);
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
        {Button4, "focus", "prev-tab"}, {Button5, "focus", "next-tab"},
    };
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i) {
        MouseBinding *binding = &config->tab_bindings[config->tab_binding_count++];
        *binding = (MouseBinding){.button = tabs[i].button, .argc = 2};
        strcpy(binding->argv[0], tabs[i].first);
        strcpy(binding->argv[1], tabs[i].second);
    }
}

void config_set_defaults(Config *config)
{
    *config = (Config){
        .workspace_count = BOX2430_DEFAULT_WORKSPACE_COUNT,
        .focus_mode = FOCUS_CLICK,
        .raise_on_focus = false,
        .focus_on_map = true,
        .raise_on_map = true,
        .monocle_fallback_mru = false,
        .normal_placement = PLACEMENT_CENTER,
        .dialog_placement = PLACEMENT_CENTER,
        .client_fullscreen_policy = CLIENT_FULLSCREEN_FAKE,
        .border_width = 2,
        .snap_preview_width = 2,
        .snap_enabled = true,
        .snap_edge_zone = 16,
        .snap_preview = true,
        .snap_side_ratio = 0.5,
        .snap_corner_width_ratio = 0.5,
        .snap_corner_height_ratio = 0.5,
        .tabs_enabled = true,
        .tab_height = 24,
        .tab_padding = 8,
        .tab_active_bold = true,
        .tab_inactive_bold = false,
        .tab_urgent_bold = true,
        .inherit_default_bindings = true,
    };
    memcpy(config->border_focused, "#89b4fa", 8);
    memcpy(config->border_unfocused, "#45475a", 8);
    memcpy(config->border_urgent, "#f38ba8", 8);
    memcpy(config->snap_preview_color, "#89b4fa", 8);
    snprintf(config->tab_font, sizeof(config->tab_font), "monospace:size=10");
    snprintf(config->tab_font_bold, sizeof(config->tab_font_bold),
             "monospace:style=Bold:size=10");
    memcpy(config->tab_active_fg, "#ffffff", 8);
    memcpy(config->tab_active_bg, "#3b4252", 8);
    memcpy(config->tab_inactive_fg, "#aaaaaa", 8);
    memcpy(config->tab_inactive_bg, "#222222", 8);
    memcpy(config->tab_urgent_fg, "#ffffff", 8);
    memcpy(config->tab_urgent_bg, "#bf616a", 8);
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
        !read_uint(workspaces, "workspaces", "count", 1, 32,
                   &candidate->workspace_count)) return false;

    toml_datum_t focus = toml_get(root, "focus");
    static const char *focus_keys[] = {
        "mode", "raise_on_focus", "focus_on_map", "raise_on_map", "monocle_fallback",
    };
    unsigned int choice = (unsigned int)candidate->focus_mode;
    static const char *focus_modes[] = {"click", "sloppy"};
    if (!validate_keys(focus, "focus", focus_keys, 5) ||
        !read_enum(focus, "focus", "mode", focus_modes, 2, &choice)) return false;
    candidate->focus_mode = (FocusMode)choice;
    if (!read_bool(focus, "focus", "raise_on_focus", &candidate->raise_on_focus) ||
        !read_bool(focus, "focus", "focus_on_map", &candidate->focus_on_map) ||
        !read_bool(focus, "focus", "raise_on_map", &candidate->raise_on_map)) return false;
    choice = candidate->monocle_fallback_mru ? 1U : 0U;
    static const char *fallbacks[] = {"tab", "mru"};
    if (!read_enum(focus, "focus", "monocle_fallback", fallbacks, 2, &choice)) return false;
    candidate->monocle_fallback_mru = choice == 1;

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
    static const char *appearance_keys[] = {"border", "tabs", "snap_preview"};
    if (!validate_keys(appearance, "appearance", appearance_keys, 3)) return false;
    toml_datum_t border = toml_get(appearance, "border");
    static const char *border_keys[] = {"width", "focused", "unfocused", "urgent"};
    if (!validate_keys(border, "appearance.border", border_keys, 4) ||
        !read_uint(border, "appearance.border", "width", 0, 64,
                   &candidate->border_width) ||
        !read_color(border, "appearance.border", "focused", candidate->border_focused) ||
        !read_color(border, "appearance.border", "unfocused", candidate->border_unfocused) ||
        !read_color(border, "appearance.border", "urgent", candidate->border_urgent)) return false;
    toml_datum_t preview = toml_get(appearance, "snap_preview");
    static const char *preview_keys[] = {"color", "width"};
    if (!validate_keys(preview, "appearance.snap_preview", preview_keys, 2) ||
        !read_color(preview, "appearance.snap_preview", "color",
                    candidate->snap_preview_color) ||
        !read_uint(preview, "appearance.snap_preview", "width", 1, 32,
                   &candidate->snap_preview_width)) return false;
    toml_datum_t tabs = toml_get(appearance, "tabs");
    static const char *tab_keys[] = {
        "enabled", "height", "padding", "font", "font_bold",
        "active_fg", "active_bg", "inactive_fg", "inactive_bg",
        "urgent_fg", "urgent_bg", "active_bold", "inactive_bold", "urgent_bold",
    };
    if (!validate_keys(tabs, "appearance.tabs", tab_keys, 14) ||
        !read_bool(tabs, "appearance.tabs", "enabled", &candidate->tabs_enabled) ||
        !read_uint(tabs, "appearance.tabs", "height", 12, 128,
                   &candidate->tab_height) ||
        !read_uint(tabs, "appearance.tabs", "padding", 0, 128,
                   &candidate->tab_padding) ||
        !read_text(tabs, "appearance.tabs", "font", candidate->tab_font,
                   sizeof(candidate->tab_font)) ||
        !read_text(tabs, "appearance.tabs", "font_bold", candidate->tab_font_bold,
                   sizeof(candidate->tab_font_bold)) ||
        !read_color(tabs, "appearance.tabs", "active_fg", candidate->tab_active_fg) ||
        !read_color(tabs, "appearance.tabs", "active_bg", candidate->tab_active_bg) ||
        !read_color(tabs, "appearance.tabs", "inactive_fg", candidate->tab_inactive_fg) ||
        !read_color(tabs, "appearance.tabs", "inactive_bg", candidate->tab_inactive_bg) ||
        !read_color(tabs, "appearance.tabs", "urgent_fg", candidate->tab_urgent_fg) ||
        !read_color(tabs, "appearance.tabs", "urgent_bg", candidate->tab_urgent_bg) ||
        !read_bool(tabs, "appearance.tabs", "active_bold", &candidate->tab_active_bold) ||
        !read_bool(tabs, "appearance.tabs", "inactive_bold", &candidate->tab_inactive_bold) ||
        !read_bool(tabs, "appearance.tabs", "urgent_bold", &candidate->tab_urgent_bold))
        return false;

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
