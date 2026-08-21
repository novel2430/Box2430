CC ?= cc
PKG_CONFIG ?= pkg-config

CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Isrc
CFLAGS_COMMON = -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
	$(shell $(PKG_CONFIG) --cflags x11 xinerama xft)
LDLIBS = $(shell $(PKG_CONFIG) --libs x11 xinerama xft)

PROFILE ?= debug
PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
BUILD_DIR = build/$(PROFILE)
TARGET = $(BUILD_DIR)/box2430
SOURCES = src/main.c src/wm.c src/monitor.c src/command.c src/config.c src/x11.c \
	vendor/tomlc17/tomlc17.c
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d)

.PHONY: all clean release sanitize test test-tools install

all: $(TARGET)

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
	$(BUILD_DIR)/x11-configure-request $(BUILD_DIR)/monitor-geometry-test

test: all test-tools
	$(BUILD_DIR)/monitor-geometry-test
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

$(BUILD_DIR)/monitor-geometry-test: tests/monitor_geometry_test.c src/monitor.c \
	src/box2430.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ \
		tests/monitor_geometry_test.c src/monitor.c $(LDLIBS)

-include $(DEPS)
