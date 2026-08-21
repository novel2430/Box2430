#ifndef BOX2430_H
#define BOX2430_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdbool.h>

enum {
    BOX2430_MAX_KEY_BINDINGS = 96,
    BOX2430_MAX_COMMAND_ARGS = 16,
    BOX2430_MAX_COMMAND_ARG_LENGTH = 128,
    BOX2430_MAX_RULES = 64,
    BOX2430_MAX_RULE_PATTERN = 256,
    BOX2430_MAX_MONITORS = 32,
    BOX2430_MAX_TAB_FONTS = 16,
};

typedef struct KeyBinding {
    unsigned int modifiers;
    KeySym symbol;
    int argc;
    char argv[BOX2430_MAX_COMMAND_ARGS][BOX2430_MAX_COMMAND_ARG_LENGTH];
} KeyBinding;

typedef struct MouseBinding {
    unsigned int modifiers;
    unsigned int button;
    int argc;
    char argv[BOX2430_MAX_COMMAND_ARGS][BOX2430_MAX_COMMAND_ARG_LENGTH];
} MouseBinding;

enum {
    BOX2430_DEFAULT_WORKSPACE_COUNT = 9,
};

typedef enum FocusMode { FOCUS_CLICK, FOCUS_SLOPPY } FocusMode;
typedef enum PlacementPolicy { PLACEMENT_CENTER, PLACEMENT_CLIENT } PlacementPolicy;
typedef enum ClientFullscreenPolicy {
    CLIENT_FULLSCREEN_ALLOW,
    CLIENT_FULLSCREEN_FAKE,
    CLIENT_FULLSCREEN_DENY,
} ClientFullscreenPolicy;

typedef enum WindowType {
    WINDOW_TYPE_NORMAL,
    WINDOW_TYPE_DIALOG,
    WINDOW_TYPE_DOCK,
    WINDOW_TYPE_DESKTOP,
    WINDOW_TYPE_NOTIFICATION,
} WindowType;

typedef struct Rule {
    bool has_class;
    bool has_instance;
    bool has_title;
    bool has_window_type;
    char class_pattern[BOX2430_MAX_RULE_PATTERN];
    char instance_pattern[BOX2430_MAX_RULE_PATTERN];
    char title_pattern[BOX2430_MAX_RULE_PATTERN];
    WindowType window_type;
    bool has_workspace;
    bool has_monitor;
    bool has_focus_on_map;
    bool has_raise_on_map;
    bool has_border;
    bool has_fullscreen_policy;
    bool has_placement;
    unsigned int workspace;
    unsigned int monitor;
    bool focus_on_map;
    bool raise_on_map;
    bool border;
    ClientFullscreenPolicy fullscreen_policy;
    PlacementPolicy placement;
} Rule;

typedef struct Config {
    unsigned int workspace_count;
    FocusMode focus_mode;
    bool raise_on_focus;
    bool focus_on_map;
    bool raise_on_map;
    PlacementPolicy normal_placement;
    PlacementPolicy dialog_placement;
    ClientFullscreenPolicy client_fullscreen_policy;
    unsigned int border_width;
    char border_focused[8];
    char border_unfocused[8];
    char border_urgent[8];
    char snap_preview_color[8];
    unsigned int snap_preview_width;
    bool snap_enabled;
    unsigned int snap_edge_zone;
    bool snap_preview;
    double snap_side_ratio;
    double snap_corner_width_ratio;
    double snap_corner_height_ratio;
    bool tabs_enabled;
    unsigned int tab_height;
    unsigned int tab_padding;
    char tab_font[128];
    char tab_font_bold[128];
    char tab_active_fg[8];
    char tab_active_bg[8];
    char tab_inactive_fg[8];
    char tab_inactive_bg[8];
    char tab_urgent_fg[8];
    char tab_urgent_bg[8];
    bool tab_active_bold;
    bool tab_inactive_bold;
    bool tab_urgent_bold;
    bool inherit_default_bindings;
    unsigned int key_binding_count;
    KeyBinding key_bindings[BOX2430_MAX_KEY_BINDINGS];
    unsigned int mouse_binding_count;
    MouseBinding mouse_bindings[16];
    unsigned int tab_binding_count;
    MouseBinding tab_bindings[16];
    unsigned int rule_count;
    Rule rules[BOX2430_MAX_RULES];
} Config;

typedef struct Rect {
    int x;
    int y;
    int width;
    int height;
} Rect;

typedef enum WorkspaceMode {
    WORKSPACE_FREE,
    WORKSPACE_MONOCLE,
} WorkspaceMode;

typedef enum SnapState {
    SNAP_NONE,
    SNAP_LEFT,
    SNAP_RIGHT,
    SNAP_TOP_LEFT,
    SNAP_TOP_RIGHT,
    SNAP_BOTTOM_LEFT,
    SNAP_BOTTOM_RIGHT,
} SnapState;

typedef struct Client Client;
typedef struct Workspace Workspace;
typedef struct Monitor Monitor;
typedef struct SpecialWindow SpecialWindow;

struct SpecialWindow {
    Window window;
    WindowType type;
    unsigned long strut[12];
    bool has_strut;
    SpecialWindow *next;
};

struct Client {
    Window window;
    Workspace *workspace;
    Rect geometry;
    Rect normal_geometry;
    SnapState snap_state;
    Client *next;
    Client *workspace_next;
    Client *tab_prev;
    Client *tab_next;
    Client *stack_prev;
    Client *stack_next;
    bool urgent;
    bool accepts_input;
    bool takes_focus;
    unsigned int ignored_unmaps;
    bool maximized;
    bool fullscreen;
    bool user_fullscreen;
    bool client_fullscreen;
    char *title;
    char *class_name;
    char *instance;
    WindowType window_type;
    Window transient_for;
    unsigned int border_width;
    ClientFullscreenPolicy fullscreen_policy;
};

struct Workspace {
    Monitor *monitor;
    unsigned int index;
    WorkspaceMode mode;
    Client *clients;
    Client *last_focused_client;
    Client *tab_head;
    Client *tab_tail;
    Client *stack_head;
    Client *stack_tail;
};

struct Monitor {
    unsigned int index;
    Rect geometry;
    Rect workarea;
    Workspace *workspaces;
    Workspace *active_workspace;
    Window tab_bar;
    XftDraw *tab_draw;
};

typedef struct Atoms {
    Atom wm_protocols;
    Atom wm_delete_window;
    Atom wm_take_focus;
    Atom wm_state;
    Atom net_supported;
    Atom net_active_window;
    Atom net_client_list;
    Atom net_client_list_stacking;
    Atom net_wm_state;
    Atom net_wm_state_fullscreen;
    Atom net_close_window;
    Atom utf8_string;
    Atom net_wm_name;
    Atom net_wm_window_type;
    Atom net_wm_window_type_normal;
    Atom net_wm_window_type_dialog;
    Atom net_wm_window_type_dock;
    Atom net_wm_window_type_desktop;
    Atom net_wm_window_type_notification;
    Atom net_wm_strut;
    Atom net_wm_strut_partial;
    Atom net_workarea;
} Atoms;

typedef struct WM {
    Display *display;
    int screen;
    Window root;
    int x_fd;
    Atoms atoms;
    Config config;
    Monitor *monitors;
    unsigned int monitor_count;
    Monitor *selected_monitor;
    Client *clients;
    Client *focused_client;
    SpecialWindow *special_windows;
    unsigned long focused_border;
    unsigned long unfocused_border;
    unsigned long urgent_border;
    unsigned int numlock_mask;
    bool running;
    bool restart_requested;
    struct {
        Client *client;
        Rect start_geometry;
        int start_x;
        int start_y;
        bool active;
        bool resize;
        SnapState preview_snap;
        Monitor *preview_monitor;
        bool preview_maximized;
        Window preview_windows[4];
    } drag;
    unsigned long snap_preview_color;
    XftFont *tab_fonts[BOX2430_MAX_TAB_FONTS];
    unsigned int tab_font_count;
    XftFont *tab_fonts_bold[BOX2430_MAX_TAB_FONTS];
    unsigned int tab_font_bold_count;
    XftColor tab_active_fg;
    XftColor tab_active_bg;
    XftColor tab_inactive_fg;
    XftColor tab_inactive_bg;
    XftColor tab_urgent_fg;
    XftColor tab_urgent_bg;
    bool tab_resources_ready;
} WM;

bool wm_init(WM *wm, const char *display_name, const char *config_path);
void wm_run(WM *wm);
void wm_destroy(WM *wm);
void workspace_activate(WM *wm, Monitor *monitor, Workspace *workspace);
void monitor_select(WM *wm, Monitor *monitor);
void client_close(WM *wm, Client *client);
void client_focus_relative(WM *wm, bool forward);
void client_focus_tab_target(WM *wm, Client *client, Time time);
void client_raise(WM *wm, Client *client);
void client_lower(WM *wm, Client *client);
void client_move_to_workspace(WM *wm, Client *client, Workspace *workspace,
                              bool follow, bool translate_monitor_geometry);
void workspace_set_mode(WM *wm, Workspace *workspace, WorkspaceMode mode);
void client_snap(WM *wm, Client *client, SnapState state);
void client_set_maximized(WM *wm, Client *client, bool maximized);
void client_set_fullscreen(WM *wm, Client *client, bool fullscreen);

typedef enum CommandStatus {
    COMMAND_OK,
    COMMAND_INVALID,
} CommandStatus;

typedef enum CommandContextType {
    COMMAND_CONTEXT_KEYBOARD,
    COMMAND_CONTEXT_MOUSE,
    COMMAND_CONTEXT_TABBAR,
} CommandContextType;

typedef struct CommandContext {
    CommandContextType type;
    Client *client;
    int root_x;
    int root_y;
    Time time;
} CommandContext;

CommandStatus command_run(WM *wm, const CommandContext *context, int argc,
                          const char *const *argv);
bool command_validate(const Config *config, CommandContextType context, int argc,
                      const char *const *argv);
void mouse_begin_drag(WM *wm, Client *client, bool resize, int root_x, int root_y);
void config_set_defaults(Config *config);
bool config_load(Config *config, const char *explicit_path);

bool x11_acquire_wm_ownership(WM *wm);
void x11_init_atoms(WM *wm);
void x11_set_wm_state(WM *wm, Window window, long state);
void x11_update_client_lists(WM *wm);
void x11_update_active_window(WM *wm);
WindowType x11_read_window_type(WM *wm, Window window);
char *x11_read_window_title(WM *wm, Window window);
void x11_read_window_class(WM *wm, Window window, char **instance,
                           char **class_name);
bool x11_window_requests_fullscreen(WM *wm, Window window);
bool x11_read_strut(WM *wm, Window window, unsigned long strut[12]);
void x11_update_workarea(WM *wm);

#endif
