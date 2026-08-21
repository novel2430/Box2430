#!/bin/sh
set -eu

for scenario in \
    xvfb_bootstrap.sh \
    xvfb_core_commands.sh \
    xvfb_config.sh \
    xvfb_rules.sh \
    xvfb_special_windows.sh \
    xvfb_semantic_geometry.sh \
    xvfb_mouse.sh \
    xvfb_numlock.sh \
    xvfb_normal_hints.sh \
    xvfb_focus_cycle.sh \
    xvfb_tabbar.sh \
    xvfb_restart.sh \
    xvfb_focus_urgency.sh \
    xvfb_focus_history.sh \
    xvfb_focus_protocol.sh \
    xvfb_spawn.sh
do
    "$(dirname "$0")/$scenario"
done
