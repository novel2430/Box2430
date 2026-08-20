#include "microbox.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static void usage(FILE *stream, const char *program)
{
    fprintf(stream, "usage: %s [-d display] [-c config.toml]\n", program);
}

int main(int argc, char **argv)
{
    const char *display_name = NULL;
    const char *config_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            display_name = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout, argv[0]);
            return 0;
        } else {
            usage(stderr, argv[0]);
            return 2;
        }
    }

    WM wm = {0};
    if (!wm_init(&wm, display_name, config_path)) {
        wm_destroy(&wm);
        return 1;
    }

    wm_run(&wm);
    bool restart = wm.restart_requested;
    wm_destroy(&wm);
    if (restart) {
        execvp(argv[0], argv);
        fprintf(stderr, "microbox: restart exec failed: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
