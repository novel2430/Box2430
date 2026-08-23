#include "box2430.h"

#include <stdio.h>

static RandRMonitorObservation observation(Rect geometry, Atom name,
                                            RandROutputObservation *outputs,
                                            unsigned int output_count)
{
    return (RandRMonitorObservation){
        .geometry = geometry,
        .name = name,
        .outputs = outputs,
        .output_count = output_count,
    };
}

static bool expect_matches(
    const char *name, const RandRMonitorObservation *old_monitors,
    unsigned int old_count, const RandRMonitorObservation *new_monitors,
    unsigned int new_count, const int *expected_old_for_new,
    const int *expected_new_for_old)
{
    int old_for_new[BOX2430_MAX_MONITORS];
    int new_for_old[BOX2430_MAX_MONITORS];
    match_monitor_observations(old_monitors, old_count, new_monitors, new_count,
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
    RandRMonitorObservation old_pair[] = {
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
        observation((Rect){1920, 0, 1920, 1080}, 0, NULL, 0),
    };
    RandRMonitorObservation reordered_pair[] = {
        observation((Rect){1920, 0, 1920, 1080}, 0, NULL, 0),
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
    };
    int reorder_old_for_new[] = {1, 0};
    int reorder_new_for_old[] = {1, 0};
    if (!expect_matches("enumeration reorder", old_pair, 2, reordered_pair, 2,
                        reorder_old_for_new, reorder_new_for_old)) return 1;

    RandRMonitorObservation inserted_before[] = {
        observation((Rect){-1280, 0, 1280, 1024}, 0, NULL, 0),
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
    };
    int insertion_old_for_new[] = {-1, 0};
    int insertion_new_for_old[] = {1};
    if (!expect_matches("insertion before existing", old_pair, 1,
                        inserted_before, 2, insertion_old_for_new,
                        insertion_new_for_old)) return 1;

    RandRMonitorObservation appended_after[] = {
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
        observation((Rect){1920, 0, 1280, 1024}, 0, NULL, 0),
    };
    int append_old_for_new[] = {0, -1};
    int append_new_for_old[] = {0};
    if (!expect_matches("append after existing", old_pair, 1, appended_after, 2,
                        append_old_for_new, append_new_for_old)) return 1;

    RandRMonitorObservation keep_right[] = {
        observation((Rect){1920, 0, 1920, 1080}, 0, NULL, 0),
    };
    int removal_old_for_new[] = {1};
    int removal_new_for_old[] = {-1, 0};
    if (!expect_matches("remove left", old_pair, 2, keep_right, 1,
                        removal_old_for_new, removal_new_for_old)) return 1;

    RandRMonitorObservation moved_resized[] = {
        observation((Rect){-1600, -200, 2560, 1440}, 0, NULL, 0),
    };
    int moved_old_for_new[] = {0};
    int moved_new_for_old[] = {0};
    if (!expect_matches("origin and resolution change", old_pair, 1,
                        moved_resized, 1, moved_old_for_new,
                        moved_new_for_old)) return 1;

    RandRMonitorObservation old_negative_pair[] = {
        observation((Rect){-1920, -1080, 1920, 1080}, 0, NULL, 0),
        observation((Rect){0, -1080, 1920, 1080}, 0, NULL, 0),
    };
    RandRMonitorObservation shifted_pair[] = {
        observation((Rect){-1700, -1080, 1600, 900}, 0, NULL, 0),
        observation((Rect){100, -1080, 1920, 1080}, 0, NULL, 0),
    };
    int shifted_old_for_new[] = {0, 1};
    int shifted_new_for_old[] = {0, 1};
    if (!expect_matches("negative overlap continuity", old_negative_pair, 2,
                        shifted_pair, 2, shifted_old_for_new,
                        shifted_new_for_old)) return 1;

    RandRMonitorObservation same_geometry_old[] = {
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
    };
    RandRMonitorObservation same_geometry_new[] = {
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
        observation((Rect){0, 0, 1920, 1080}, 0, NULL, 0),
    };
    int same_old_for_new[] = {0, 1};
    int same_new_for_old[] = {0, 1};
    if (!expect_matches("distinct same-geometry monitors", same_geometry_old, 2,
                        same_geometry_new, 2, same_old_for_new,
                        same_new_for_old)) return 1;

    RandROutputObservation output_a_ordered[] = {{11, "DP-1"}, {12, "DP-2"}};
    RandROutputObservation output_a_reversed[] = {{12, "DP-2"}, {11, "DP-1"}};
    RandROutputObservation output_b[] = {{21, "HDMI-1"}};
    RandRMonitorObservation identified_old[] = {
        observation((Rect){0, 0, 1920, 1080}, 101,
                    output_a_ordered, 2),
        observation((Rect){0, 0, 1920, 1080}, 102, output_b, 1),
    };
    RandRMonitorObservation identified_reordered[] = {
        observation((Rect){0, 0, 1920, 1080}, 102, output_b, 1),
        observation((Rect){0, 0, 1920, 1080}, 101,
                    output_a_reversed, 2),
    };
    int identified_old_for_new[] = {1, 0};
    int identified_new_for_old[] = {1, 0};
    if (!expect_matches("same-geometry identity reorder", identified_old, 2,
                        identified_reordered, 2, identified_old_for_new,
                        identified_new_for_old)) return 1;

    RandROutputObservation output_one[] = {{1, "one"}};
    RandROutputObservation output_two[] = {{2, "two"}};
    RandRMonitorObservation geometry_wins_old[] = {
        observation((Rect){0, 0, 800, 600}, 0, output_one, 1),
        observation((Rect){800, 0, 800, 600}, 0, output_two, 1),
    };
    RandRMonitorObservation geometry_wins_new[] = {
        observation((Rect){0, 0, 800, 600}, 0, output_two, 1),
        observation((Rect){800, 0, 800, 600}, 0, output_one, 1),
    };
    int geometry_old_for_new[] = {0, 1};
    int geometry_new_for_old[] = {0, 1};
    if (!expect_matches("identity cannot beat exact geometry", geometry_wins_old,
                        2, geometry_wins_new, 2, geometry_old_for_new,
                        geometry_new_for_old)) return 1;

    RandRMonitorObservation nonexact_geometry_new[] = {
        observation((Rect){100, 0, 800, 600}, 0, output_two, 1),
        observation((Rect){700, 0, 800, 600}, 0, output_one, 1),
    };
    if (!expect_matches("identity cannot beat overlap", geometry_wins_old, 2,
                        nonexact_geometry_new, 2, geometry_old_for_new,
                        geometry_new_for_old)) return 1;

    RandRMonitorObservation tied_old[] = {
        observation((Rect){-100, 0, 100, 100}, 0, output_one, 1),
        observation((Rect){100, 0, 100, 100}, 0, output_two, 1),
    };
    RandRMonitorObservation tied_new[] = {
        observation((Rect){0, 0, 100, 100}, 0, output_two, 1),
    };
    int tied_old_for_new[] = {1};
    int tied_new_for_old[] = {-1, 0};
    if (!expect_matches("identity breaks equal geometry score", tied_old, 2,
                        tied_new, 1, tied_old_for_new,
                        tied_new_for_old)) return 1;

    RandRMonitorObservation logical_name_old[] = {
        observation((Rect){-100, 0, 100, 100}, 301, NULL, 0),
        observation((Rect){100, 0, 100, 100}, 302, NULL, 0),
    };
    RandRMonitorObservation logical_name_new[] = {
        observation((Rect){0, 0, 100, 100}, 302, NULL, 0),
    };
    if (!expect_matches("logical name breaks equal geometry score",
                        logical_name_old, 2, logical_name_new, 1,
                        tied_old_for_new, tied_new_for_old)) return 1;

    RandRMonitorObservation synthetic_old[] = {
        {.geometry = {0, 0, 1366, 768}, .synthetic = true},
    };
    RandRMonitorObservation synthetic_new[] = {
        {.geometry = {0, 0, 1280, 720}, .synthetic = true},
    };
    int synthetic_old_for_new[] = {0};
    int synthetic_new_for_old[] = {0};
    if (!expect_matches("synthetic monitor continuity", synthetic_old, 1,
                        synthetic_new, 1, synthetic_old_for_new,
                        synthetic_new_for_old)) return 1;

    RandRMonitorSnapshot ordered_snapshot = {
        .monitors = &identified_old[0], .count = 1,
    };
    RandRMonitorObservation reordered_outputs_monitor = observation(
        (Rect){0, 0, 1920, 1080}, 101, output_a_reversed, 2);
    RandRMonitorSnapshot reordered_outputs_snapshot = {
        .monitors = &reordered_outputs_monitor, .count = 1,
    };
    if (!randr_monitor_snapshots_equal(&ordered_snapshot,
                                       &reordered_outputs_snapshot)) {
        fputs("FAIL: output array order changed snapshot equality\n", stderr);
        return 1;
    }
    reordered_outputs_monitor.primary = true;
    if (randr_monitor_snapshots_equal(&ordered_snapshot,
                                      &reordered_outputs_snapshot)) {
        fputs("FAIL: metadata change was not detected\n", stderr);
        return 1;
    }

    puts("PASS: RandR logical-monitor continuity and metadata matching");
    return 0;
}
