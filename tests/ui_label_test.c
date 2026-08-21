#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Client *workspace_focus_target(Workspace *workspace)
{
    (void)workspace;
    return NULL;
}

static int fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main(void)
{
    Client client = {
        .title = "Window title",
        .class_name = "",
        .instance = "app-instance",
    };
    if (strcmp(ui_client_label(&client, UI_LABEL_TITLE), "Window title") != 0)
        return fail("title source mismatch");
    if (strcmp(ui_client_label(&client, UI_LABEL_CLASS), "") != 0)
        return fail("empty class source incorrectly fell back");
    if (strcmp(ui_client_label(&client, UI_LABEL_INSTANCE), "app-instance") != 0)
        return fail("instance source mismatch");

    UILabelFormat format = {0};
    strcpy(format.prefix, "[ ");
    strcpy(format.suffix, " ]");
    char *formatted = ui_format_label(&format, "Window title");
    if (!formatted || strcmp(formatted, "[ Window title ]") != 0) {
        free(formatted);
        return fail("single-%s label composition mismatch");
    }
    free(formatted);
    formatted = ui_format_label(&format, "");
    if (!formatted || strcmp(formatted, "") != 0) {
        free(formatted);
        return fail("empty source should produce an empty label");
    }
    free(formatted);

    UIStyle base = {
        .fg = "#111111",
        .bg = "#222222",
        .font_style = UI_FONT_NORMAL,
    };
    UIStyleOverride override = {
        .has_bg = true,
        .has_font_style = true,
        .bg = "#333333",
        .font_style = UI_FONT_BOLD,
    };
    UIStyle resolved = ui_resolve_style(base, &override);
    if (strcmp(resolved.fg, "#111111") != 0 ||
        strcmp(resolved.bg, "#333333") != 0 ||
        resolved.font_style != UI_FONT_BOLD)
        return fail("explicit style inheritance mismatch");

    Monitor monitor = {0};
    Workspace empty = {.monitor = &monitor, .index = 0};
    Workspace occupied = {.monitor = &monitor, .index = 1};
    Workspace urgent = {.monitor = &monitor, .index = 2};
    Workspace active = {.monitor = &monitor, .index = 3};
    Client occupied_client = {.workspace = &occupied};
    Client urgent_client = {.workspace = &urgent, .urgent = true};
    Client active_urgent_client = {.workspace = &active, .urgent = true};
    occupied.clients = &occupied_client;
    urgent.clients = &urgent_client;
    monitor.active_workspace = &active;
    if (ui_workspace_visual_state(&monitor, &empty) != UI_WORKSPACE_EMPTY)
        return fail("empty workspace state mismatch");
    if (ui_workspace_visual_state(&monitor, &occupied) != UI_WORKSPACE_OCCUPIED)
        return fail("occupied workspace state mismatch");
    if (ui_workspace_visual_state(&monitor, &urgent) != UI_WORKSPACE_URGENT)
        return fail("urgent workspace state mismatch");
    if (ui_workspace_visual_state(&monitor, &active) != UI_WORKSPACE_ACTIVE)
        return fail("active workspace state mismatch");
    active.clients = &active_urgent_client;
    if (ui_workspace_visual_state(&monitor, &active) != UI_WORKSPACE_ACTIVE_URGENT)
        return fail("active+urgent workspace state mismatch");

    puts("PASS: UI client label/format/style/workspace-state helpers");
    return 0;
}
