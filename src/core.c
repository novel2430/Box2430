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
