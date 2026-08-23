#include "box2430.h"

#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy_string(const char *source, size_t length)
{
    char *copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, source, length);
    copy[length] = '\0';
    return copy;
}

void randr_free_monitor_snapshot(RandRMonitorSnapshot *snapshot)
{
    if (!snapshot) return;
    for (unsigned int i = 0; i < snapshot->count; ++i) {
        RandRMonitorObservation *monitor = &snapshot->monitors[i];
        free(monitor->name_string);
        for (unsigned int j = 0; j < monitor->output_count; ++j)
            free(monitor->outputs[j].name);
        free(monitor->outputs);
    }
    free(snapshot->monitors);
    memset(snapshot, 0, sizeof(*snapshot));
}

bool randr_check_version(WM *wm)
{
    int event_base;
    int error_base;
    if (!XRRQueryExtension(wm->display, &event_base, &error_base)) {
        fprintf(stderr,
                "box2430: RandR extension is unavailable; RandR 1.5 or newer is required\n");
        return false;
    }

    int major = 0;
    int minor = 0;
    if (!XRRQueryVersion(wm->display, &major, &minor)) {
        fprintf(stderr,
                "box2430: cannot query RandR server version; RandR 1.5 or newer is required\n");
        return false;
    }
    if (major < 1 || (major == 1 && minor < 5)) {
        fprintf(stderr,
                "box2430: RandR 1.5 or newer is required; server provides %d.%d\n",
                major, minor);
        return false;
    }
    return true;
}

static bool capture_synthetic_root(WM *wm, RandRMonitorSnapshot *snapshot)
{
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(wm->display, wm->root, &attributes) ||
        attributes.width < 1 || attributes.height < 1) {
        fprintf(stderr,
                "box2430: cannot capture root geometry for zero-monitor RandR observation\n");
        return false;
    }
    snapshot->monitors = calloc(1, sizeof(*snapshot->monitors));
    if (!snapshot->monitors) {
        fprintf(stderr,
                "box2430: out of memory capturing synthetic root monitor\n");
        return false;
    }
    snapshot->count = 1;
    snapshot->monitors[0].geometry = (Rect){
        0, 0, attributes.width, attributes.height,
    };
    snapshot->monitors[0].synthetic = true;
    return true;
}

bool randr_query_monitor_snapshot(WM *wm, RandRMonitorSnapshot *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    XRRMonitorInfo *monitors = NULL;
    XRRScreenResources *resources = NULL;
    int monitor_count = -1;
    bool success = false;

    /* Keep monitor/output membership and connector names within one short,
     * coherent server-side observation. The current-resources request reads
     * state already accepted by the server; it does not poll the hardware. */
    XGrabServer(wm->display);
    monitors = XRRGetMonitors(wm->display, wm->root, True, &monitor_count);
    if (monitor_count < 0) {
        fprintf(stderr, "box2430: RandR monitor query failed\n");
        goto done;
    }
    if (monitor_count == 0) {
        success = capture_synthetic_root(wm, snapshot);
        goto done;
    }
    if (!monitors) {
        fprintf(stderr, "box2430: RandR returned an invalid monitor observation\n");
        goto done;
    }
    if (monitor_count > BOX2430_MAX_MONITORS) {
        fprintf(stderr,
                "box2430: RandR reported %d active logical monitors; maximum is %d\n",
                monitor_count, BOX2430_MAX_MONITORS);
        goto done;
    }

    snapshot->monitors = calloc((size_t)monitor_count,
                                sizeof(*snapshot->monitors));
    if (!snapshot->monitors) {
        fprintf(stderr, "box2430: out of memory capturing RandR monitors\n");
        goto done;
    }
    snapshot->count = (unsigned int)monitor_count;

    bool need_output_resources = false;
    for (int i = 0; i < monitor_count; ++i) {
        if (monitors[i].width < 1 || monitors[i].height < 1 ||
            monitors[i].noutput < 0 ||
            (monitors[i].noutput > 0 && !monitors[i].outputs)) {
            fprintf(stderr,
                    "box2430: RandR returned invalid logical monitor %d\n", i);
            goto done;
        }
        if (monitors[i].noutput > 0) need_output_resources = true;
    }
    if (need_output_resources) {
        resources = XRRGetScreenResourcesCurrent(wm->display, wm->root);
        if (!resources) {
            fprintf(stderr,
                    "box2430: cannot capture RandR output metadata\n");
            goto done;
        }
    }

    for (int i = 0; i < monitor_count; ++i) {
        const XRRMonitorInfo *source = &monitors[i];
        RandRMonitorObservation *destination = &snapshot->monitors[i];
        destination->geometry = (Rect){
            source->x, source->y, source->width, source->height,
        };
        destination->name = source->name;
        destination->primary = source->primary;
        destination->automatic = source->automatic;

        char *logical_name = XGetAtomName(wm->display, source->name);
        if (!logical_name) {
            fprintf(stderr,
                    "box2430: cannot capture RandR logical monitor name\n");
            goto done;
        }
        destination->name_string = copy_string(logical_name,
                                               strlen(logical_name));
        XFree(logical_name);
        if (!destination->name_string) {
            fprintf(stderr,
                    "box2430: out of memory capturing RandR monitor name\n");
            goto done;
        }

        if (source->noutput > 0) {
            destination->outputs = calloc((size_t)source->noutput,
                                          sizeof(*destination->outputs));
            if (!destination->outputs) {
                fprintf(stderr,
                        "box2430: out of memory capturing RandR monitor outputs\n");
                goto done;
            }
            destination->output_count = (unsigned int)source->noutput;
        }
        for (int j = 0; j < source->noutput; ++j) {
            XRROutputInfo *output_info = XRRGetOutputInfo(
                wm->display, resources, source->outputs[j]);
            if (!output_info || output_info->nameLen < 0) {
                if (output_info) XRRFreeOutputInfo(output_info);
                fprintf(stderr,
                        "box2430: cannot capture RandR output 0x%lx metadata\n",
                        (unsigned long)source->outputs[j]);
                goto done;
            }
            RandROutputObservation *output = &destination->outputs[j];
            output->id = (unsigned long)source->outputs[j];
            output->name = copy_string(output_info->name,
                                       (size_t)output_info->nameLen);
            XRRFreeOutputInfo(output_info);
            if (!output->name) {
                fprintf(stderr,
                        "box2430: out of memory capturing RandR output name\n");
                goto done;
            }
        }
    }
    success = true;

done:
    if (resources) XRRFreeScreenResources(resources);
    if (monitors) XRRFreeMonitors(monitors);
    XUngrabServer(wm->display);
    XFlush(wm->display);
    if (!success) randr_free_monitor_snapshot(snapshot);
    return success;
}
