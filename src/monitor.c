#include "box2430.h"

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
