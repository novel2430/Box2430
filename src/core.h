#ifndef BOX2430_CORE_H
#define BOX2430_CORE_H

#include <stdbool.h>

enum {
    BOX2430_MAX_MONITORS = 32,
    BOX2430_MAX_WORKSPACES = 32,
    BOX2430_DEFAULT_WORKSPACE_COUNT = 9,
};

typedef enum PlacementPolicy {
    PLACEMENT_CENTER,
    PLACEMENT_CLIENT,
} PlacementPolicy;

typedef enum ClientFullscreenPolicy {
    CLIENT_FULLSCREEN_ALLOW,
    CLIENT_FULLSCREEN_FAKE,
    CLIENT_FULLSCREEN_DENY,
} ClientFullscreenPolicy;

typedef enum ClientDecorationPolicy {
    CLIENT_DECORATION_AUTO,
    CLIENT_DECORATION_FORCE,
    CLIENT_DECORATION_NONE,
} ClientDecorationPolicy;

typedef enum WindowType {
    WINDOW_TYPE_NORMAL,
    WINDOW_TYPE_DIALOG,
    WINDOW_TYPE_DOCK,
    WINDOW_TYPE_DESKTOP,
    WINDOW_TYPE_NOTIFICATION,
} WindowType;

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

/* Backend-neutral Box authority.  Runtime protocol objects, projection state,
 * and backend lifecycle bookkeeping must live outside these structures. */
struct Client {
    Workspace *workspace;

    /* Persistent/windowed Box geometry.  A backend may present a different
     * rectangle temporarily (MONOCLE/fullscreen) or observe a client-selected
     * size that differs from this authority. */
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
    bool focusable;
    bool maximized;
    bool fullscreen;
    bool user_fullscreen;
    bool client_fullscreen;

    char *title;
    char *app_id;
    char *class_name;
    char *instance;
    WindowType window_type;

    bool requests_no_decoration;
    bool auto_decoration_eligible;
    ClientDecorationPolicy decoration_policy;
    Client *parent;

    bool border_enabled;
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
    Workspace *workspaces;
    Workspace *active_workspace;
};


typedef struct TopologyClientPlan {
    Client *client;
    unsigned int old_monitor_index;
    unsigned int new_monitor_index;
    unsigned int workspace_index;
    bool migrate;
    bool adjust_geometry;
} TopologyClientPlan;

typedef struct MonitorTopologyPlan {
    unsigned int old_count;
    unsigned int new_count;
    Rect old_rects[BOX2430_MAX_MONITORS];
    Rect new_rects[BOX2430_MAX_MONITORS];
    int old_for_new[BOX2430_MAX_MONITORS];
    int new_for_old[BOX2430_MAX_MONITORS];
    unsigned int fallback_new_index;
    unsigned int selected_new_index;
    Client *preferred_focus;
    TopologyClientPlan *clients;
    unsigned int client_count;
} MonitorTopologyPlan;

typedef enum MonitorTopologyPlanResult {
    MONITOR_TOPOLOGY_PLAN_FAILED,
    MONITOR_TOPOLOGY_NO_CHANGE,
    MONITOR_TOPOLOGY_SEMANTIC_CHANGE,
} MonitorTopologyPlanResult;

typedef struct WMModel {
    Monitor *monitors;
    unsigned int monitor_count;
    Monitor *selected_monitor;
    Client *clients;
    Client *focused_client;
} WMModel;

bool client_workspace_is_active(const Client *client);
bool client_should_be_visible(const Client *client);
bool client_can_focus(const Client *client);
bool monitor_init_authority(Monitor *monitor, unsigned int index,
                            Rect geometry, unsigned int workspace_count);
void monitor_finish_authority(Monitor *monitor);
void workspace_attach_client(Workspace *workspace, Client *client);
void workspace_detach_client(Workspace *workspace, Client *client);
void workspace_promote_focus(Workspace *workspace, Client *client);
void client_reassign_workspace(Client *client, Workspace *workspace);
Client *workspace_focus_fallback(Workspace *workspace, Client *removed);
Client *workspace_focus_target(Workspace *workspace);

MonitorTopologyPlanResult model_plan_monitor_topology(
    const WMModel *model, unsigned int new_count,
    const Rect new_rects[BOX2430_MAX_MONITORS],
    const int old_for_new[BOX2430_MAX_MONITORS],
    const int new_for_old[BOX2430_MAX_MONITORS],
    MonitorTopologyPlan *plan);
void model_free_monitor_topology_plan(MonitorTopologyPlan *plan);
void client_translate_latent_geometry(Client *client, Rect old_monitor,
                                      Rect new_monitor);
Rect workspace_monocle_content_area(const Workspace *workspace,
                                    bool reserve_tab_strip, bool tab_at_top,
                                    int tab_height);

#endif
