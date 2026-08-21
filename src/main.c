#include "box2430.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static void usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [-d display] [-c config.toml] [-a path|--autostart path]\n",
            program);
}

int main(int argc, char **argv)
{
    const char *display_name = NULL;
    const char *config_path = NULL;
    const char *autostart_path = NULL;
    bool session_start = getenv("_BOX2430_RESTART") == NULL;
    unsetenv("_BOX2430_RESTART");

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            display_name = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((strcmp(argv[i], "-a") == 0 ||
                    strcmp(argv[i], "--autostart") == 0) && i + 1 < argc) {
            autostart_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else {
            usage(stderr, argv[0]);
            return 2;
        }
    }

    WM wm = {0};
    if (!wm_init(&wm, display_name, config_path, session_start)) {
        wm_destroy(&wm);
        return 1;
    }

    wm_run(&wm, session_start ? autostart_path : NULL);
    bool restart = wm.restart_requested;
    wm_destroy(&wm);
    if (restart) {
        if (setenv("_BOX2430_RESTART", "1", 1) < 0) {
            fprintf(stderr, "box2430: cannot mark restart: %s\n", strerror(errno));
            return 1;
        }
        execvp(argv[0], argv);
        fprintf(stderr, "box2430: restart exec failed: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
