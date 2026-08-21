#include "box2430.h"

#include <stdint.h>

static bool rect_equal(Rect left, Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

unsigned int normalize_monitor_rects(const Rect *raw_rects,
                                     unsigned int raw_count, Rect fallback,
                                     Rect *normalized, unsigned int capacity)
{
    if (!capacity) return 0;
    if (!raw_rects || !raw_count) {
        normalized[0] = fallback;
        return 1;
    }

    unsigned int count = 0;
    for (unsigned int i = 0; i < raw_count; ++i) {
        bool unique = true;
        for (unsigned int j = 0; j < count; ++j) {
            if (rect_equal(normalized[j], raw_rects[i])) {
                unique = false;
                break;
            }
        }
        if (!unique) continue;
        if (count == capacity) {
            normalized[0] = fallback;
            return 1;
        }
        normalized[count++] = raw_rects[i];
    }
    if (!count) {
        normalized[0] = fallback;
        return 1;
    }
    return count;
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

void match_monitor_rects(const Rect *old_rects, unsigned int old_count,
                         const Rect *new_rects, unsigned int new_count,
                         int old_for_new[BOX2430_MAX_MONITORS],
                         int new_for_old[BOX2430_MAX_MONITORS])
{
    for (unsigned int i = 0; i < BOX2430_MAX_MONITORS; ++i) {
        old_for_new[i] = -1;
        new_for_old[i] = -1;
    }

    /* Exact geometry is the strongest continuity signal and handles a pure
     * Xinerama enumeration reorder without moving monitor-local state. */
    for (unsigned int old_index = 0; old_index < old_count; ++old_index) {
        for (unsigned int new_index = 0; new_index < new_count; ++new_index) {
            if (old_for_new[new_index] >= 0 ||
                !rect_equal(old_rects[old_index], new_rects[new_index])) {
                continue;
            }
            new_for_old[old_index] = (int)new_index;
            old_for_new[new_index] = (int)old_index;
            break;
        }
    }

    /* Match all remaining possible pairs greedily. Positive overlap wins over
     * non-overlap; otherwise nearest centers win. Old/new array positions are
     * used only as deterministic tie-breakers, never as identity. */
    for (;;) {
        bool found = false;
        unsigned int best_old = 0;
        unsigned int best_new = 0;
        int64_t best_overlap = -1;
        int64_t best_distance = 0;

        for (unsigned int old_index = 0; old_index < old_count; ++old_index) {
            if (new_for_old[old_index] >= 0) continue;
            for (unsigned int new_index = 0; new_index < new_count; ++new_index) {
                if (old_for_new[new_index] >= 0) continue;
                int64_t overlap = overlap_area(old_rects[old_index],
                                               new_rects[new_index]);
                int64_t distance = center_distance_squared(old_rects[old_index],
                                                           new_rects[new_index]);
                bool better = !found || overlap > best_overlap ||
                    (overlap == best_overlap && distance < best_distance) ||
                    (overlap == best_overlap && distance == best_distance &&
                     (old_index < best_old ||
                      (old_index == best_old && new_index < best_new)));
                if (!better) continue;
                found = true;
                best_old = old_index;
                best_new = new_index;
                best_overlap = overlap;
                best_distance = distance;
            }
        }

        if (!found) break;
        new_for_old[best_old] = (int)best_new;
        old_for_new[best_new] = (int)best_old;
    }
}
