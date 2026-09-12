#include "core.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    Monitor owned_monitor = {0};
    if (!monitor_init_authority(&owned_monitor, 3, (Rect){10, 20, 640, 480}, 2))
        return fail("core monitor authority initialization failed");
    if (owned_monitor.index != 3 || owned_monitor.active_workspace == NULL ||
        owned_monitor.workspaces[1].monitor != &owned_monitor ||
        owned_monitor.workspaces[1].index != 1)
        return fail("core monitor authority initialization lost workspace ownership");

    Client owned_first = {.focusable = true};
    Client owned_second = {.focusable = true};
    owned_first.workspace = &owned_monitor.workspaces[0];
    owned_second.workspace = &owned_monitor.workspaces[0];
    workspace_attach_client(&owned_monitor.workspaces[0], &owned_first);
    workspace_attach_client(&owned_monitor.workspaces[0], &owned_second);
    workspace_promote_focus(&owned_monitor.workspaces[0], &owned_first);
    if (owned_monitor.workspaces[0].tab_head != &owned_first ||
        owned_monitor.workspaces[0].tab_tail != &owned_second ||
        owned_monitor.workspaces[0].stack_tail != &owned_second ||
        workspace_focus_target(&owned_monitor.workspaces[0]) != &owned_first)
        return fail("core workspace order helpers disagree");

    workspace_raise_client(&owned_monitor.workspaces[0], &owned_first);
    if (owned_monitor.workspaces[0].stack_head != &owned_second ||
        owned_monitor.workspaces[0].stack_tail != &owned_first ||
        owned_second.stack_next != &owned_first ||
        owned_first.stack_prev != &owned_second)
        return fail("core stack raise helper lost bottom-to-top authority");
    if (workspace_focus_relative_target(&owned_monitor.workspaces[0],
                                        &owned_first, true) != &owned_second ||
        workspace_focus_relative_target(&owned_monitor.workspaces[0],
                                        &owned_first, false) != &owned_second)
        return fail("core focus cycle target disagrees with stable tab order");

    client_reassign_workspace(&owned_second, &owned_monitor.workspaces[1]);
    if (owned_second.workspace != &owned_monitor.workspaces[1] ||
        owned_monitor.workspaces[0].tab_tail != &owned_first ||
        owned_monitor.workspaces[1].tab_head != &owned_second)
        return fail("core workspace reassignment lost order authority");
    workspace_detach_client(&owned_monitor.workspaces[0], &owned_first);
    workspace_detach_client(&owned_monitor.workspaces[1], &owned_second);
    monitor_finish_authority(&owned_monitor);
    if (owned_monitor.workspaces != NULL || owned_monitor.active_workspace != NULL)
        return fail("core monitor authority cleanup left workspace state attached");

    Monitor monitor = {0};
    Workspace workspaces[2] = {0};
    Client first = {0};
    Client second = {0};

    monitor.workspaces = workspaces;
    monitor.active_workspace = &workspaces[0];
    for (unsigned int i = 0; i < 2; ++i) {
        workspaces[i].monitor = &monitor;
        workspaces[i].index = i;
        workspaces[i].mode = WORKSPACE_FREE;
    }

    first.workspace = &workspaces[0];
    second.workspace = &workspaces[0];
    first.focusable = true;
    second.focusable = true;
    first.tab_next = &second;
    second.tab_prev = &first;
    workspaces[0].tab_head = &first;
    workspaces[0].tab_tail = &second;
    workspaces[0].focus_head = &second;
    workspaces[0].focus_tail = &first;
    second.focus_next = &first;
    first.focus_prev = &second;

    if (!client_workspace_is_active(&first) ||
        !client_should_be_visible(&first) ||
        !client_should_be_visible(&second))
        return fail("FREE visibility is not derived from workspace authority");

    if (workspace_focus_target(&workspaces[0]) != &second)
        return fail("focus history did not choose the semantic focus target");

    workspaces[0].mode = WORKSPACE_MONOCLE;
    if (client_should_be_visible(&first) ||
        !client_should_be_visible(&second))
        return fail("MONOCLE visibility does not follow the focus target");

    second.focusable = false;
    if (workspace_focus_target(&workspaces[0]) != &first ||
        !client_should_be_visible(&first) ||
        client_should_be_visible(&second))
        return fail("normalized focusability did not drive MONOCLE fallback");

    monitor.active_workspace = &workspaces[1];
    if (client_workspace_is_active(&first) || client_should_be_visible(&first))
        return fail("inactive workspace client remained semantically visible");

    if (sizeof(first.geometry) != sizeof(Rect) ||
        memcmp(&first.geometry, &(Rect){0}, sizeof(Rect)) != 0)
        return fail("core geometry storage is not available independently");

    monitor.workarea = (Rect){0, 20, 800, 580};
    Rect content = workspace_monocle_content_area(
        &workspaces[1], true, true, 24);
    if (memcmp(&content, &(Rect){0, 44, 800, 556}, sizeof(Rect)) != 0)
        return fail("top MONOCLE tab reservation changed content geometry");
    content = workspace_monocle_content_area(&workspaces[1], true, false, 24);
    if (memcmp(&content, &(Rect){0, 20, 800, 556}, sizeof(Rect)) != 0)
        return fail("bottom MONOCLE tab reservation changed content geometry");

    Monitor old_monitors[2] = {0};
    Workspace old_workspaces[2][2] = {0};
    Client topology_client = {0};
    for (unsigned int i = 0; i < 2; ++i) {
        old_monitors[i].index = i;
        old_monitors[i].geometry = (Rect){(int)i * 800, 0, 800, 600};
        old_monitors[i].workarea = old_monitors[i].geometry;
        old_monitors[i].workspaces = old_workspaces[i];
        old_monitors[i].active_workspace = &old_workspaces[i][0];
        for (unsigned int j = 0; j < 2; ++j) {
            old_workspaces[i][j].monitor = &old_monitors[i];
            old_workspaces[i][j].index = j;
        }
    }
    topology_client.workspace = &old_workspaces[1][1];
    topology_client.geometry = (Rect){900, 50, 300, 200};
    topology_client.normal_geometry = topology_client.geometry;
    WMModel model = {
        .monitors = old_monitors,
        .monitor_count = 2,
        .selected_monitor = &old_monitors[1],
        .clients = &topology_client,
        .focused_client = &topology_client,
    };
    Rect new_rects[BOX2430_MAX_MONITORS] = {{0, 0, 1024, 768}};
    int old_for_new[BOX2430_MAX_MONITORS] = {0};
    int new_for_old[BOX2430_MAX_MONITORS];
    for (unsigned int i = 0; i < BOX2430_MAX_MONITORS; ++i)
        new_for_old[i] = -1;
    new_for_old[0] = 0;

    MonitorTopologyPlan plan;
    if (model_plan_monitor_topology(&model, 1, new_rects, old_for_new,
                                    new_for_old, &plan) !=
        MONITOR_TOPOLOGY_SEMANTIC_CHANGE)
        return fail("neutral topology planner rejected monitor removal");
    if (plan.selected_new_index != 0 || plan.client_count != 1 ||
        !plan.clients[0].migrate || plan.clients[0].new_monitor_index != 0 ||
        plan.clients[0].workspace_index != 1 || !plan.clients[0].adjust_geometry) {
        model_free_monitor_topology_plan(&plan);
        return fail("neutral topology planner lost Box continuity policy");
    }
    model_free_monitor_topology_plan(&plan);

    client_translate_latent_geometry(
        &topology_client, (Rect){800, 0, 800, 600}, (Rect){0, 0, 1024, 768});
    if (memcmp(&topology_client.geometry, &(Rect){100, 50, 300, 200},
               sizeof(Rect)) != 0 ||
        memcmp(&topology_client.normal_geometry, &(Rect){100, 50, 300, 200},
               sizeof(Rect)) != 0)
        return fail("latent geometry translation is not backend-neutral");

    puts("PASS: backend-neutral Box model, presentation, and topology semantics");
    return 0;
}
