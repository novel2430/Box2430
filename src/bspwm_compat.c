#include "bspwm_compat.h"

#include "box2430.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum {
    BSPWM_COMPAT_MAX_COMMAND = 512,
    BSPWM_COMPAT_MAX_ARGS = 4,
    BSPWM_COMPAT_MAX_ARG_LENGTH = 256,
    BSPWM_COMPAT_MAX_REPORT = 16384,
};

typedef enum BspwmCompatClientKind {
    BSPWM_COMPAT_COMMAND,
    BSPWM_COMPAT_SUBSCRIBER,
} BspwmCompatClientKind;

typedef struct BspwmCompatClient {
    int fd;
    BspwmCompatClientKind kind;
    char input[BSPWM_COMPAT_MAX_COMMAND];
    size_t input_length;
    char output[BSPWM_COMPAT_MAX_REPORT];
    size_t output_offset;
    size_t output_length;
    char next_output[BSPWM_COMPAT_MAX_REPORT];
    size_t next_output_length;
} BspwmCompatClient;

struct BspwmCompat {
    int listener_fd;
    bool owns_socket_path;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    BspwmCompatClient clients[BSPWM_COMPAT_MAX_CLIENTS];
    int poll_slots[BSPWM_COMPAT_MAX_POLL_FDS];
    size_t poll_count;
    char last_report[BSPWM_COMPAT_MAX_REPORT];
    size_t last_report_length;
    bool report_error_logged;
};

#define COMPAT_ERROR(...) do { \
    fputs("box2430: bspwm_compat: ", stderr); \
    fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); \
} while (0)

bool bspwm_compat_serialize_report(const WM *wm, char *report, size_t capacity,
                                   size_t *length)
{
    if (!wm || !report || capacity == 0 || !length ||
        wm->monitor_snapshot.count != wm->model.monitor_count) return false;
    size_t used = 0;
#define APPEND_REPORT(...) do { \
    if (used >= capacity) return false; \
    int append_length = snprintf(report + used, capacity - used, __VA_ARGS__); \
    if (append_length < 0 || (size_t)append_length >= capacity - used) \
        return false; \
    used += (size_t)append_length; \
} while (0)
    APPEND_REPORT("W");
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        const Monitor *monitor = &wm->model.monitors[i];
        const char *name = wm->monitor_snapshot.monitors[i].name_string;
        if (!name) return false;
        APPEND_REPORT("%s%c%s", i == 0 ? "" : ":",
                      monitor == wm->model.selected_monitor ? 'M' : 'm', name);
        for (unsigned int j = 0; j < wm->config.workspace_count; ++j) {
            const Workspace *workspace = &monitor->workspaces[j];
            bool urgent = false;
            for (const Client *client = workspace->clients; client;
                 client = client->workspace_next) {
                if (client->urgent) {
                    urgent = true;
                    break;
                }
            }
            char state = urgent ? 'u' : workspace->clients ? 'o' : 'f';
            if (workspace == monitor->active_workspace)
                state = (char)(state - ('a' - 'A'));
            APPEND_REPORT(":%c%u", state, workspace->index + 1);
        }
        const char mode = monitor->active_workspace->mode == WORKSPACE_MONOCLE
            ? 'M' : 'T';
        APPEND_REPORT(":L%c", mode);
    }
    APPEND_REPORT("\n");
#undef APPEND_REPORT
    *length = used;
    return true;
}

bool bspwm_compat_default_socket_path(const char *display_name, char *path,
                                      size_t capacity)
{
    if (!display_name || !*display_name || !path || capacity == 0) return false;
    const char *colon = strrchr(display_name, ':');
    if (!colon) return false;
    size_t host_length = (size_t)(colon - display_name);
    if (host_length > INT_MAX) return false;
    const char *cursor = colon + 1;
    if (*cursor < '0' || *cursor > '9') return false;
    errno = 0;
    char *end = NULL;
    unsigned long display = strtoul(cursor, &end, 10);
    if (errno || display > INT_MAX) return false;
    unsigned long screen = 0;
    if (*end == '.') {
        cursor = end + 1;
        if (*cursor < '0' || *cursor > '9') return false;
        errno = 0;
        screen = strtoul(cursor, &end, 10);
        if (errno || screen > INT_MAX) return false;
    }
    if (*end != '\0') return false;
    int written = snprintf(path, capacity, "/tmp/bspwm%.*s_%lu_%lu-socket",
                           (int)host_length, display_name, display, screen);
    return written >= 0 && (size_t)written < capacity;
}

static bool set_fd_flags(int fd)
{
    int status = fcntl(fd, F_GETFL);
    if (status < 0 || fcntl(fd, F_SETFL, status | O_NONBLOCK) < 0) return false;
    int descriptor = fcntl(fd, F_GETFD);
    return descriptor >= 0 && fcntl(fd, F_SETFD, descriptor | FD_CLOEXEC) == 0;
}

static void close_client(BspwmCompatClient *client)
{
    if (client->fd >= 0) close(client->fd);
    *client = (BspwmCompatClient){.fd = -1};
}

static bool prepare_socket_path(BspwmCompat *compat, const WM *wm)
{
    const char *override = getenv("BSPWM_SOCKET");
    int written;
    if (override && *override) {
        written = snprintf(compat->socket_path, sizeof(compat->socket_path),
                           "%s", override);
        if (written < 0 || (size_t)written >= sizeof(compat->socket_path)) {
            COMPAT_ERROR("BSPWM_SOCKET path is too long for a Unix socket");
            return false;
        }
        return true;
    }
    const char *display_name = DisplayString(wm->display);
    if (!bspwm_compat_default_socket_path(display_name, compat->socket_path,
                                          sizeof(compat->socket_path))) {
        COMPAT_ERROR("cannot derive socket path from X display '%s'",
                     display_name ? display_name : "(null)");
        return false;
    }
    return true;
}

static bool remove_stale_socket(const char *path)
{
    struct stat status;
    if (lstat(path, &status) < 0) {
        if (errno == ENOENT) return true;
        COMPAT_ERROR("cannot inspect socket path '%s': %s", path,
                     strerror(errno));
        return false;
    }
    if (!S_ISSOCK(status.st_mode)) {
        COMPAT_ERROR("socket path '%s' exists and is not a Unix socket", path);
        return false;
    }

    int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) {
        COMPAT_ERROR("cannot probe existing socket '%s': %s", path,
                     strerror(errno));
        return false;
    }
    if (!set_fd_flags(probe)) {
        COMPAT_ERROR("cannot set probe socket flags: %s", strerror(errno));
        close(probe);
        return false;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    int result = connect(probe, (struct sockaddr *)&address, sizeof(address));
    int saved_errno = errno;
    close(probe);
    if (result == 0) {
        COMPAT_ERROR("socket path '%s' already has a live listener", path);
        return false;
    }
    if (saved_errno == EAGAIN || saved_errno == EINPROGRESS) {
        COMPAT_ERROR("socket path '%s' appears to have a live listener", path);
        return false;
    }
    if (saved_errno != ECONNREFUSED) {
        COMPAT_ERROR("cannot safely replace socket '%s': %s", path,
                     strerror(saved_errno));
        return false;
    }
    if (status.st_uid != geteuid()) {
        COMPAT_ERROR("refusing to remove stale socket '%s' owned by another user",
                     path);
        return false;
    }
    if (unlink(path) < 0) {
        COMPAT_ERROR("cannot remove stale socket '%s': %s", path,
                     strerror(errno));
        return false;
    }
    return true;
}

static int create_listener(BspwmCompat *compat)
{
    if (!remove_stale_socket(compat->socket_path)) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        COMPAT_ERROR("cannot create listener: %s", strerror(errno));
        return -1;
    }
    if (!set_fd_flags(fd)) {
        COMPAT_ERROR("cannot set listener flags: %s", strerror(errno));
        close(fd);
        return -1;
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
             compat->socket_path);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        COMPAT_ERROR("cannot bind socket '%s': %s", compat->socket_path,
                     strerror(errno));
        close(fd);
        return -1;
    }
    compat->owns_socket_path = true;
    if (listen(fd, BSPWM_COMPAT_MAX_CLIENTS) < 0) {
        COMPAT_ERROR("cannot listen on socket '%s': %s", compat->socket_path,
                     strerror(errno));
        close(fd);
        unlink(compat->socket_path);
        compat->owns_socket_path = false;
        return -1;
    }
    return fd;
}

static void queue_report(BspwmCompatClient *client, const char *report,
                         size_t length)
{
    if (client->output_offset == client->output_length ||
        client->output_offset == 0) {
        memcpy(client->output, report, length);
        client->output_offset = 0;
        client->output_length = length;
        client->next_output_length = 0;
    } else {
        memcpy(client->next_output, report, length);
        client->next_output_length = length;
    }
}

static bool flush_client(BspwmCompatClient *client)
{
    while (client->output_offset < client->output_length) {
        ssize_t sent = send(client->fd, client->output + client->output_offset,
                            client->output_length - client->output_offset,
                            MSG_NOSIGNAL);
        if (sent > 0) {
            client->output_offset += (size_t)sent;
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        return false;
    }
    client->output_offset = 0;
    client->output_length = 0;
    if (client->next_output_length > 0) {
        memcpy(client->output, client->next_output, client->next_output_length);
        client->output_length = client->next_output_length;
        client->next_output_length = 0;
        return flush_client(client);
    }
    return true;
}

BspwmCompat *bspwm_compat_create(WM *wm)
{
    BspwmCompat *compat = calloc(1, sizeof(*compat));
    if (!compat) {
        COMPAT_ERROR("cannot allocate runtime: %s", strerror(errno));
        return NULL;
    }
    compat->listener_fd = -1;
    for (size_t i = 0; i < BSPWM_COMPAT_MAX_CLIENTS; ++i)
        compat->clients[i].fd = -1;
    if (!prepare_socket_path(compat, wm) ||
        (compat->listener_fd = create_listener(compat)) < 0 ||
        !bspwm_compat_serialize_report(wm, compat->last_report,
                                       sizeof(compat->last_report),
                                       &compat->last_report_length)) {
        if (compat->listener_fd >= 0) close(compat->listener_fd);
        if (compat->owns_socket_path) unlink(compat->socket_path);
        if (compat->listener_fd >= 0)
            COMPAT_ERROR("initial report exceeds the bounded report buffer");
        free(compat);
        return NULL;
    }
    return compat;
}

void bspwm_compat_destroy(BspwmCompat *compat)
{
    if (!compat) return;
    for (size_t i = 0; i < BSPWM_COMPAT_MAX_CLIENTS; ++i)
        close_client(&compat->clients[i]);
    if (compat->listener_fd >= 0) close(compat->listener_fd);
    if (compat->owns_socket_path && unlink(compat->socket_path) < 0 &&
        errno != ENOENT)
        COMPAT_ERROR("cannot remove owned socket '%s': %s", compat->socket_path,
                     strerror(errno));
    free(compat);
}

static Monitor *monitor_by_name(WM *wm, const char *name)
{
    if (wm->monitor_snapshot.count != wm->model.monitor_count) return NULL;
    for (unsigned int i = 0; i < wm->model.monitor_count; ++i) {
        const char *candidate = wm->monitor_snapshot.monitors[i].name_string;
        if (candidate && strcmp(candidate, name) == 0)
            return &wm->model.monitors[i];
    }
    return NULL;
}

static bool parse_workspace_ordinal(const char *text, unsigned int limit,
                                    unsigned int *index)
{
    if (!text || *text < '0' || *text > '9') return false;
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || *end != '\0' || value == 0 || value > limit ||
        value > UINT_MAX) return false;
    *index = (unsigned int)value - 1;
    return true;
}

static bool activate_named_workspace(WM *wm, const char *selector)
{
    const char *separator = strrchr(selector, ':');
    if (!separator || separator[1] != '^') return false;
    size_t name_length = (size_t)(separator - selector);
    if (name_length == 0 || name_length >= BSPWM_COMPAT_MAX_ARG_LENGTH)
        return false;
    char name[BSPWM_COMPAT_MAX_ARG_LENGTH];
    memcpy(name, selector, name_length);
    name[name_length] = '\0';
    unsigned int index;
    Monitor *monitor = monitor_by_name(wm, name);
    if (!monitor ||
        !parse_workspace_ordinal(separator + 2, wm->config.workspace_count,
                                 &index)) return false;
    workspace_activate(wm, monitor, &monitor->workspaces[index]);
    return true;
}

static bool activate_relative(WM *wm, const char *selector)
{
    bool forward;
    bool occupied;
    bool local;
    if (strcmp(selector, "next") == 0) {
        forward = true; occupied = false; local = false;
    } else if (strcmp(selector, "prev") == 0) {
        forward = false; occupied = false; local = false;
    } else if (strcmp(selector, "next.local") == 0) {
        forward = true; occupied = false; local = true;
    } else if (strcmp(selector, "prev.local") == 0) {
        forward = false; occupied = false; local = true;
    } else if (strcmp(selector, "next.occupied") == 0) {
        forward = true; occupied = true; local = false;
    } else if (strcmp(selector, "prev.occupied") == 0) {
        forward = false; occupied = true; local = false;
    } else if (strcmp(selector, "next.occupied.local") == 0) {
        forward = true; occupied = true; local = true;
    } else if (strcmp(selector, "prev.occupied.local") == 0) {
        forward = false; occupied = true; local = true;
    } else {
        return false;
    }

    Monitor *selected = wm->model.selected_monitor;
    unsigned int workspace_count = wm->config.workspace_count;
    unsigned int total = local ? workspace_count
        : wm->model.monitor_count * workspace_count;
    unsigned int current = local ? selected->active_workspace->index
        : selected->index * workspace_count + selected->active_workspace->index;
    for (unsigned int distance = 1; distance < total; ++distance) {
        unsigned int flat = forward ? (current + distance) % total
            : (current + total - distance) % total;
        Monitor *monitor = local ? selected
            : &wm->model.monitors[flat / workspace_count];
        Workspace *workspace = &monitor->workspaces[flat % workspace_count];
        if (!occupied || workspace->clients) {
            workspace_activate(wm, monitor, workspace);
            return true;
        }
    }
    return true;
}

static bool run_command(WM *wm, BspwmCompatClient *client, int argc,
                        char *argv[BSPWM_COMPAT_MAX_ARGS])
{
    if (argc == 2 && strcmp(argv[0], "subscribe") == 0 &&
        strcmp(argv[1], "report") == 0) {
        client->kind = BSPWM_COMPAT_SUBSCRIBER;
        client->input_length = 0;
        queue_report(client, wm->bspwm_compat->last_report,
                     wm->bspwm_compat->last_report_length);
        if (!flush_client(client)) close_client(client);
        return false;
    }
    bool valid = false;
    if (argc == 3 && strcmp(argv[1], "-f") == 0 &&
        strcmp(argv[0], "monitor") == 0) {
        Monitor *monitor = monitor_by_name(wm, argv[2]);
        if (monitor) {
            workspace_activate(wm, monitor, monitor->active_workspace);
            valid = true;
        }
    } else if (argc == 3 && strcmp(argv[1], "-f") == 0 &&
               strcmp(argv[0], "desktop") == 0) {
        valid = strchr(argv[2], ':')
            ? activate_named_workspace(wm, argv[2])
            : activate_relative(wm, argv[2]);
    }
    close_client(client);
    return valid;
}

typedef enum InputResult {
    INPUT_INCOMPLETE,
    INPUT_PROCESSED,
    INPUT_REJECTED,
} InputResult;

static InputResult process_input(WM *wm, BspwmCompatClient *client,
                                 bool *transitioned)
{
    char *argv[BSPWM_COMPAT_MAX_ARGS] = {0};
    int argc = 0;
    size_t argument_start = 0;
    for (size_t i = 0; i < client->input_length; ++i) {
        if (client->input[i] != '\0') continue;
        if (argc >= BSPWM_COMPAT_MAX_ARGS || i == argument_start ||
            i - argument_start >= BSPWM_COMPAT_MAX_ARG_LENGTH)
            return INPUT_REJECTED;
        argv[argc++] = client->input + argument_start;
        argument_start = i + 1;
    }
    if (client->input_length - argument_start >= BSPWM_COMPAT_MAX_ARG_LENGTH)
        return INPUT_REJECTED;
    if (argc == 0) return INPUT_INCOMPLETE;
    int expected;
    if (strcmp(argv[0], "subscribe") == 0) expected = 2;
    else if (strcmp(argv[0], "monitor") == 0 ||
             strcmp(argv[0], "desktop") == 0) expected = 3;
    else return INPUT_REJECTED;
    if (argc < expected) return INPUT_INCOMPLETE;
    if (argc != expected || argument_start != client->input_length)
        return INPUT_REJECTED;
    *transitioned |= run_command(wm, client, argc, argv);
    return INPUT_PROCESSED;
}

static bool read_client(WM *wm, BspwmCompatClient *client, bool *transitioned)
{
    if (client->kind == BSPWM_COMPAT_SUBSCRIBER) {
        char extra[64];
        ssize_t received = recv(client->fd, extra, sizeof(extra), 0);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                            errno == EINTR)) return true;
        return false;
    }
    while (client->input_length < sizeof(client->input)) {
        ssize_t received = recv(client->fd,
                                client->input + client->input_length,
                                sizeof(client->input) - client->input_length, 0);
        if (received > 0) {
            client->input_length += (size_t)received;
            InputResult result = process_input(wm, client, transitioned);
            if (result == INPUT_REJECTED) return false;
            if (result == INPUT_PROCESSED) return client->fd >= 0;
            continue;
        }
        if (received == 0) return false;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
    }
    return false;
}

static void accept_clients(BspwmCompat *compat)
{
    for (;;) {
        int fd = accept(compat->listener_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                COMPAT_ERROR("accept failed: %s", strerror(errno));
            return;
        }
        if (!set_fd_flags(fd)) {
            close(fd);
            continue;
        }
        size_t slot;
        for (slot = 0; slot < BSPWM_COMPAT_MAX_CLIENTS; ++slot)
            if (compat->clients[slot].fd < 0) break;
        if (slot == BSPWM_COMPAT_MAX_CLIENTS) {
            close(fd);
            continue;
        }
        compat->clients[slot] = (BspwmCompatClient){.fd = fd};
    }
}

size_t bspwm_compat_pollfds(BspwmCompat *compat, struct pollfd *pollfds,
                            size_t capacity)
{
    if (!compat || !pollfds || capacity < BSPWM_COMPAT_MAX_POLL_FDS) return 0;
    size_t count = 0;
    for (size_t i = 0; i < BSPWM_COMPAT_MAX_CLIENTS; ++i) {
        BspwmCompatClient *client = &compat->clients[i];
        if (client->fd < 0) continue;
        short events = POLLIN;
        if (client->output_offset < client->output_length) events |= POLLOUT;
        pollfds[count] = (struct pollfd){.fd = client->fd, .events = events};
        compat->poll_slots[count++] = (int)i;
    }
    pollfds[count] = (struct pollfd){.fd = compat->listener_fd, .events = POLLIN};
    compat->poll_slots[count++] = -1;
    compat->poll_count = count;
    return count;
}

bool bspwm_compat_dispatch(WM *wm, const struct pollfd *pollfds, size_t count)
{
    BspwmCompat *compat = wm ? wm->bspwm_compat : NULL;
    if (!compat || !pollfds || count != compat->poll_count) return false;
    bool transitioned = false;
    for (size_t i = 0; i < count; ++i) {
        short revents = pollfds[i].revents;
        if (!revents) continue;
        int slot = compat->poll_slots[i];
        if (slot < 0) {
            if (revents & POLLIN) accept_clients(compat);
            continue;
        }
        BspwmCompatClient *client = &compat->clients[slot];
        if (client->fd < 0) continue;
        bool keep = true;
        if (revents & (POLLIN | POLLHUP))
            keep = read_client(wm, client, &transitioned);
        if (keep && client->fd >= 0 && (revents & POLLOUT))
            keep = flush_client(client);
        if (keep && (revents & (POLLERR | POLLNVAL))) keep = false;
        if (!keep && client->fd >= 0) close_client(client);
    }
    return transitioned;
}

void bspwm_compat_publish(WM *wm)
{
    BspwmCompat *compat = wm ? wm->bspwm_compat : NULL;
    if (!compat) return;
    char report[BSPWM_COMPAT_MAX_REPORT];
    size_t length;
    if (!bspwm_compat_serialize_report(wm, report, sizeof(report), &length)) {
        if (!compat->report_error_logged) {
            COMPAT_ERROR("report exceeds the bounded report buffer");
            compat->report_error_logged = true;
        }
        return;
    }
    compat->report_error_logged = false;
    if (length == compat->last_report_length &&
        memcmp(report, compat->last_report, length) == 0) return;
    memcpy(compat->last_report, report, length);
    compat->last_report_length = length;
    for (size_t i = 0; i < BSPWM_COMPAT_MAX_CLIENTS; ++i) {
        BspwmCompatClient *client = &compat->clients[i];
        if (client->fd >= 0 && client->kind == BSPWM_COMPAT_SUBSCRIBER)
            queue_report(client, report, length);
    }
}
