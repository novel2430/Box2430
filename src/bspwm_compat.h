#ifndef BSPWM_COMPAT_H
#define BSPWM_COMPAT_H

#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct WM WM;
typedef struct BspwmCompat BspwmCompat;

enum {
    BSPWM_COMPAT_MAX_CLIENTS = 32,
    BSPWM_COMPAT_MAX_POLL_FDS = BSPWM_COMPAT_MAX_CLIENTS + 1,
};

BspwmCompat *bspwm_compat_create(WM *wm);
void bspwm_compat_destroy(BspwmCompat *compat);
size_t bspwm_compat_pollfds(BspwmCompat *compat, struct pollfd *pollfds,
                            size_t capacity);
bool bspwm_compat_dispatch(WM *wm, const struct pollfd *pollfds, size_t count);
void bspwm_compat_publish(WM *wm);

bool bspwm_compat_default_socket_path(const char *display_name, char *path,
                                      size_t capacity);
bool bspwm_compat_serialize_report(const WM *wm, char *report, size_t capacity,
                                   size_t *length);

#endif
