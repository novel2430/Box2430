#!/bin/sh
set -eu

for scenario in \
    xvfb_bootstrap.sh \
    xvfb_core_commands.sh \
    xvfb_workspace_transition.sh \
    xvfb_config.sh \
    xvfb_rules.sh \
    xvfb_special_windows.sh \
    xvfb_semantic_geometry.sh \
    xvfb_configure_request.sh \
    xvfb_fullscreen_transitions.sh \
    xvfb_mouse.sh \
    xvfb_numlock.sh \
    xvfb_keymap.sh \
    xvfb_normal_hints.sh \
    xvfb_focus_cycle.sh \
    xvfb_tabbar.sh \
    xvfb_lifecycle.sh \
    xvfb_visibility_withdrawal.sh \
    xvfb_restart.sh \
    xvfb_focus_urgency.sh \
    xvfb_focus_history.sh \
    xvfb_focus_protocol.sh \
    xvfb_property_cache.sh \
    xvfb_focus_compat.sh \
    xvfb_spawn.sh
do
    "$(dirname "$0")/$scenario"
done
