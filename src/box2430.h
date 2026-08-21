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
    BOX2430_MAX_WORKSPACES = 32,
    BOX2430_MAX_TAB_FONTS = 16,
    BOX2430_MAX_UI_FORMAT = 128,
    BOX2430_MAX_UI_LABEL = 128,
    BOX2430_MAX_BAR_WIDGETS = 6,
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
typedef enum ActiveWindowPolicy {
    ACTIVE_WINDOW_URGENT,
    ACTIVE_WINDOW_FOCUS,
} ActiveWindowPolicy;
typedef enum PlacementPolicy { PLACEMENT_CENTER, PLACEMENT_CLIENT } PlacementPolicy;
typedef enum ClientFullscreenPolicy {
    CLIENT_FULLSCREEN_ALLOW,
    CLIENT_FULLSCREEN_FAKE,
    CLIENT_FULLSCREEN_DENY,
} ClientFullscreenPolicy;

typedef enum UIFontStyle {
    UI_FONT_NORMAL,
    UI_FONT_BOLD,
} UIFontStyle;

typedef enum UILabelSource {
    UI_LABEL_TITLE,
    UI_LABEL_CLASS,
    UI_LABEL_INSTANCE,
} UILabelSource;

typedef enum UIBarPosition {
    UI_BAR_TOP,
    UI_BAR_BOTTOM,
} UIBarPosition;

typedef enum UIBarWidget {
    UI_WIDGET_WORKSPACES,
    UI_WIDGET_MODE,
    UI_WIDGET_TITLE,
    UI_WIDGET_STATUS,
    UI_WIDGET_CLOCK,
    UI_WIDGET_TRAY,
    UI_WIDGET_COUNT,
} UIBarWidget;

typedef enum UIWorkspaceVisualState {
    UI_WORKSPACE_EMPTY,
    UI_WORKSPACE_OCCUPIED,
    UI_WORKSPACE_ACTIVE,
    UI_WORKSPACE_URGENT,
    UI_WORKSPACE_ACTIVE_URGENT,
    UI_WORKSPACE_STATE_COUNT,
} UIWorkspaceVisualState;

typedef enum UIModeVisualState {
    UI_MODE_FREE,
    UI_MODE_MONOCLE,
    UI_MODE_STATE_COUNT,
} UIModeVisualState;

typedef struct UILabelFormat {
    char prefix[BOX2430_MAX_UI_FORMAT];
    char suffix[BOX2430_MAX_UI_FORMAT];
} UILabelFormat;

typedef struct UIStyle {
    char fg[8];
    char bg[8];
    UIFontStyle font_style;
    UILabelFormat format;
} UIStyle;

typedef struct UIStyleOverride {
    bool has_fg;
    bool has_bg;
    bool has_font_style;
    bool has_format;
    char fg[8];
    char bg[8];
    UIFontStyle font_style;
    UILabelFormat format;
} UIStyleOverride;

typedef struct TabConfig {
    bool enabled;
    unsigned int height;
    unsigned int padding;
    char font[128];
    char font_bold[128];
    UILabelSource source;
    UIStyle style;
    UIStyleOverride inactive;
    UIStyleOverride active;
    UIStyleOverride urgent;
} TabConfig;

typedef struct WorkspaceWidgetConfig {
    UIStyleOverride style;
    UIStyleOverride empty;
    UIStyleOverride occupied;
    UIStyleOverride active;
    UIStyleOverride urgent;
    UIStyleOverride active_urgent;
} WorkspaceWidgetConfig;

typedef struct ModeStateConfig {
    char label[BOX2430_MAX_UI_LABEL];
    UIStyleOverride style;
} ModeStateConfig;

typedef struct ModeWidgetConfig {
    UIStyleOverride style;
    ModeStateConfig free;
    ModeStateConfig monocle;
} ModeWidgetConfig;

typedef struct TitleWidgetConfig {
    UILabelSource source;
    UIStyleOverride style;
} TitleWidgetConfig;

typedef struct StatusWidgetConfig {
    UIStyleOverride style;
} StatusWidgetConfig;

typedef struct ClockWidgetConfig {
    char format[BOX2430_MAX_UI_FORMAT];
    UIStyleOverride style;
} ClockWidgetConfig;

typedef struct BarConfig {
    bool enabled;
    UIBarPosition position;
    unsigned int height;
    unsigned int padding;
    unsigned int gap;
    char font[128];
    char font_bold[128];
    UIStyle style;
    UIBarWidget left[BOX2430_MAX_BAR_WIDGETS];
    unsigned int left_count;
    UIBarWidget center[BOX2430_MAX_BAR_WIDGETS];
    unsigned int center_count;
    UIBarWidget right[BOX2430_MAX_BAR_WIDGETS];
    unsigned int right_count;
    WorkspaceWidgetConfig workspaces;
    ModeWidgetConfig mode;
    TitleWidgetConfig title;
    StatusWidgetConfig status;
    ClockWidgetConfig clock;
} BarConfig;

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
    ActiveWindowPolicy active_window_policy;
    bool raise_on_focus;
    bool focus_on_map;
    bool raise_on_map;
    PlacementPolicy normal_placement;
    PlacementPolicy dialog_placement;
    ClientFullscreenPolicy client_fullscreen_policy;
    unsigned int border_width;
    char background[8];
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
    TabConfig tabs;
    BarConfig bar;
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

unsigned int normalize_monitor_rects(const Rect *raw_rects,
                                     unsigned int raw_count, Rect fallback,
                                     Rect *normalized, unsigned int capacity);
void match_monitor_rects(const Rect *old_rects, unsigned int old_count,
                         const Rect *new_rects, unsigned int new_count,
                         int old_for_new[BOX2430_MAX_MONITORS],
                         int new_for_old[BOX2430_MAX_MONITORS]);

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
    Client *focus_prev;
    Client *focus_next;
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
    unsigned int original_border_width;
    bool size_hints_valid;
    int base_width;
    int base_height;
    int minimum_width;
    int minimum_height;
    int maximum_width;
    int maximum_height;
    int width_increment;
    int height_increment;
    double minimum_aspect;
    double maximum_aspect;
    ClientFullscreenPolicy fullscreen_policy;
};

struct Workspace {
    Monitor *monitor;
    unsigned int index;
    WorkspaceMode mode;
    Client *clients;
    Client *tab_head;
    Client *tab_tail;
    Client *stack_head;
    Client *stack_tail;
    Client *focus_head;
    Client *focus_tail;
};

struct Monitor {
    unsigned int index;
    Rect geometry;
    Rect workarea;
    Rect bar_geometry;
    Workspace *workspaces;
    Workspace *active_workspace;
    Window bar;
    XftDraw *bar_draw;
    Rect bar_widget_rects[UI_WIDGET_COUNT];
    Rect bar_workspace_rects[BOX2430_MAX_WORKSPACES];
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
    XftFont *bar_fonts[BOX2430_MAX_TAB_FONTS];
    unsigned int bar_font_count;
    XftFont *bar_fonts_bold[BOX2430_MAX_TAB_FONTS];
    unsigned int bar_font_bold_count;
    XftColor bar_bg;
    XftColor bar_workspace_fg[UI_WORKSPACE_STATE_COUNT];
    XftColor bar_workspace_bg[UI_WORKSPACE_STATE_COUNT];
    XftColor bar_mode_fg[UI_MODE_STATE_COUNT];
    XftColor bar_mode_bg[UI_MODE_STATE_COUNT];
    XftColor bar_title_fg;
    XftColor bar_title_bg;
    bool tab_resources_ready;
    bool bar_resources_ready;
} WM;

bool wm_init(WM *wm, const char *display_name, const char *config_path,
             bool session_start);
void wm_run(WM *wm, const char *autostart_path);
void wm_destroy(WM *wm);
Client *workspace_focus_target(Workspace *workspace);
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
bool spawn_autostart(WM *wm, const char *path);
bool command_validate(const Config *config, CommandContextType context, int argc,
                      const char *const *argv);
void mouse_begin_drag(WM *wm, Client *client, bool resize, int root_x, int root_y);
void config_set_defaults(Config *config);
bool config_load(Config *config, const char *explicit_path);

bool x11_acquire_wm_ownership(WM *wm);
void x11_init_atoms(WM *wm);
void x11_set_wm_state(WM *wm, Window window, long state);
bool x11_window_is_iconic(WM *wm, Window window);
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
