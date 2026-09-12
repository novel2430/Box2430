CC ?= cc
PKG_CONFIG ?= pkg-config

CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc
CFLAGS_COMMON = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
	$(shell $(PKG_CONFIG) --cflags x11 'xrandr >= 1.5' xft xcursor)
LDLIBS = $(shell $(PKG_CONFIG) --libs x11 'xrandr >= 1.5' xft xcursor)

PROFILE ?= debug
PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
BUILD_DIR = build/$(PROFILE)
TARGET = $(BUILD_DIR)/box2430
RIVER_TARGET = $(BUILD_DIR)/box2430-river
RIVER_GEN_DIR = $(BUILD_DIR)/river-protocol
RIVER_PROTOCOLS_DIR ?= $(shell d=$$($(PKG_CONFIG) --variable=pkgdatadir river-protocols 2>/dev/null); \
	if [ -n "$$d" ]; then printf '%s/stable' "$$d"; else printf '%s' '/usr/share/river-protocols/stable'; fi)
RIVER_WM_XML = $(RIVER_PROTOCOLS_DIR)/river-window-management-v1.xml
RIVER_LAYER_XML = $(RIVER_PROTOCOLS_DIR)/river-layer-shell-v1.xml
RIVER_WM_HEADER = $(RIVER_GEN_DIR)/river-window-management-v1-client-protocol.h
RIVER_WM_CODE = $(RIVER_GEN_DIR)/river-window-management-v1-protocol.c
RIVER_LAYER_HEADER = $(RIVER_GEN_DIR)/river-layer-shell-v1-client-protocol.h
RIVER_LAYER_CODE = $(RIVER_GEN_DIR)/river-layer-shell-v1-protocol.c
RIVER_SOURCES = src/river/main.c src/river/runtime.c src/core.c \
	$(RIVER_WM_CODE) $(RIVER_LAYER_CODE)
SOURCES = src/main.c src/core.c src/wm.c src/ui.c src/decoration.c src/tray.c src/bspwm_compat.c src/monitor.c src/monitor_randr.c src/command.c src/config.c src/x11.c \
	vendor/tomlc17/tomlc17.c
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d)

.PHONY: all clean release sanitize test test-tools install river river-check-deps

all: $(TARGET)

river: $(RIVER_TARGET)

river-check-deps:
	@command -v wayland-scanner >/dev/null 2>&1 || { \
		echo 'box2430: make river requires wayland-scanner' >&2; exit 1; }
	@$(PKG_CONFIG) --exists wayland-client || { \
		echo 'box2430: make river requires the wayland-client development package' >&2; exit 1; }
	@test -f "$(RIVER_WM_XML)" || { \
		echo 'box2430: river-window-management-v1.xml not found.' >&2; \
		echo 'Set RIVER_PROTOCOLS_DIR=/path/to/river/protocol or install river-protocols.' >&2; exit 1; }
	@test -f "$(RIVER_LAYER_XML)" || { \
		echo 'box2430: river-layer-shell-v1.xml not found.' >&2; \
		echo 'Set RIVER_PROTOCOLS_DIR=/path/to/river/protocol or install river-protocols.' >&2; exit 1; }

$(RIVER_WM_HEADER): | river-check-deps
	@mkdir -p $(dir $@)
	wayland-scanner client-header "$(RIVER_WM_XML)" $@

$(RIVER_WM_CODE): | river-check-deps
	@mkdir -p $(dir $@)
	wayland-scanner private-code "$(RIVER_WM_XML)" $@

$(RIVER_LAYER_HEADER): | river-check-deps
	@mkdir -p $(dir $@)
	wayland-scanner client-header "$(RIVER_LAYER_XML)" $@

$(RIVER_LAYER_CODE): | river-check-deps
	@mkdir -p $(dir $@)
	wayland-scanner private-code "$(RIVER_LAYER_XML)" $@

$(RIVER_TARGET): $(RIVER_SOURCES) $(RIVER_WM_HEADER) $(RIVER_LAYER_HEADER) | river-check-deps
	@mkdir -p $(dir $@)
	$(CC) -D_POSIX_C_SOURCE=200809L -Isrc -I$(RIVER_GEN_DIR) \
		-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
		$(shell $(PKG_CONFIG) --cflags wayland-client 2>/dev/null) $(CFLAGS) \
		$(LDFLAGS) -o $@ $(RIVER_SOURCES) \
		$(shell $(PKG_CONFIG) --libs wayland-client 2>/dev/null)

release:
	$(MAKE) PROFILE=release CFLAGS='-O2 -DNDEBUG' all

sanitize:
	$(MAKE) PROFILE=sanitize CC=clang \
		CFLAGS='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' all

ifeq ($(PROFILE),debug)
CFLAGS += -Og -g
endif

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -MMD -MP -c -o $@ $<

clean:
	rm -rf build

install: release
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 build/release/box2430 $(DESTDIR)$(BINDIR)/box2430
	install -d $(DESTDIR)$(DATADIR)/box2430
	install -m 0644 config.example.toml $(DESTDIR)$(DATADIR)/box2430/config.example.toml

test-tools: $(BUILD_DIR)/x11-test-client $(BUILD_DIR)/x11-set-urgency \
	$(BUILD_DIR)/x11-focus-client $(BUILD_DIR)/x11-set-numlock-modifier \
	$(BUILD_DIR)/x11-keymap-probe $(BUILD_DIR)/x11-property-mutator \
	$(BUILD_DIR)/x11-size-hints-client $(BUILD_DIR)/x11-sigchld-client \
	$(BUILD_DIR)/x11-lifecycle-client $(BUILD_DIR)/x11-focus-compat-client \
	$(BUILD_DIR)/x11-root-color $(BUILD_DIR)/x11-workspace-transition-observer \
	$(BUILD_DIR)/x11-configure-request $(BUILD_DIR)/x11-stacking-order \
	$(BUILD_DIR)/x11-net-wm-state $(BUILD_DIR)/x11-window-color \
	$(BUILD_DIR)/x11-window-hash \
	$(BUILD_DIR)/x11-cursor-shape \
	$(BUILD_DIR)/x11-tray-test-client $(BUILD_DIR)/x11-randr-monitor \
	$(BUILD_DIR)/randr-monitor-test \
	$(BUILD_DIR)/core-model-test \
	$(BUILD_DIR)/monitor-geometry-test \
	$(BUILD_DIR)/ui-label-test \
	$(BUILD_DIR)/bspwm-compat-test \
	$(BUILD_DIR)/decoration-test \
	$(BUILD_DIR)/bspwm-compat-client

test: all test-tools
	$(BUILD_DIR)/core-model-test
	$(BUILD_DIR)/monitor-geometry-test
	$(BUILD_DIR)/ui-label-test
	$(BUILD_DIR)/bspwm-compat-test
	$(BUILD_DIR)/decoration-test
	tests/run_xvfb.sh

$(BUILD_DIR)/x11-test-client: tests/x11_test_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-set-urgency: tests/x11_set_urgency.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-focus-client: tests/x11_focus_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-set-numlock-modifier: tests/x11_set_numlock_modifier.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-keymap-probe: tests/x11_keymap_probe.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-property-mutator: tests/x11_property_mutator.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-size-hints-client: tests/x11_size_hints_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-sigchld-client: tests/x11_sigchld_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-lifecycle-client: tests/x11_lifecycle_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-focus-compat-client: tests/x11_focus_compat_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-root-color: tests/x11_root_color.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-workspace-transition-observer: \
		tests/x11_workspace_transition_observer.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-configure-request: tests/x11_configure_request.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-stacking-order: tests/x11_stacking_order.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-net-wm-state: tests/x11_net_wm_state.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-window-color: tests/x11_window_color.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-cursor-shape: tests/x11_cursor_shape.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS) $(shell $(PKG_CONFIG) --cflags --libs xfixes)

$(BUILD_DIR)/x11-window-hash: tests/x11_window_hash.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)


$(BUILD_DIR)/x11-tray-test-client: tests/x11_tray_test_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/randr-monitor-test: tests/randr_monitor_test.c \
		src/monitor.c src/monitor_randr.c src/box2430.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ \
		tests/randr_monitor_test.c src/monitor.c src/monitor_randr.c $(LDLIBS)

$(BUILD_DIR)/x11-randr-monitor: tests/x11_randr_monitor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/core-model-test: tests/core_model_test.c src/core.c src/core.h
	@mkdir -p $(dir $@)
	$(CC) -Isrc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
		$(CFLAGS) -o $@ tests/core_model_test.c src/core.c

$(BUILD_DIR)/monitor-geometry-test: tests/monitor_geometry_test.c src/monitor.c \
	src/box2430.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ \
		tests/monitor_geometry_test.c src/monitor.c $(LDLIBS)

$(BUILD_DIR)/ui-label-test: tests/ui_label_test.c src/ui.c src/tray.c src/ui.h src/tray.h src/box2430.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ \
		tests/ui_label_test.c src/ui.c src/tray.c $(LDLIBS)

$(BUILD_DIR)/bspwm-compat-test: tests/bspwm_compat_test.c \
		src/bspwm_compat.c src/bspwm_compat.h src/box2430.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ \
		tests/bspwm_compat_test.c src/bspwm_compat.c $(LDLIBS)

$(BUILD_DIR)/bspwm-compat-client: tests/bspwm_compat_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $<

-include $(DEPS)

$(BUILD_DIR)/decoration-test: tests/decoration_test.c $(filter-out $(BUILD_DIR)/src/main.o,$(OBJECTS))
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)
