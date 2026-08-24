#include "box2430.h"
#include "bspwm_compat.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void workspace_activate(WM *wm, Monitor *monitor, Workspace *workspace)
{
    (void)wm;
    (void)monitor;
    (void)workspace;
}

static void test_socket_paths(void)
{
    char path[128];
    assert(bspwm_compat_default_socket_path(":0", path, sizeof(path)));
    assert(strcmp(path, "/tmp/bspwm_0_0-socket") == 0);
    assert(bspwm_compat_default_socket_path(":12.3", path, sizeof(path)));
    assert(strcmp(path, "/tmp/bspwm_12_3-socket") == 0);
    assert(bspwm_compat_default_socket_path("host.example:7.2", path,
                                            sizeof(path)));
    assert(strcmp(path, "/tmp/bspwmhost.example_7_2-socket") == 0);
    assert(bspwm_compat_default_socket_path("[::1]:0.0", path, sizeof(path)));
    assert(strcmp(path, "/tmp/bspwm[::1]_0_0-socket") == 0);
    assert(bspwm_compat_default_socket_path("unix/:0", path, sizeof(path)));
    assert(strcmp(path, "/tmp/bspwm_0_0-socket") == 0);
    assert(bspwm_compat_default_socket_path("tcp/host.example:7.2", path,
                                            sizeof(path)));
    assert(strcmp(path, "/tmp/bspwmhost.example_7_2-socket") == 0);
    assert(bspwm_compat_default_socket_path("tcp/[::1]:0.0", path,
                                            sizeof(path)));
    assert(strcmp(path, "/tmp/bspwm[::1]_0_0-socket") == 0);
    assert(!bspwm_compat_default_socket_path("invalid", path, sizeof(path)));
    assert(!bspwm_compat_default_socket_path(":x", path, sizeof(path)));
    assert(!bspwm_compat_default_socket_path(":1.trailing", path,
                                             sizeof(path)));
    assert(!bspwm_compat_default_socket_path("host:1", path, 8));
}

static void test_report_projection(void)
{
    WM wm = {0};
    Monitor monitors[2] = {0};
    Workspace workspaces[2][3] = {0};
    RandRMonitorObservation observations[2] = {0};
    Client occupied = {0};
    Client urgent = {0};
    Client other = {0};

    wm.config.workspace_count = 3;
    wm.model.monitors = monitors;
    wm.model.monitor_count = 2;
    wm.model.selected_monitor = &monitors[1];
    wm.monitor_snapshot.monitors = observations;
    wm.monitor_snapshot.count = 2;
    observations[0].name_string = "left";
    observations[1].name_string = "right";
    for (unsigned int i = 0; i < 2; ++i) {
        monitors[i].index = i;
        monitors[i].workspaces = workspaces[i];
        monitors[i].active_workspace = &workspaces[i][0];
        for (unsigned int j = 0; j < 3; ++j) {
            workspaces[i][j].monitor = &monitors[i];
            workspaces[i][j].index = j;
        }
    }
    occupied.workspace = &workspaces[0][0];
    workspaces[0][0].clients = &occupied;
    urgent.workspace = &workspaces[0][1];
    urgent.urgent = true;
    workspaces[0][1].clients = &urgent;
    other.workspace = &workspaces[1][1];
    workspaces[1][1].clients = &other;
    workspaces[1][0].mode = WORKSPACE_MONOCLE;

    char report[512];
    size_t length = 0;
    assert(bspwm_compat_serialize_report(&wm, report, sizeof(report), &length));
    assert(length == strlen(report));
    assert(strcmp(report,
                  "Wmleft:O1:u2:f3:LT:Mright:F1:o2:f3:LM\n") == 0);
    assert(strstr(report, ":T") == NULL);
    assert(strstr(report, ":G") == NULL);

    assert(!bspwm_compat_serialize_report(&wm, report, 8, &length));
    wm.monitor_snapshot.count = 1;
    assert(!bspwm_compat_serialize_report(&wm, report, sizeof(report), &length));
}

int main(void)
{
    test_socket_paths();
    test_report_projection();
    puts("PASS: bspwm compatibility path and report projection");
    return 0;
}
