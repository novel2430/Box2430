#include "box2430.h"
#include "decoration.h"
#include "ui.h"

#include <X11/Xatom.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void rect_is(Rect rect, int x, int y, int width, int height)
{
    assert(rect.x == x && rect.y == y);
    assert(rect.width == width && rect.height == height);
}

static bool load_text(Config *config, const char *text)
{
    char path[] = "/tmp/box2430-decoration-config-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
    bool valid = config_load(config, path);
    assert(unlink(path) == 0);
    return valid;
}

static bool load_invalid_text(Config *config, const char *text)
{
    assert(fflush(stderr) == 0);
    int saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    int null_fd = open("/dev/null", O_WRONLY);
    assert(null_fd >= 0);
    assert(dup2(null_fd, STDERR_FILENO) >= 0);
    assert(close(null_fd) == 0);

    bool valid = load_text(config, text);

    assert(fflush(stderr) == 0);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    assert(close(saved_stderr) == 0);
    return valid;
}

static void test_config(void)
{
    Config defaults, config;
    config_set_defaults(&defaults);
    assert(!defaults.decoration.enabled && defaults.decoration.height == 24);
    assert(defaults.decoration.padding == 8);
    assert(!strcmp(defaults.decoration.font, "monospace:size=10"));
    config = defaults;
    assert(load_text(&config,
        "[appearance.decoration]\nenabled = true\nheight = 32\n"
        "bg = '#112233'\nfg = '#abcdef'\n"
        "focused_bg = '#445566'\nfocused_fg = '#fedcba'\n"
        "[[rules]]\nclass = 'A'\ndecoration = 'auto'\n"
        "[[rules]]\nclass = 'B'\ndecoration = 'force'\n"
        "[[rules]]\nclass = 'C'\ndecoration = 'none'\n"));
    assert(config.decoration.enabled && config.decoration.height == 32);
    assert(!strcmp(config.decoration.focused_fg, "#fedcba"));
    assert(config.rule_count == 3);
    for (unsigned int i = 0; i < 3; ++i) {
        assert(config.rules[i].has_decoration);
        assert(config.rules[i].decoration == (ClientDecorationPolicy)i);
    }
    const char *heights[] = {"12", "128"};
    char text[512];
    for (unsigned int i = 0; i < 2; ++i) {
        snprintf(text, sizeof(text), "[appearance.decoration]\nheight = %s\n", heights[i]);
        assert(load_text(&config, text));
        assert(config.decoration.height == (unsigned int)atoi(heights[i]));
    }
    const char *invalid[] = {
        "height = -1", "height = 0", "height = 11", "height = 129",
        "height = 4294967320", "height = 9223372036854775808",
        "height = 24.0", "height = '24'", "enabled = 'true'",
        "bg = '#xyzxyz'", "focused_fg = 'red'", "unknown = true",
        "layout = []", "layout = 'title'", "layout = ['title', 'foo']",
        "layout = ['close', 'close']", "layout = [1]",
        "layout = ['title', 'space', 'maximize', 'close', 'title']",
        "close_label = ''", "maximize_label = ''", "restore_label = ''",
        "close_label = true", "maximize_label = 4", "restore_label = []",
        "padding = -1", "padding = 129", "padding = 4294967296",
        "padding = 8.0", "padding = '8'", "font = ''", "font = 10",
    };
    for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        snprintf(text, sizeof(text),
                 "[workspaces]\ncount = 3\n[appearance.decoration]\n%s\n", invalid[i]);
        config = defaults;
        assert(!load_invalid_text(&config, text));
        assert(memcmp(&config, &defaults, sizeof(config)) == 0);
    }
    config = defaults;
    assert(!load_invalid_text(&config,
        "[appearance.decoration]\nenabled = true\n"
        "[[rules]]\nclass = 'A'\ndecoration = 'maybe'\n"));
    assert(memcmp(&config, &defaults, sizeof(config)) == 0);
    assert(!load_invalid_text(&config,
        "[[rules]]\nclass = 'A'\ndecoration = true\n"));
    assert(memcmp(&config, &defaults, sizeof(config)) == 0);
}

static void test_decoration_typography(void)
{
    Config config;
    assert(load_text(&config,
        "[appearance.tabs]\nfont = 'sans:size=20'\npadding = 42\n"));
    assert(config.decoration.padding == 8);
    assert(!strcmp(config.decoration.font, "monospace:size=10"));
    const unsigned int padding[] = {0, 3, 128};
    for (unsigned int i = 0; i < 3; ++i) {
        char text[256];
        snprintf(text, sizeof(text),
            "[appearance.tabs]\nfont = 'sans:size=20'\npadding = 42\n"
            "[appearance.decoration]\nfont = 'monospace:size=18'\npadding = %u\n", padding[i]);
        assert(load_text(&config, text));
        assert(config.decoration.padding == padding[i]);
        assert(!strcmp(config.decoration.font, "monospace:size=18"));
        assert(config.tabs.padding == 42 && !strcmp(config.tabs.font, "sans:size=20"));
    }
    Config previous = config;
    char invalid[256];
    snprintf(invalid, sizeof(invalid), "[appearance.decoration]\nfont = '%0128d'\n", 0);
    assert(!load_invalid_text(&config, invalid));
    assert(!memcmp(&config, &previous, sizeof(config)));
}

static void test_layout(void)
{
    Config config;
    config_set_defaults(&config);
    DecorationConfig *dec = &config.decoration;
    assert(dec->layout_count == 4);
    for (unsigned int i = 0; i < 4; ++i) assert(dec->layout[i] == (DecorationItem)i);
    assert(!dec->close_label[0] && !dec->maximize_label[0] && !dec->restore_label[0]);
    unsigned int widths[] = {80, 0, 24, 24};
    DecorationLayout layout = decoration_layout_calculate(dec, 300, widths);
    rect_is(layout.items[DECORATION_ITEM_TITLE], 0, 0, 80, 24);
    rect_is(layout.items[DECORATION_ITEM_SPACE], 80, 0, 172, 24);
    assert(decoration_hit_test(&layout, 10, 12) == DECORATION_PART_TITLE);
    assert(decoration_hit_test(&layout, 200, 12) == DECORATION_PART_TITLE);
    assert(decoration_hit_test(&layout, 252, 12) == DECORATION_PART_MAXIMIZE);
    assert(decoration_hit_test(&layout, 276, 12) == DECORATION_PART_CLOSE);
    assert(decoration_hit_test(&layout, 300, 12) == DECORATION_PART_NONE);
    assert(decoration_hit_test(&layout, 100, 24) == DECORATION_PART_NONE);
    assert(load_text(&config, "[appearance.decoration]\n"
        "layout = ['close', 'maximize', 'space', 'title']\n"
        "close_label = 'X'\nmaximize_label = 'MAX'\nrestore_label = 'RES'\n"));
    assert(!strcmp(dec->maximize_label, "MAX"));
    layout = decoration_layout_calculate(dec, 300, widths);
    rect_is(layout.items[DECORATION_ITEM_CLOSE], 0, 0, 24, 24);
    rect_is(layout.items[DECORATION_ITEM_TITLE], 220, 0, 80, 24);
    /* Every permutation, including extreme narrow widths, stays non-overlapping. */
    for (unsigned int a = 0; a < 4; ++a)
        for (unsigned int b = 0; b < 4; ++b)
            for (unsigned int c = 0; c < 4; ++c)
                for (unsigned int d = 0; d < 4; ++d) {
                    if (a == b || a == c || a == d || b == c || b == d || c == d) continue;
                    dec->layout[0] = a; dec->layout[1] = b;
                    dec->layout[2] = c; dec->layout[3] = d;
                    for (int w = 0; w < 400; ++w) {
                        layout = decoration_layout_calculate(dec, w, widths);
                        int x = 0;
                        for (unsigned int i = 0; i < 4; ++i) {
                            Rect rect = layout.items[dec->layout[i]];
                            assert(rect.x == x && rect.width >= 0);
                            x += rect.width;
                            assert(x <= w);
                        }
                        assert(x == w);
                    }
                }
    assert(load_text(&config, "[appearance.decoration]\nlayout = ['title']\n"));
    layout = decoration_layout_calculate(dec, 300, widths);
    rect_is(layout.items[DECORATION_ITEM_TITLE], 0, 0, 300, 24);
    assert(decoration_hit_test(&layout, 290, 12) == DECORATION_PART_TITLE);
    assert(!layout.items[DECORATION_ITEM_CLOSE].width);
    assert(!layout.items[DECORATION_ITEM_MAXIMIZE].width);
    assert(load_text(&config,
        "[appearance.decoration]\nclose_label = '󰅖'\nmaximize_label = '󰁌'\nrestore_label = '󰁍'\n"));
    assert(!strcmp(dec->close_label, "󰅖"));
    char invalid_label[256];
    snprintf(invalid_label, sizeof(invalid_label),
             "[appearance.decoration]\nclose_label = '%0128d'\n", 0);
    Config previous = config;
    assert(!load_invalid_text(&config, invalid_label));
    assert(memcmp(&config, &previous, sizeof(config)) == 0);
}

static void test_bindings(void)
{
    Config defaults, config;
    config_set_defaults(&defaults);
    const DecorationBindings *bindings = &defaults.decoration_bindings;
    assert(decoration_click_action(bindings, Button1, false) == DECORATION_ACTION_RAISE);
    assert(decoration_click_action(bindings, Button1, true) == DECORATION_ACTION_MAXIMIZE_TOGGLE);
    assert(decoration_click_action(bindings, Button2, false) == DECORATION_ACTION_LOWER);
    assert(decoration_click_action(bindings, Button3, false) == DECORATION_ACTION_NONE);
    assert(decoration_click_action(bindings, Button4, false) == DECORATION_ACTION_NONE);
    assert(load_text(&config, ""));
    assert(memcmp(&config.decoration_bindings, bindings, sizeof(*bindings)) == 0);
    assert(load_text(&config, "[bindings]\ninherit_defaults = false\n"));
    assert(!config.key_binding_count && !config.mouse_binding_count);
    assert(memcmp(&config.decoration_bindings, bindings, sizeof(*bindings)) == 0);

    char text[512];
    const char *actions[] = {"none", "raise", "lower", "maximize-toggle"};
    for (unsigned int i = 0; i < 4; ++i) {
        snprintf(text, sizeof(text),
            "[bindings.decoration]\nclick = '%s'\ndouble_click = '%s'\n"
            "middle_click = '%s'\nright_click = '%s'\n",
            actions[i], actions[i], actions[i], actions[i]);
        assert(load_text(&config, text));
        bindings = &config.decoration_bindings;
        assert(decoration_click_action(bindings, Button1, false) == (DecorationAction)i);
        assert(decoration_click_action(bindings, Button1, true) == (DecorationAction)i);
        assert(decoration_click_action(bindings, Button2, false) == (DecorationAction)i);
        assert(decoration_click_action(bindings, Button3, false) == (DecorationAction)i);
    }
    assert(load_text(&config, "[bindings.decoration]\ndouble_click = 'none'\n"));
    assert(config.decoration_bindings.click == DECORATION_ACTION_RAISE);
    assert(config.decoration_bindings.double_click == DECORATION_ACTION_NONE);
    const char *invalid[] = {
        "click = 'whatever'", "double_click = 'maximize'", "middle_click = 'close'",
        "right_click = 'spawn kitty'", "drag = 'move'", "click = true",
        "double_click = 1", "middle_click = []", "right_click = {}",
    };
    for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        snprintf(text, sizeof(text),
            "[workspaces]\ncount = 3\n[bindings.decoration]\n%s\n", invalid[i]);
        config = defaults;
        assert(!load_invalid_text(&config, text));
        assert(memcmp(&config, &defaults, sizeof(config)) == 0);
    }
    assert(!load_invalid_text(&config, "[bindings]\ndecoration = 'raise'\n"));
    assert(memcmp(&config, &defaults, sizeof(config)) == 0);
}

static void assert_types(WM *wm, Window window, const Atom *atoms, int count,
                          WindowType semantic, bool eligible)
{
    XChangeProperty(wm->display, window, wm->atoms.net_wm_window_type,
                     XA_ATOM, 32, PropModeReplace, (const unsigned char *)atoms, count);
    assert(x11_read_window_type(wm, window) == semantic);
    assert(x11_window_auto_decoration_eligible(wm, window) == eligible);
}

/* Real property reads, on unmapped fixture windows in the integration server.
 * No WM ownership or root property is changed by this helper. */
static void test_x11_types(void)
{
    WM wm = {0};
    wm.display = XOpenDisplay(NULL);
    assert(wm.display);
    wm.screen = DefaultScreen(wm.display);
    wm.root = XCreateSimpleWindow(wm.display, DefaultRootWindow(wm.display),
                                  0, 0, 1, 1, 0, 0, 0);
    x11_init_atoms(&wm);
    Window window = XCreateSimpleWindow(wm.display, wm.root, 0, 0, 1, 1, 0, 0, 0);
    Atom custom = XInternAtom(wm.display, "_BOX2430_CUSTOM_TYPE", False);
    Atom normal = wm.atoms.net_wm_window_type_normal;
    Atom dialog = wm.atoms.net_wm_window_type_dialog;
    for (unsigned int enabled = 0; enabled < 2; ++enabled) {
        wm.config.decoration.enabled = enabled;
        assert_types(&wm, window, (Atom[]){custom, normal}, 2, WINDOW_TYPE_NORMAL, true);
        assert_types(&wm, window, (Atom[]){custom, dialog}, 2, WINDOW_TYPE_NORMAL, true);
        assert_types(&wm, window, &dialog, 1, WINDOW_TYPE_DIALOG, true);
        assert_types(&wm, window, &custom, 1, WINDOW_TYPE_NORMAL, false);
        assert_types(&wm, window, NULL, 0, WINDOW_TYPE_NORMAL, true);
        for (unsigned int i = 0; i < 9; ++i) {
            Atom excluded = wm.atoms.net_wm_window_type_undecorated[i];
            assert_types(&wm, window, (Atom[]){custom, excluded, normal}, 3,
                          WINDOW_TYPE_NORMAL, false);
            assert_types(&wm, window, (Atom[]){normal, excluded}, 2,
                          WINDOW_TYPE_NORMAL, true);
            XSetTransientForHint(wm.display, window, wm.root);
            assert_types(&wm, window, (Atom[]){excluded, normal}, 2,
                          WINDOW_TYPE_DIALOG, false);
            assert_types(&wm, window, (Atom[]){custom, dialog}, 2,
                          WINDOW_TYPE_DIALOG, true);
            XDeleteProperty(wm.display, window, XA_WM_TRANSIENT_FOR);
        }
        Atom special[] = {wm.atoms.net_wm_window_type_dock,
                          wm.atoms.net_wm_window_type_desktop,
                          wm.atoms.net_wm_window_type_notification};
        for (unsigned int i = 0; i < 3; ++i) {
            assert_types(&wm, window, &special[i], 1,
                          (WindowType)(WINDOW_TYPE_DOCK + i), false);
            assert_types(&wm, window, (Atom[]){custom, special[i], normal}, 3,
                          WINDOW_TYPE_NORMAL, false);
        }
        Atom long_list[65];
        for (unsigned int i = 0; i < 64; ++i) long_list[i] = custom;
        long_list[64] = normal;
        assert_types(&wm, window, long_list, 65, WINDOW_TYPE_NORMAL, true);
        XDeleteProperty(wm.display, window, wm.atoms.net_wm_window_type);
        assert(x11_read_window_type(&wm, window) == WINDOW_TYPE_NORMAL);
        assert(x11_window_auto_decoration_eligible(&wm, window));
    }
    XDestroyWindow(wm.display, wm.root);
    XCloseDisplay(wm.display);
    puts("PASS: X11 decoration type lists and unchanged semantic classification");
}

static void test_input(void)
{
    assert(!decoration_drag_threshold_reached(100, 100, 103, 103));
    assert(decoration_drag_threshold_reached(100, 100, 104, 100));
    assert(decoration_drag_threshold_reached(100, 100, 100, 96));
    Client a = {0}, b = {0};
    DecorationInputState input = {
        .client = &a, .titlebar = 123, .button = Button1, .press_x = 100, .press_y = 100,
        .last_client = &a, .last_titlebar = 123, .last_x = 100, .last_y = 100,
        .last_time = 1000,
    };
    assert(decoration_is_double_click(&input, 1300));
    assert(!decoration_is_double_click(&input, 1301));
    input.button = Button2;
    assert(!decoration_is_double_click(&input, 1100));
    input.button = Button1;
    input.part = DECORATION_PART_CLOSE;
    assert(!decoration_is_double_click(&input, 1100));
    input.part = DECORATION_PART_TITLE;
    input.dragging = true;
    assert(!decoration_is_double_click(&input, 1100));
    input.dragging = false;
    input.client = &b;
    assert(!decoration_is_double_click(&input, 1100));
    input.client = &a;
    input.titlebar = 124;
    assert(!decoration_is_double_click(&input, 1100));
    input.titlebar = 123;
    input.press_x = 104;
    assert(!decoration_is_double_click(&input, 1100));
    input.press_x = 100;
    input.last_time = 0xfffffff0UL;
    assert(decoration_is_double_click(&input, 0x10));
    input.last_client = NULL;
    assert(!decoration_is_double_click(&input, 0x10));
}

int main(int argc, char **argv)
{
    if ((argc == 3 || argc == 5) && !strcmp(argv[1], "--label-width")) {
        Display *display = XOpenDisplay(NULL);
        assert(display);
        XftFont *font = XftFontOpenName(display, DefaultScreen(display),
                                      argc == 5 ? argv[3] : "monospace:size=10");
        assert(font);
        unsigned int padding = argc == 5 ? (unsigned int)atoi(argv[4]) : 8;
        printf("%u\n", ui_text_width(display, &font, 1, argv[2]) + 2 * padding);
        XftFontClose(display, font);
        XCloseDisplay(display);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--pointer-grab")) {
        Display *display = XOpenDisplay(NULL);
        assert(display);
        int result = XGrabPointer(display, DefaultRootWindow(display), False, 0,
                                   GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
        if (result == GrabSuccess) XUngrabPointer(display, CurrentTime);
        XCloseDisplay(display);
        return result == GrabSuccess ? 0 : 1;
    }
    if (argc == 2 && !strcmp(argv[1], "--x11-types")) {
        test_x11_types();
        return 0;
    }
    assert(argc == 1);
    test_config();
    test_decoration_typography();
    test_layout();
    test_bindings();
    test_input();
    WM wm = {0};
    config_set_defaults(&wm.config);
    Workspace workspace = {.mode = WORKSPACE_FREE};
    Client client = {.workspace = &workspace, .window_type = WINDOW_TYPE_NORMAL,
                     .auto_decoration_eligible = true,
                     .geometry = {100, 100, 800, 600}};
    assert(!client_should_decorate(&wm, &client));
    client.decoration_policy = CLIENT_DECORATION_FORCE;
    assert(!client_should_decorate(&wm, &client));
    wm.config.decoration.enabled = true;
    client.decoration_policy = CLIENT_DECORATION_AUTO;
    assert(client_should_decorate(&wm, &client));
    client.window_type = WINDOW_TYPE_DIALOG;
    assert(client_should_decorate(&wm, &client));
    client.auto_decoration_eligible = false;
    assert(!client_should_decorate(&wm, &client));
    client.decoration_policy = CLIENT_DECORATION_FORCE;
    assert(client_should_decorate(&wm, &client));
    client.window_type = WINDOW_TYPE_NORMAL;
    client.auto_decoration_eligible = true;
    client.decoration_policy = CLIENT_DECORATION_AUTO;
    assert(!x11_motif_requests_no_decoration(0, 0));
    assert(!x11_motif_requests_no_decoration(1, 0));
    assert(!x11_motif_requests_no_decoration(2, 1));
    assert(x11_motif_requests_no_decoration(2, 0));
    client.requests_no_decoration = x11_motif_requests_no_decoration(2, 0);
    assert(!client_should_decorate(&wm, &client));
    client.decoration_policy = CLIENT_DECORATION_FORCE;
    assert(client_should_decorate(&wm, &client));
    workspace.mode = WORKSPACE_MONOCLE;
    assert(!client_should_decorate(&wm, &client));
    workspace.mode = WORKSPACE_FREE;
    client.fullscreen = true;
    assert(!client_should_decorate(&wm, &client));
    client.fullscreen = false;
    client.decoration_policy = CLIENT_DECORATION_NONE;
    assert(!client_should_decorate(&wm, &client));
    client.decoration_policy = CLIENT_DECORATION_AUTO;
    client.requests_no_decoration = false;
    rect_is(client_decoration_rect(&wm, &client), 100, 76, 800, 24);
    rect_is(client_outer_rect(&wm, &client, client.geometry), 100, 76, 800, 624);
    Rect target = {0, 24, 1920, 1056};
    rect_is(client_content_rect(&wm, &client, target), 0, 48, 1920, 1032);
    client.border_enabled = true;
    rect_is(client_content_rect(&wm, &client, target), 0, 48, 1916, 1028);
    rect_is(client_outer_rect(&wm, &client,
                             client_content_rect(&wm, &client, target)),
            0, 24, 1920, 1056);
    puts("PASS: decoration resolver, Motif flags, geometry and atomic config");
    return 0;
}
