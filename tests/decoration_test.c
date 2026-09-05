#include "box2430.h"
#include "decoration.h"

#include <X11/Xatom.h>
#include <assert.h>
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

static void test_config(void)
{
    Config defaults, config;
    config_set_defaults(&defaults);
    assert(!defaults.decoration.enabled && defaults.decoration.height == 24);
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
    };
    for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        snprintf(text, sizeof(text),
                 "[workspaces]\ncount = 3\n[appearance.decoration]\n%s\n", invalid[i]);
        config = defaults;
        assert(!load_text(&config, text));
        assert(memcmp(&config, &defaults, sizeof(config)) == 0);
    }
    config = defaults;
    assert(!load_text(&config,
        "[appearance.decoration]\nenabled = true\n"
        "[[rules]]\nclass = 'A'\ndecoration = 'maybe'\n"));
    assert(memcmp(&config, &defaults, sizeof(config)) == 0);
    assert(!load_text(&config,
        "[[rules]]\nclass = 'A'\ndecoration = true\n"));
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

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--x11-types")) {
        test_x11_types();
        return 0;
    }
    assert(argc == 1);
    test_config();
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
