#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int connect_socket(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) < 0 ||
        strlen(path) >= sizeof(address.sun_path) ||
        connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_argument(int fd, const char *argument)
{
    size_t length = strlen(argument) + 1;
    for (size_t i = 0; i < length; ++i) {
        ssize_t sent;
        do sent = send(fd, argument + i, 1, MSG_NOSIGNAL);
        while (sent < 0 && errno == EINTR);
        if (sent != 1) return -1;
        struct timespec pause = {.tv_nsec = 2000000};
        nanosleep(&pause, NULL);
    }
    return 0;
}

static int send_command(int fd, int argc, char **argv)
{
    for (int i = 0; i < argc; ++i)
        if (send_argument(fd, argv[i]) < 0) return -1;
    return 0;
}

static int subscribe_reports(const char *path, unsigned long count)
{
    int fd = connect_socket(path);
    if (fd < 0) return 1;
    char *command[] = {"subscribe", "report"};
    if (send_command(fd, 2, command) < 0) return 1;
    unsigned long lines = 0;
    char buffer[4096];
    while (count == 0 || lines < count) {
        struct pollfd descriptor = {.fd = fd, .events = POLLIN};
        int ready;
        do ready = poll(&descriptor, 1, 5000); while (ready < 0 && errno == EINTR);
        if (ready <= 0) return 1;
        ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) return 1;
        if (fwrite(buffer, 1, (size_t)received, stdout) != (size_t)received)
            return 1;
        fflush(stdout);
        for (ssize_t i = 0; i < received; ++i)
            if (buffer[i] == '\n') ++lines;
    }
    close(fd);
    return 0;
}

static int send_oversized(const char *path)
{
    int fd = connect_socket(path);
    if (fd < 0) return 1;
    char buffer[1024];
    memset(buffer, 'x', sizeof(buffer));
    size_t offset = 0;
    while (offset < sizeof(buffer)) {
        ssize_t sent = send(fd, buffer + offset, sizeof(buffer) - offset,
                            MSG_NOSIGNAL);
        if (sent > 0) offset += (size_t)sent;
        else if (sent < 0 && errno == EINTR) continue;
        else break;
    }
    close(fd);
    return 0;
}

static int exhaust_connections(const char *path, unsigned long count)
{
    if (count > 128) return 2;
    int fds[128];
    unsigned long opened = 0;
    for (; opened < count; ++opened) {
        fds[opened] = connect_socket(path);
        if (fds[opened] < 0) break;
    }
    struct timespec pause = {.tv_nsec = 300000000};
    nanosleep(&pause, NULL);
    unsigned int rejected = 0;
    for (unsigned long i = 0; i < opened; ++i) {
        struct pollfd descriptor = {.fd = fds[i], .events = POLLIN};
        if (poll(&descriptor, 1, 0) > 0 &&
            (descriptor.revents & (POLLHUP | POLLERR))) ++rejected;
        close(fds[i]);
    }
    printf("%u\n", rejected);
    return opened == count && rejected > 0 ? 0 : 1;
}

static int create_stale_socket(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) < 0 ||
        strlen(path) >= sizeof(address.sun_path) ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) return 1;
    close(fd);
    return 0;
}

static int hold_listener(const char *path, unsigned long seconds)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s", path) < 0 ||
        strlen(path) >= sizeof(address.sun_path) ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, 4) < 0) return 1;
    sleep((unsigned int)seconds);
    close(fd);
    unlink(path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    if (strcmp(argv[1], "subscribe") == 0 && argc == 4)
        return subscribe_reports(argv[2], strtoul(argv[3], NULL, 10));
    if (strcmp(argv[1], "command") == 0 && argc >= 4) {
        int fd = connect_socket(argv[2]);
        if (fd < 0) return 1;
        int result = send_command(fd, argc - 3, argv + 3);
        close(fd);
        return result < 0;
    }
    if (strcmp(argv[1], "oversized") == 0 && argc == 3)
        return send_oversized(argv[2]);
    if (strcmp(argv[1], "exhaust") == 0 && argc == 4)
        return exhaust_connections(argv[2], strtoul(argv[3], NULL, 10));
    if (strcmp(argv[1], "stale") == 0 && argc == 3)
        return create_stale_socket(argv[2]);
    if (strcmp(argv[1], "listen") == 0 && argc == 4)
        return hold_listener(argv[2], strtoul(argv[3], NULL, 10));
    return 2;
}
