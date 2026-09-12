#include "core.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

bool client_workspace_is_active(const Client *client)
{
    return client && client->workspace && client->workspace->monitor &&
           client->workspace == client->workspace->monitor->active_workspace;
}

bool client_can_focus(const Client *client)
{
    return client && client->focusable;
}

bool monitor_init_authority(Monitor *monitor, unsigned int index,
                            Rect geometry, unsigned int workspace_count)
{
    if (!monitor || !workspace_count ||
        workspace_count > BOX2430_MAX_WORKSPACES)
        return false;

    memset(monitor, 0, sizeof(*monitor));
    monitor->workspaces = calloc(workspace_count, sizeof(*monitor->workspaces));
    if (!monitor->workspaces) return false;

    monitor->index = index;
    monitor->geometry = geometry;
    monitor->workarea = geometry;
    for (unsigned int i = 0; i < workspace_count; ++i) {
        Workspace *workspace = &monitor->workspaces[i];
        workspace->monitor = monitor;
        workspace->index = i;
        workspace->mode = WORKSPACE_FREE;
    }
    monitor->active_workspace = &monitor->workspaces[0];
    return true;
}

void monitor_finish_authority(Monitor *monitor)
{
    if (!monitor) return;
    free(monitor->workspaces);
    monitor->workspaces = NULL;
    monitor->active_workspace = NULL;
}

void workspace_attach_client(Workspace *workspace, Client *client)
{
    if (!workspace || !client) return;

    client->workspace_next = workspace->clients;
    workspace->clients = client;

    client->tab_prev = workspace->tab_tail;
    client->tab_next = NULL;
    if (workspace->tab_tail) workspace->tab_tail->tab_next = client;
    else workspace->tab_head = client;
    workspace->tab_tail = client;

    client->stack_prev = workspace->stack_tail;
    client->stack_next = NULL;
    if (workspace->stack_tail) workspace->stack_tail->stack_next = client;
    else workspace->stack_head = client;
    workspace->stack_tail = client;
}

static void workspace_unlink_focus(Workspace *workspace, Client *client)
{
    bool linked = workspace && client &&
        (workspace->focus_head == client || workspace->focus_tail == client ||
         client->focus_prev || client->focus_next);
    if (!linked) return;

    if (client->focus_prev) client->focus_prev->focus_next = client->focus_next;
    else workspace->focus_head = client->focus_next;
    if (client->focus_next) client->focus_next->focus_prev = client->focus_prev;
    else workspace->focus_tail = client->focus_prev;
    client->focus_prev = NULL;
    client->focus_next = NULL;
}

void workspace_promote_focus(Workspace *workspace, Client *client)
{
    if (!workspace || !client) return;
    workspace_unlink_focus(workspace, client);
    client->focus_prev = NULL;
    client->focus_next = workspace->focus_head;
    if (workspace->focus_head) workspace->focus_head->focus_prev = client;
    else workspace->focus_tail = client;
    workspace->focus_head = client;
}

void workspace_raise_client(Workspace *workspace, Client *client)
{
    if (!workspace || !client || client->workspace != workspace ||
        workspace->stack_tail == client)
        return;

    if (client->stack_prev) client->stack_prev->stack_next = client->stack_next;
    else if (workspace->stack_head == client) workspace->stack_head = client->stack_next;
    if (client->stack_next) client->stack_next->stack_prev = client->stack_prev;

    client->stack_prev = workspace->stack_tail;
    client->stack_next = NULL;
    if (workspace->stack_tail) workspace->stack_tail->stack_next = client;
    else workspace->stack_head = client;
    workspace->stack_tail = client;
}

Client *workspace_focus_relative_target(Workspace *workspace,
                                        Client *current, bool forward)
{
    if (!workspace) return NULL;
    Client *cursor = current && current->workspace == workspace
        ? current : workspace_focus_target(workspace);

    unsigned int count = 0;
    for (Client *client = workspace->tab_head; client; client = client->tab_next)
        ++count;
    for (unsigned int i = 0; i < count; ++i) {
        cursor = cursor ? (forward ? cursor->tab_next : cursor->tab_prev) : NULL;
        if (!cursor) cursor = forward ? workspace->tab_head : workspace->tab_tail;
        if (client_can_focus(cursor)) return cursor;
    }
    return NULL;
}

void workspace_detach_client(Workspace *workspace, Client *client)
{
    if (!workspace || !client) return;

    Client **link = &workspace->clients;
    while (*link && *link != client) link = &(*link)->workspace_next;
    if (*link) *link = client->workspace_next;

    if (client->tab_prev) client->tab_prev->tab_next = client->tab_next;
    else if (workspace->tab_head == client) workspace->tab_head = client->tab_next;
    if (client->tab_next) client->tab_next->tab_prev = client->tab_prev;
    else if (workspace->tab_tail == client) workspace->tab_tail = client->tab_prev;

    if (client->stack_prev) client->stack_prev->stack_next = client->stack_next;
    else if (workspace->stack_head == client) workspace->stack_head = client->stack_next;
    if (client->stack_next) client->stack_next->stack_prev = client->stack_prev;
    else if (workspace->stack_tail == client) workspace->stack_tail = client->stack_prev;

    workspace_unlink_focus(workspace, client);
    client->workspace_next = NULL;
    client->tab_prev = NULL;
    client->tab_next = NULL;
    client->stack_prev = NULL;
    client->stack_next = NULL;
}

void client_reassign_workspace(Client *client, Workspace *workspace)
{
    if (!client || !workspace || client->workspace == workspace) return;
    if (client->workspace) workspace_detach_client(client->workspace, client);
    client->workspace = workspace;
    client->focus_prev = NULL;
    client->focus_next = NULL;
    workspace_attach_client(workspace, client);
}

static Client *workspace_stable_focus_fallback(Workspace *workspace,
                                                Client *removed)
{
    if (!workspace) return NULL;
    if (removed) {
        for (Client *client = removed->tab_next; client; client = client->tab_next)
            if (client_can_focus(client)) return client;
        for (Client *client = removed->tab_prev; client; client = client->tab_prev)
            if (client_can_focus(client)) return client;
        return NULL;
    }
    for (Client *client = workspace->tab_tail; client; client = client->tab_prev)
        if (client_can_focus(client)) return client;
    return NULL;
}

Client *workspace_focus_fallback(Workspace *workspace, Client *removed)
{
    if (!workspace) return NULL;
    for (Client *client = workspace->focus_head; client; client = client->focus_next)
        if (client != removed && client_can_focus(client)) return client;
    return workspace_stable_focus_fallback(workspace, removed);
}

Client *workspace_focus_target(Workspace *workspace)
{
    return workspace_focus_fallback(workspace, NULL);
}

bool client_should_be_visible(const Client *client)
{
    if (!client_workspace_is_active(client)) return false;
    if (client->workspace->mode == WORKSPACE_FREE) return true;
    return workspace_focus_target(client->workspace) == client;
}


static bool rect_equal(Rect left, Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

void model_free_monitor_topology_plan(MonitorTopologyPlan *plan)
{
    if (!plan) return;
    free(plan->clients);
    plan->clients = NULL;
    plan->client_count = 0;
}

MonitorTopologyPlanResult model_plan_monitor_topology(
    const WMModel *model, unsigned int new_count,
    const Rect new_rects[BOX2430_MAX_MONITORS],
    const int old_for_new[BOX2430_MAX_MONITORS],
    const int new_for_old[BOX2430_MAX_MONITORS],
    MonitorTopologyPlan *plan)
{
    if (!model || !plan || model->monitor_count == 0 || !new_count ||
        new_count > BOX2430_MAX_MONITORS)
        return MONITOR_TOPOLOGY_PLAN_FAILED;

    memset(plan, 0, sizeof(*plan));
    plan->old_count = model->monitor_count;
    plan->new_count = new_count;
    for (unsigned int i = 0; i < plan->old_count; ++i)
        plan->old_rects[i] = model->monitors[i].geometry;
    for (unsigned int i = 0; i < plan->new_count; ++i) {
        plan->new_rects[i] = new_rects[i];
        plan->old_for_new[i] = old_for_new[i];
    }
    for (unsigned int i = 0; i < plan->old_count; ++i)
        plan->new_for_old[i] = new_for_old[i];

    bool changed = plan->old_count != plan->new_count;
    for (unsigned int new_index = 0; new_index < plan->new_count; ++new_index) {
        int old_index = plan->old_for_new[new_index];
        if (old_index < 0 || old_index >= (int)plan->old_count ||
            old_index != (int)new_index ||
            !rect_equal(plan->old_rects[old_index], plan->new_rects[new_index]))
            changed = true;
    }
    if (!changed) return MONITOR_TOPOLOGY_NO_CHANGE;

    plan->fallback_new_index = 0;
    unsigned int selected_old_index = model->selected_monitor
        ? model->selected_monitor->index : 0;
    int selected_new_index = selected_old_index < plan->old_count
        ? plan->new_for_old[selected_old_index] : -1;
    plan->selected_new_index = selected_new_index >= 0
        ? (unsigned int)selected_new_index : plan->fallback_new_index;
    plan->preferred_focus = model->focused_client;

    unsigned int client_count = 0;
    for (Client *client = model->clients; client; client = client->next)
        ++client_count;
    if (!client_count) return MONITOR_TOPOLOGY_SEMANTIC_CHANGE;

    plan->clients = calloc(client_count, sizeof(*plan->clients));
    if (!plan->clients) return MONITOR_TOPOLOGY_PLAN_FAILED;
    plan->client_count = client_count;

    unsigned int i = 0;
    for (Client *client = model->clients; client; client = client->next, ++i) {
        TopologyClientPlan *client_plan = &plan->clients[i];
        if (!client->workspace || !client->workspace->monitor) {
            model_free_monitor_topology_plan(plan);
            return MONITOR_TOPOLOGY_PLAN_FAILED;
        }
        unsigned int old_index = client->workspace->monitor->index;
        if (old_index >= plan->old_count) {
            model_free_monitor_topology_plan(plan);
            return MONITOR_TOPOLOGY_PLAN_FAILED;
        }
        int continued_new = plan->new_for_old[old_index];
        unsigned int new_index = continued_new >= 0
            ? (unsigned int)continued_new : plan->fallback_new_index;
        if (new_index >= plan->new_count) {
            model_free_monitor_topology_plan(plan);
            return MONITOR_TOPOLOGY_PLAN_FAILED;
        }
        client_plan->client = client;
        client_plan->old_monitor_index = old_index;
        client_plan->new_monitor_index = new_index;
        client_plan->workspace_index = client->workspace->index;
        client_plan->migrate = continued_new < 0;
        client_plan->adjust_geometry = client_plan->migrate ||
            !rect_equal(plan->old_rects[old_index], plan->new_rects[new_index]);
    }
    return MONITOR_TOPOLOGY_SEMANTIC_CHANGE;
}

void client_translate_latent_geometry(Client *client, Rect old_monitor,
                                      Rect new_monitor)
{
    if (!client) return;
    int dx = new_monitor.x - old_monitor.x;
    int dy = new_monitor.y - old_monitor.y;
    client->geometry.x += dx;
    client->geometry.y += dy;
    client->normal_geometry.x += dx;
    client->normal_geometry.y += dy;
}

Rect workspace_monocle_content_area(const Workspace *workspace,
                                    bool reserve_tab_strip, bool tab_at_top,
                                    int tab_height)
{
    Rect area = {0};
    if (!workspace || !workspace->monitor) return area;
    area = workspace->monitor->workarea;
    if (!reserve_tab_strip || tab_height <= 0) return area;
    if (tab_height > area.height) tab_height = area.height;
    if (tab_at_top) area.y += tab_height;
    area.height -= tab_height;
    return area;
}
