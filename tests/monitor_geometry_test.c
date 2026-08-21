#include "box2430.h"

#include <stdio.h>

static bool rect_equal(Rect left, Rect right)
{
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
}

static bool expect_topology(const char *name, const Rect *raw,
                            unsigned int raw_count, Rect fallback,
                            const Rect *expected, unsigned int expected_count)
{
    Rect normalized[BOX2430_MAX_MONITORS];
    unsigned int count = normalize_monitor_rects(
        raw, raw_count, fallback, normalized, BOX2430_MAX_MONITORS);
    if (count != expected_count) {
        fprintf(stderr, "FAIL: %s returned %u monitors, expected %u\n",
                name, count, expected_count);
        return false;
    }
    for (unsigned int i = 0; i < count; ++i) {
        if (!rect_equal(normalized[i], expected[i])) {
            fprintf(stderr, "FAIL: %s monitor %u has wrong geometry\n", name, i);
            return false;
        }
    }
    return true;
}

int main(void)
{
    Rect fallback = {0, 0, 1366, 768};
    Rect duplicate[] = {
        {0, 0, 1920, 1080},
        {0, 0, 1920, 1080},
    };
    Rect one[] = {{0, 0, 1920, 1080}};
    if (!expect_topology("duplicate", duplicate, 2, fallback, one, 1)) return 1;

    Rect mixed[] = {
        {0, 0, 1920, 1080},
        {0, 0, 1920, 1080},
        {1920, 0, 1920, 1080},
    };
    Rect two[] = {
        {0, 0, 1920, 1080},
        {1920, 0, 1920, 1080},
    };
    if (!expect_topology("mixed duplicate", mixed, 3, fallback, two, 2)) return 1;

    Rect non_overlapping[] = {
        {0, 0, 1920, 1080},
        {1920, 0, 1920, 1080},
        {0, 1080, 1920, 1080},
    };
    if (!expect_topology("non-overlapping", non_overlapping, 3, fallback,
                         non_overlapping, 3)) return 1;

    Rect partially_overlapping[] = {
        {0, 0, 1920, 1080},
        {1600, 0, 1920, 1080},
    };
    if (!expect_topology("partially overlapping", partially_overlapping, 2,
                         fallback, partially_overlapping, 2)) return 1;

    if (!expect_topology("fallback", NULL, 0, fallback, &fallback, 1)) return 1;

    puts("PASS: monitor geometry normalization");
    return 0;
}
