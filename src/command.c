#include "microbox.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef CommandStatus (*CommandFn)(WM *wm, const CommandContext *context,
                                   int argc, const char *const *argv);

typedef struct CommandDef {
    const char *name;
    CommandFn handler;
} CommandDef;

static CommandStatus command_wm(WM *wm, const CommandContext *context,
                                int argc, const char *const *argv)
{
    (void)context;
    if (argc != 1) return COMMAND_INVALID;
    if (strcmp(argv[0], "restart") == 0) wm->restart_requested = true;
    else if (strcmp(argv[0], "quit") != 0) return COMMAND_INVALID;
    wm->running = false;
    return COMMAND_OK;
}

static CommandStatus command_spawn(WM *wm, const CommandContext *context,
                                   int argc, const char *const *argv)
{
    (void)context;
    if (argc < 1) return COMMAND_INVALID;
    pid_t child = fork();
    if (child < 0) return COMMAND_INVALID;
    if (child == 0) {
        close(wm->x_fd);
        setsid();
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    return COMMAND_OK;
}

static CommandStatus command_workspace(WM *wm, const CommandContext *context,
                                       int argc, const char *const *argv)
{
    (void)context;
    if (argc != 1) return COMMAND_INVALID;
    unsigned int current = wm->selected_monitor->active_workspace->index;
    unsigned int target;
    if (strcmp(argv[0], "next") == 0) {
        target = (current + 1) % wm->config.workspace_count;
    } else if (strcmp(argv[0], "prev") == 0) {
        target = (current + wm->config.workspace_count - 1) % wm->config.workspace_count;
    } else {
        char *end = NULL;
        errno = 0;
        unsigned long number = strtoul(argv[0], &end, 10);
        if (errno || !end || *end || number < 1 ||
            number > wm->config.workspace_count) {
            return COMMAND_INVALID;
        }
        target = (unsigned int)number - 1;
    }
    workspace_activate(wm, wm->selected_monitor,
                       &wm->selected_monitor->workspaces[target]);
    return COMMAND_OK;
}

static Monitor *relative_monitor(WM *wm, Monitor *monitor, bool forward)
{
    unsigned int index = monitor->index;
    index = forward ? (index + 1) % wm->monitor_count
                    : (index + wm->monitor_count - 1) % wm->monitor_count;
    return &wm->monitors[index];
}

static CommandStatus command_monitor(WM *wm, const CommandContext *context,
                                     int argc, const char *const *argv)
{
    (void)context;
    if (argc != 1) return COMMAND_INVALID;
    bool forward;
    if (strcmp(argv[0], "next") == 0) forward = true;
    else if (strcmp(argv[0], "prev") == 0) forward = false;
    else return COMMAND_INVALID;
    monitor_select(wm, relative_monitor(wm, wm->selected_monitor, forward));
    return COMMAND_OK;
}

static bool parse_workspace(WM *wm, const char *text, Workspace **workspace)
{
    char *end = NULL;
    errno = 0;
    unsigned long number = strtoul(text, &end, 10);
    if (errno || !end || *end || number < 1 || number > wm->config.workspace_count)
        return false;
    *workspace = &wm->selected_monitor->workspaces[number - 1];
    return true;
}

static CommandStatus command_window(WM *wm, const CommandContext *context,
                                    int argc, const char *const *argv)
{
    (void)context;
    Client *client = wm->focused_client;
    if (argc == 1 && strcmp(argv[0], "close") == 0) {
        if (!client) return COMMAND_INVALID;
        client_close(wm, client);
    } else if (argc == 1 && strcmp(argv[0], "raise") == 0) {
        if (!client) return COMMAND_INVALID;
        client_raise(wm, client);
    } else if (argc == 1 && strcmp(argv[0], "lower") == 0) {
        if (!client) return COMMAND_INVALID;
        client_lower(wm, client);
    } else if (argc >= 2 && argc <= 3 && strcmp(argv[0], "move-workspace") == 0) {
        Workspace *workspace;
        bool follow = argc == 3 && strcmp(argv[2], "--follow") == 0;
        if (!client || !parse_workspace(wm, argv[1], &workspace) ||
            (argc == 3 && !follow)) return COMMAND_INVALID;
        client_move_to_workspace(wm, client, workspace, follow, false);
    } else if (argc >= 2 && argc <= 4 && strcmp(argv[0], "move-monitor") == 0) {
        bool forward;
        if (strcmp(argv[1], "next") == 0) forward = true;
        else if (strcmp(argv[1], "prev") == 0) forward = false;
        else return COMMAND_INVALID;
        bool follow = false;
        bool keep_workspace = false;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--follow") == 0 && !follow) follow = true;
            else if (strcmp(argv[i], "--keep-workspace") == 0 && !keep_workspace)
                keep_workspace = true;
            else return COMMAND_INVALID;
        }
        if (!client) return COMMAND_INVALID;
        Monitor *target = relative_monitor(wm, client->workspace->monitor, forward);
        Workspace *workspace = keep_workspace
            ? &target->workspaces[client->workspace->index]
            : target->active_workspace;
        client_move_to_workspace(wm, client, workspace, follow, true);
    } else {
        return COMMAND_INVALID;
    }
    return COMMAND_OK;
}

static CommandStatus command_focus(WM *wm, const CommandContext *context,
                                   int argc, const char *const *argv)
{
    if (argc != 1) return COMMAND_INVALID;
    if (strcmp(argv[0], "next-tab") == 0) client_focus_relative(wm, true, true);
    else if (strcmp(argv[0], "prev-tab") == 0) client_focus_relative(wm, true, false);
    else if (strcmp(argv[0], "next-mru") == 0)
        client_focus_mru_cycle(wm, true, context ? context->modifiers : 0);
    else if (strcmp(argv[0], "prev-mru") == 0)
        client_focus_mru_cycle(wm, false, context ? context->modifiers : 0);
    else return COMMAND_INVALID;
    return COMMAND_OK;
}

static CommandStatus command_mode(WM *wm, const CommandContext *context,
                                  int argc, const char *const *argv)
{
    (void)context;
    Workspace *workspace = wm->selected_monitor->active_workspace;
    WorkspaceMode mode;
    if (argc == 1 && strcmp(argv[0], "free") == 0) mode = WORKSPACE_FREE;
    else if (argc == 1 && strcmp(argv[0], "monocle") == 0) mode = WORKSPACE_MONOCLE;
    else if (argc == 2 && strcmp(argv[0], "monocle") == 0 &&
             strcmp(argv[1], "toggle") == 0)
        mode = workspace->mode == WORKSPACE_FREE ? WORKSPACE_MONOCLE : WORKSPACE_FREE;
    else return COMMAND_INVALID;
    workspace_set_mode(wm, workspace, mode);
    return COMMAND_OK;
}

static bool parse_snap(const char *text, SnapState *state)
{
    static const struct { const char *name; SnapState state; } values[] = {
        {"none", SNAP_NONE}, {"left", SNAP_LEFT}, {"right", SNAP_RIGHT},
        {"top-left", SNAP_TOP_LEFT}, {"top-right", SNAP_TOP_RIGHT},
        {"bottom-left", SNAP_BOTTOM_LEFT}, {"bottom-right", SNAP_BOTTOM_RIGHT},
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        if (strcmp(text, values[i].name) == 0) {
            *state = values[i].state;
            return true;
        }
    }
    return false;
}

static CommandStatus command_snap(WM *wm, const CommandContext *context,
                                  int argc, const char *const *argv)
{
    (void)context;
    if (argc != 1 || !wm->focused_client) return COMMAND_INVALID;
    if (strcmp(argv[0], "maximize") == 0) {
        client_set_maximized(wm, wm->focused_client, true);
        return COMMAND_OK;
    }
    SnapState state;
    if (!parse_snap(argv[0], &state)) return COMMAND_INVALID;
    client_snap(wm, wm->focused_client, state);
    return COMMAND_OK;
}

static CommandStatus command_maximize(WM *wm, const CommandContext *context,
                                      int argc, const char *const *argv)
{
    (void)context;
    if (!wm->focused_client || argc > 1) return COMMAND_INVALID;
    bool target;
    if (argc == 0) target = true;
    else if (strcmp(argv[0], "toggle") == 0) target = !wm->focused_client->maximized;
    else return COMMAND_INVALID;
    client_set_maximized(wm, wm->focused_client, target);
    return COMMAND_OK;
}

static CommandStatus command_fullscreen(WM *wm, const CommandContext *context,
                                        int argc, const char *const *argv)
{
    (void)context;
    if (!wm->focused_client || argc > 1) return COMMAND_INVALID;
    bool target;
    if (argc == 0) target = true;
    else if (strcmp(argv[0], "toggle") == 0) target = !wm->focused_client->user_fullscreen;
    else return COMMAND_INVALID;
    client_set_fullscreen(wm, wm->focused_client, target);
    return COMMAND_OK;
}

static const CommandDef commands[] = {
    {"wm", command_wm},
    {"spawn", command_spawn},
    {"workspace", command_workspace},
    {"monitor", command_monitor},
    {"window", command_window},
    {"focus", command_focus},
    {"mode", command_mode},
    {"snap", command_snap},
    {"maximize", command_maximize},
    {"fullscreen", command_fullscreen},
};

static CommandStatus command_mouse(WM *wm, const CommandContext *context,
                                   int argc, const char *const *argv)
{
    if (!context || context->type != COMMAND_CONTEXT_MOUSE || !context->client ||
        argc != 1) return COMMAND_INVALID;
    if (strcmp(argv[0], "move-window") == 0)
        mouse_begin_drag(wm, context->client, false, context->root_x, context->root_y);
    else if (strcmp(argv[0], "resize-window") == 0)
        mouse_begin_drag(wm, context->client, true, context->root_x, context->root_y);
    else return COMMAND_INVALID;
    return COMMAND_OK;
}

static CommandStatus command_tab(WM *wm, const CommandContext *context,
                                 int argc, const char *const *argv)
{
    if (!context || context->type != COMMAND_CONTEXT_TABBAR || !context->client ||
        argc != 1) return COMMAND_INVALID;
    if (strcmp(argv[0], "focus") == 0) {
        client_focus_tab_target(wm, context->client, context->time);
    } else if (strcmp(argv[0], "close") == 0) {
        client_close(wm, context->client);
    } else return COMMAND_INVALID;
    return COMMAND_OK;
}

CommandStatus command_run(WM *wm, const CommandContext *context, int argc,
                          const char *const *argv)
{
    if (argc < 1 || !argv) return COMMAND_INVALID;
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            return commands[i].handler(wm, context, argc - 1, argv + 1);
        }
    }
    if (strcmp(argv[0], "mouse") == 0)
        return command_mouse(wm, context, argc - 1, argv + 1);
    if (strcmp(argv[0], "tab") == 0)
        return command_tab(wm, context, argc - 1, argv + 1);
    return COMMAND_INVALID;
}

static bool valid_workspace_number(const Config *config, const char *text)
{
    char *end = NULL;
    errno = 0;
    unsigned long number = strtoul(text, &end, 10);
    return !errno && end && !*end && number >= 1 && number <= config->workspace_count;
}

bool command_validate(const Config *config, CommandContextType context, int argc,
                      const char *const *argv)
{
    if (!config || argc < 1 || !argv) return false;
    if (strcmp(argv[0], "mouse") == 0)
        return context == COMMAND_CONTEXT_MOUSE && argc == 2 &&
               (strcmp(argv[1], "move-window") == 0 ||
                strcmp(argv[1], "resize-window") == 0);
    if (strcmp(argv[0], "tab") == 0)
        return context == COMMAND_CONTEXT_TABBAR && argc == 2 &&
               (strcmp(argv[1], "focus") == 0 || strcmp(argv[1], "close") == 0);
    if (strcmp(argv[0], "wm") == 0)
        return argc == 2 && (strcmp(argv[1], "quit") == 0 ||
                             strcmp(argv[1], "restart") == 0);
    if (strcmp(argv[0], "spawn") == 0) return argc >= 2;
    if (strcmp(argv[0], "workspace") == 0)
        return argc == 2 && (strcmp(argv[1], "next") == 0 ||
               strcmp(argv[1], "prev") == 0 || valid_workspace_number(config, argv[1]));
    if (strcmp(argv[0], "monitor") == 0)
        return argc == 2 && (strcmp(argv[1], "next") == 0 || strcmp(argv[1], "prev") == 0);
    if (strcmp(argv[0], "focus") == 0)
        return argc == 2 && (strcmp(argv[1], "next-tab") == 0 ||
               strcmp(argv[1], "prev-tab") == 0 ||
               strcmp(argv[1], "next-mru") == 0 || strcmp(argv[1], "prev-mru") == 0);
    if (strcmp(argv[0], "mode") == 0)
        return (argc == 2 && (strcmp(argv[1], "free") == 0 ||
                strcmp(argv[1], "monocle") == 0)) ||
               (argc == 3 && strcmp(argv[1], "monocle") == 0 &&
                strcmp(argv[2], "toggle") == 0);
    if (strcmp(argv[0], "snap") == 0) {
        if (argc != 2) return false;
        SnapState state;
        return strcmp(argv[1], "maximize") == 0 || parse_snap(argv[1], &state);
    }
    if (strcmp(argv[0], "maximize") == 0 || strcmp(argv[0], "fullscreen") == 0)
        return argc == 1 || (argc == 2 && strcmp(argv[1], "toggle") == 0);
    if (strcmp(argv[0], "window") != 0 || argc < 2) return false;
    if (argc == 2 && (strcmp(argv[1], "close") == 0 ||
        strcmp(argv[1], "raise") == 0 || strcmp(argv[1], "lower") == 0)) return true;
    if (strcmp(argv[1], "move-workspace") == 0)
        return (argc == 3 || (argc == 4 && strcmp(argv[3], "--follow") == 0)) &&
               valid_workspace_number(config, argv[2]);
    if (strcmp(argv[1], "move-monitor") == 0) {
        if (argc < 3 || argc > 5 ||
            (strcmp(argv[2], "next") != 0 && strcmp(argv[2], "prev") != 0)) return false;
        bool follow = false, keep = false;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--follow") == 0 && !follow) follow = true;
            else if (strcmp(argv[i], "--keep-workspace") == 0 && !keep) keep = true;
            else return false;
        }
        return true;
    }
    return false;
}
