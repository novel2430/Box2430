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

static bool expect_matches(const char *name, const Rect *old_rects,
                           unsigned int old_count, const Rect *new_rects,
                           unsigned int new_count, const int *expected_old_for_new,
                           const int *expected_new_for_old)
{
    int old_for_new[BOX2430_MAX_MONITORS];
    int new_for_old[BOX2430_MAX_MONITORS];
    match_monitor_rects(old_rects, old_count, new_rects, new_count,
                        old_for_new, new_for_old);
    for (unsigned int i = 0; i < new_count; ++i) {
        if (old_for_new[i] != expected_old_for_new[i]) {
            fprintf(stderr,
                    "FAIL: %s new monitor %u matched old %d, expected %d\n",
                    name, i, old_for_new[i], expected_old_for_new[i]);
            return false;
        }
    }
    for (unsigned int i = 0; i < old_count; ++i) {
        if (new_for_old[i] != expected_new_for_old[i]) {
            fprintf(stderr,
                    "FAIL: %s old monitor %u matched new %d, expected %d\n",
                    name, i, new_for_old[i], expected_new_for_old[i]);
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

    Rect old_pair[] = {
        {0, 0, 1920, 1080},
        {1920, 0, 1920, 1080},
    };
    Rect reordered_pair[] = {
        {1920, 0, 1920, 1080},
        {0, 0, 1920, 1080},
    };
    int reorder_old_for_new[] = {1, 0};
    int reorder_new_for_old[] = {1, 0};
    if (!expect_matches("enumeration reorder", old_pair, 2, reordered_pair, 2,
                        reorder_old_for_new, reorder_new_for_old)) return 1;

    Rect inserted_before[] = {
        {-1280, 0, 1280, 1024},
        {0, 0, 1920, 1080},
    };
    int insertion_old_for_new[] = {-1, 0};
    int insertion_new_for_old[] = {1};
    if (!expect_matches("insertion before existing", old_pair, 1,
                        inserted_before, 2, insertion_old_for_new,
                        insertion_new_for_old)) return 1;

    Rect appended_after[] = {
        {0, 0, 1920, 1080},
        {1920, 0, 1280, 1024},
    };
    int append_old_for_new[] = {0, -1};
    int append_new_for_old[] = {0};
    if (!expect_matches("append after existing", old_pair, 1, appended_after, 2,
                        append_old_for_new, append_new_for_old)) return 1;

    Rect keep_right[] = {{1920, 0, 1920, 1080}};
    int removal_old_for_new[] = {1};
    int removal_new_for_old[] = {-1, 0};
    if (!expect_matches("remove left", old_pair, 2, keep_right, 1,
                        removal_old_for_new, removal_new_for_old)) return 1;

    Rect moved_resized[] = {{-1600, -200, 2560, 1440}};
    int moved_old_for_new[] = {0};
    int moved_new_for_old[] = {0};
    if (!expect_matches("origin and resolution change", old_pair, 1,
                        moved_resized, 1, moved_old_for_new,
                        moved_new_for_old)) return 1;

    Rect old_negative_pair[] = {
        {-1920, -1080, 1920, 1080},
        {0, -1080, 1920, 1080},
    };
    Rect shifted_pair[] = {
        {-1700, -1080, 1600, 900},
        {100, -1080, 1920, 1080},
    };
    int shifted_old_for_new[] = {0, 1};
    int shifted_new_for_old[] = {0, 1};
    if (!expect_matches("negative nearest continuity", old_negative_pair, 2,
                        shifted_pair, 2, shifted_old_for_new,
                        shifted_new_for_old)) return 1;

    /* Pure Xinerama cannot prove that this new x=0 rectangle is a moved old
     * right-hand physical display. Exact geometry continuity deterministically
     * keeps the old x=0 logical monitor instead. */
    Rect ambiguous_one[] = {{0, 0, 1920, 1080}};
    int ambiguous_old_for_new[] = {0};
    int ambiguous_new_for_old[] = {0, -1};
    if (!expect_matches("ambiguous physical identity", old_pair, 2,
                        ambiguous_one, 1, ambiguous_old_for_new,
                        ambiguous_new_for_old)) return 1;

    puts("PASS: monitor geometry normalization and logical matching");
    return 0;
}
