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
TARGET = $(BUILD_DIR)/microbox
SOURCES = src/main.c src/wm.c src/command.c src/config.c src/x11.c vendor/tomlc17/tomlc17.c
OBJECTS = $(SOURCES:%.c=$(BUILD_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d)

.PHONY: all clean release sanitize test-tools install

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
	install -m 0755 build/release/microbox $(DESTDIR)$(BINDIR)/microbox
	install -d $(DESTDIR)$(DATADIR)/microbox
	install -m 0644 config.example.toml $(DESTDIR)$(DATADIR)/microbox/config.example.toml

test-tools: $(BUILD_DIR)/x11-test-client $(BUILD_DIR)/x11-set-urgency \
	$(BUILD_DIR)/x11-focus-client

$(BUILD_DIR)/x11-test-client: tests/x11_test_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-set-urgency: tests/x11_set_urgency.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/x11-focus-client: tests/x11_focus_client.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS_COMMON) $(CFLAGS) -o $@ $< $(LDLIBS)

-include $(DEPS)
