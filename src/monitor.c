#include "box2430.h"

#include <stdint.h>
#include <string.h>

static bool rect_equal(Rect left, Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

static bool nullable_string_equal(const char *left, const char *right)
{
    if (!left || !right) return left == right;
    return strcmp(left, right) == 0;
}

static bool output_ids_equal_as_sets(const RandRMonitorObservation *left,
                                     const RandRMonitorObservation *right)
{
    if (!left->output_count || left->output_count != right->output_count)
        return false;
    for (unsigned int i = 0; i < left->output_count; ++i) {
        bool found = false;
        for (unsigned int j = 0; j < right->output_count; ++j) {
            if (left->outputs[i].id == right->outputs[j].id) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static bool output_metadata_equal_as_sets(
    const RandRMonitorObservation *left,
    const RandRMonitorObservation *right)
{
    if (left->output_count != right->output_count) return false;
    for (unsigned int i = 0; i < left->output_count; ++i) {
        bool found = false;
        for (unsigned int j = 0; j < right->output_count; ++j) {
            if (left->outputs[i].id == right->outputs[j].id &&
                nullable_string_equal(left->outputs[i].name,
                                      right->outputs[j].name)) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static int identity_score(const RandRMonitorObservation *old_monitor,
                          const RandRMonitorObservation *new_monitor)
{
    int score = 0;
    if (output_ids_equal_as_sets(old_monitor, new_monitor)) score += 2;
    if (old_monitor->name != None && old_monitor->name == new_monitor->name)
        score += 1;
    return score;
}

static int64_t overlap_area(Rect left, Rect right)
{
    int x1 = left.x > right.x ? left.x : right.x;
    int y1 = left.y > right.y ? left.y : right.y;
    int x2 = left.x + left.width < right.x + right.width
        ? left.x + left.width : right.x + right.width;
    int y2 = left.y + left.height < right.y + right.height
        ? left.y + left.height : right.y + right.height;
    int width = x2 - x1;
    int height = y2 - y1;
    if (width <= 0 || height <= 0) return 0;
    return (int64_t)width * height;
}

static int64_t center_distance_squared(Rect left, Rect right)
{
    int64_t left_x2 = 2LL * left.x + left.width;
    int64_t left_y2 = 2LL * left.y + left.height;
    int64_t right_x2 = 2LL * right.x + right.width;
    int64_t right_y2 = 2LL * right.y + right.height;
    int64_t dx = left_x2 - right_x2;
    int64_t dy = left_y2 - right_y2;
    return dx * dx + dy * dy;
}

void match_monitor_observations(
    const RandRMonitorObservation *old_monitors, unsigned int old_count,
    const RandRMonitorObservation *new_monitors, unsigned int new_count,
    int old_for_new[BOX2430_MAX_MONITORS],
    int new_for_old[BOX2430_MAX_MONITORS])
{
    for (unsigned int i = 0; i < BOX2430_MAX_MONITORS; ++i) {
        old_for_new[i] = -1;
        new_for_old[i] = -1;
    }

    /* Exact geometry remains the strongest continuity evidence. Metadata can
     * only choose between otherwise equal exact-geometry candidates. */
    for (;;) {
        bool found = false;
        unsigned int best_old = 0;
        unsigned int best_new = 0;
        int best_identity = -1;
        for (unsigned int old_index = 0; old_index < old_count; ++old_index) {
            if (new_for_old[old_index] >= 0) continue;
            for (unsigned int new_index = 0; new_index < new_count; ++new_index) {
                if (old_for_new[new_index] >= 0 ||
                    !rect_equal(old_monitors[old_index].geometry,
                                new_monitors[new_index].geometry)) {
                    continue;
                }
                int identity = identity_score(&old_monitors[old_index],
                                              &new_monitors[new_index]);
                bool better = !found || identity > best_identity ||
                    (identity == best_identity &&
                     (old_index < best_old ||
                      (old_index == best_old && new_index < best_new)));
                if (!better) continue;
                found = true;
                best_old = old_index;
                best_new = new_index;
                best_identity = identity;
            }
        }
        if (!found) break;
        new_for_old[best_old] = (int)best_new;
        old_for_new[best_new] = (int)best_old;
    }

    /* For non-exact candidates, overlap and center distance remain
     * authoritative. Metadata is consulted only after both geometry scores
     * tie; array positions are the final deterministic fallback. */
    for (;;) {
        bool found = false;
        unsigned int best_old = 0;
        unsigned int best_new = 0;
        int64_t best_overlap = -1;
        int64_t best_distance = 0;
        int best_identity = -1;

        for (unsigned int old_index = 0; old_index < old_count; ++old_index) {
            if (new_for_old[old_index] >= 0) continue;
            for (unsigned int new_index = 0; new_index < new_count; ++new_index) {
                if (old_for_new[new_index] >= 0) continue;
                int64_t overlap = overlap_area(old_monitors[old_index].geometry,
                                               new_monitors[new_index].geometry);
                int64_t distance = center_distance_squared(
                    old_monitors[old_index].geometry,
                    new_monitors[new_index].geometry);
                int identity = identity_score(&old_monitors[old_index],
                                              &new_monitors[new_index]);
                bool better = !found || overlap > best_overlap ||
                    (overlap == best_overlap && distance < best_distance) ||
                    (overlap == best_overlap && distance == best_distance &&
                     identity > best_identity) ||
                    (overlap == best_overlap && distance == best_distance &&
                     identity == best_identity &&
                     (old_index < best_old ||
                      (old_index == best_old && new_index < best_new)));
                if (!better) continue;
                found = true;
                best_old = old_index;
                best_new = new_index;
                best_overlap = overlap;
                best_distance = distance;
                best_identity = identity;
            }
        }

        if (!found) break;
        new_for_old[best_old] = (int)best_new;
        old_for_new[best_new] = (int)best_old;
    }
}

bool randr_monitor_snapshots_equal(const RandRMonitorSnapshot *left,
                                   const RandRMonitorSnapshot *right)
{
    if (left->count != right->count) return false;
    for (unsigned int i = 0; i < left->count; ++i) {
        const RandRMonitorObservation *left_monitor = &left->monitors[i];
        const RandRMonitorObservation *right_monitor = &right->monitors[i];
        if (!rect_equal(left_monitor->geometry, right_monitor->geometry) ||
            left_monitor->name != right_monitor->name ||
            !nullable_string_equal(left_monitor->name_string,
                                   right_monitor->name_string) ||
            left_monitor->primary != right_monitor->primary ||
            left_monitor->automatic != right_monitor->automatic ||
            left_monitor->synthetic != right_monitor->synthetic ||
            !output_metadata_equal_as_sets(left_monitor, right_monitor)) {
            return false;
        }
    }
    return true;
}
