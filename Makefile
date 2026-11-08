NAME = wm
VERSION = 0.0.1

# ?=, not =: a plain assignment here beats DEBUG in the environment, so
# `DEBUG=1 make` would build release and say nothing about it. The explicit
# `make DEBUG=1` form works either way.
DEBUG ?= 0

CC = gcc

# Required pkg-config packages
PKGLIST = xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGLIST))
PKG_LDFLAGS := $(shell pkg-config --libs $(PKGLIST))

# Always add vendor include paths for xcb-errors
PKG_CFLAGS += -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include

CPPFLAGS = -DVERSION=\"${VERSION}\" -DWM_HUB_TESTING -D_DEFAULT_SOURCE
CFLAGS = $(PKG_CFLAGS) $(VENDOR_INCLUDES) $(CPPFLAGS)
# ---------------------------------------------------------------------------
# Static build for portable binary (works with glibc and musl)
#
# Use: make LDFLAGS_STATIC=-static (Alpine/musl)
# For dynamic build: make LDFLAGS_STATIC= (default)
#
# Note: -static flag must come BEFORE -l flags for static linking to work
#
LDFLAGS_STATIC =
LDFLAGS = $(LDFLAGS_STATIC) $(PKG_LDFLAGS) -pthread -lc

ifeq ($(strip $(DEBUG)),1)
	CFLAGS += -g3 -pedantic -Wall -Og -DDEBUG
	LDFLAGS += -g
else
	CFLAGS += -Os
endif

# Address and UB sanitizers: make SANITIZE=1
# Useful for the unit tests and for a live session. No clean is needed; each
# variant keeps its objects under build/<variant>/, which is what lets the two
# alternated in one checkout.
ifeq ($(strip $(SANITIZE)),1)
	CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -g
	LDFLAGS += -fsanitize=address,undefined
endif

# Objects for each flag set live in their own directory. An object compiled
# with -fsanitize, or with -DDEBUG, is not interchangeable with a plain one,
# and make cannot tell the two apart by looking at the file on disk: the
# -fsanitize link then fails on __asan_init, or, worse, succeeds and silently
# leaves the other variant's code in the binary. Separate directories make
# alternating `make` and `make SANITIZE=1` in one tree work without a clean.
BUILD_VARIANT = $(if $(filter 1,$(DEBUG)),debug,release)$(if $(filter 1,$(SANITIZE)),-sanitize,)
BUILD_DIR = build/$(BUILD_VARIANT)
# ---------------------------------------------------------------------------
# Source file lists - grouped by module for easy extension
# ---------------------------------------------------------------------------

# Source directories. Each is a wildcard, so a new file joins the build without
# an edit here; scripts/check-orphan-sources.sh fails a tracked .c that no
# wildcard reaches.
SRC_CORE    = $(wildcard src/core/*.c)
SRC_CONFIG  = $(wildcard src/config/*.c)
SRC_SM      = $(wildcard src/sm/*.c)
SRC_XCB     = $(wildcard src/xcb/*.c)
SRC_TARGET  = $(wildcard src/target/*.c)
SRC_COMP    = $(wildcard src/components/*.c)
SRC_ACTIONS = $(wildcard src/actions/*.c)

# Main executable object (must be excluded from test link)
MAIN_OBJ = $(BUILD_DIR)/src/core/$(NAME).o

# All main source files
SRC = $(SRC_CORE) $(SRC_CONFIG) $(SRC_SM) $(SRC_XCB) $(SRC_TARGET) $(SRC_COMP) $(SRC_ACTIONS)
OBJ = $(SRC:%.c=$(BUILD_DIR)/%.o)

# Test source files
TEST_SRC = \
	tests/unit/test-registry.c \
	tests/unit/test-runner.c \
	tests/unit/test-wm-hub.c \
	tests/unit/test-wm-log.c \
	tests/unit/test-wm-xcb-handler.c \
	tests/unit/test-wm-monitor.c \
	tests/unit/test-wm-tag.c \
	tests/unit/test-wm-signals.c \
	tests/unit/test-wm-loop-fds.c \
	tests/unit/test-wm-loop-poll.c \
	tests/unit/test-target-client.c \
	tests/unit/test-wm-states.c \
	tests/unit/test-client-list-component.c \
	tests/unit/test-tiling-component.c \
	tests/unit/test-focus-component.c \
	tests/unit/test-wm-components.c \
	tests/unit/test-fullscreen-component.c \
	tests/unit/test-floating-component.c \
	tests/unit/test-wm-keybinding.c \
	tests/unit/test-config-file.c \
	tests/unit/test-wm-monitor-manager.c \
	tests/unit/test-launcher.c \
	tests/unit/test-terminal.c \
	tests/unit/test-tag-manager-component.c \
	tests/unit/test-wm-xcb-ewmh.c \
	tests/unit/test-ewmh-desktop-component.c \
	tests/unit/test-event-types.c

TEST_OBJ = $(TEST_SRC:%.c=$(BUILD_DIR)/%.o)

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------

all: compile_flags.txt $(NAME)
	@echo "Build complete."

# Always rewritten: it carries CFLAGS, which change with the variant, and a
# stale copy points clangd at another variant's flags.
compile_flags.txt: FORCE
	@echo "${CFLAGS}" > compile_flags.txt

# Generic pattern rule for all .c -> .o
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

-include $(OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)

$(BUILD_DIR)/$(NAME): $(OBJ)
	${CC} -o $@ $(OBJ) ${LDFLAGS}

# Manual ./wm and the flake read the tree-root copy, so this target copies the
# variant binary out. The copy runs on every build: switching variants has to
# replace the binary, and mtimes cannot say which variant produced the one
# already there. scripts/xephyr-test.sh and tests/x/run.sh do not use this
# target; they build and run build/<variant>/wm so a harness run does not
# replace ./wm.
#
# The replace goes through a temporary name in the same directory and a rename.
# `cp` onto a path that is being executed fails with ETXTBSY, which made the
# build exit non-zero and left the previous binary in place, so the next session
# ran a WM that did not contain the change. A rename swaps the directory entry:
# a WM that is already running keeps the inode it started with, and the next one
# starts the new binary.
#
# The same recipe removes a root ./test. That copy is no longer refreshed, and
# one left by an older build would still run and print a count from the tree
# that produced it.
$(NAME): $(BUILD_DIR)/$(NAME) FORCE
	@cmp -s $< $@ || { cp $< $@.tmp && mv -f $@.tmp $@; }
	@rm -f test

FORCE:

# ---------------------------------------------------------------------------
# Development tools
# ---------------------------------------------------------------------------

# Generate compile_commands.json for clang tools
compile-commands: clean
	@bear -- $(MAKE) -k CC=clang all

# Format every C source and header, tracked or about to be. Same file list as
# format-check.sh, so the fixer and the gate never disagree.
format:
	./scripts/format-check.sh --fix

# Run clang-tidy on source files
tidy: compile-commands
	@clang-tidy -p . \
		-extra-arg=-DWM_HUB_TESTING \
		-extra-arg=-D_DEFAULT_SOURCE \
		-extra-arg=-I$(GLIBC_DEV) \
		-extra-arg=-I$(abspath .) \
		-extra-arg=-I$(abspath src) \
		-extra-arg=-I$(abspath vendor/xcb-errors-include) \
		-extra-arg=-I$(abspath vendor/libxcb-errors/include) \
		$(shell pkg-config --cflags xcb xcb-util xcb-randr xcb-ewmh xcb-keysyms xproto xcb-errors 2>/dev/null | tr " " "\n" | grep "^-I" | sed "s/^-I/-extra-arg=-I/") \
		$(SRC) $(TEST_SRC)

# Run all development checks
check: format tidy

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------

$(BUILD_DIR)/test: $(TEST_OBJ) $(filter-out $(MAIN_OBJ),$(OBJ))
	${CC} -o $@ $^ ${LDFLAGS}

# No test binary at the tree root. A root copy only got refreshed by this
# target, so a plain `make` left the previous one in place and running ./test
# printed a count from an older tree that looked like a real count. The suite
# runs the variant's own binary instead, and `make test` is the only way to run
# it. Removing ./test here covers a `make test` that did not also build wm.
test: $(BUILD_DIR)/test FORCE
	@rm -f test
	$(BUILD_DIR)/test

clean:
	rm -rf build $(NAME) $(NAME).tmp test compile_commands.json compile_flags.txt tools/xclient tools/xcb-ewmh-delay.so

# Point git at the committed hooks once per clone. The hook files live in
# .githooks/ and call lefthook, which runs the gates in lefthook.yml.
# core.hooksPath is local config, so it cannot be committed with the scripts.
hooks:
	git config --local core.hooksPath .githooks
	@echo "hooks: core.hooksPath=$$(git config --local core.hooksPath)"
	@echo "hooks: run 'lefthook run pre-push' to execute every gate now"

# print-src is read by scripts/check-async.sh so the static check scans exactly
# the files the build compiles, and by scripts/check-orphan-sources.sh so a
# tracked source that no target builds is reported.
print-src:
	@echo $(SRC)

# Everything the build compiles from C, for the complexity gate: the WM's own
# list plus the tools. print-src stays the WM's list because check-async reads
# it, and a tool's event loop is its own - a wait there is what the tool does,
# not a defect in the WM. The tools are globbed rather than listed, so one
# added with a rule of its own is measured without an edit here.
print-built-src:
	@echo $(SRC) $(wildcard tools/*.c)

print-test-src:
	@echo $(TEST_SRC)

# The name of the variant directory this invocation builds into. scripts/lib/
# wm-variant.sh asks for it so a harness reports the variant make actually
# built instead of re-deriving the name and drifting from this file.
print-variant:
	@echo $(BUILD_VARIANT)

# Static check: no blocking call reachable from the event loop (docs/PHILOSOPHY.md 2.3)
check-async:
	@./scripts/check-async.sh $(if $(V),-v,)

# Seam tests: assert what the X server sees, against a live WM in a nested
# display. See docs/TESTING.md. Expect failures until the X11 output path works.
# DEBUG and SANITIZE are passed on the command line, not exported, so they are
# forwarded explicitly: `make DEBUG=1 test-x` has to run the debug WM.
test-x: $(NAME) tools/xclient tools/xcb-ewmh-delay.so
	DEBUG=$(DEBUG) SANITIZE=$(SANITIZE) ./tests/x/run.sh $(TEST_X_ARGS)

# What the WM does here, in one call: it is the probe tests/x/probe.sh starts,
# a tool rather than a test, so it needs no baseline and makes no assertion.
#   make probe PROBE_ARGS="-n 2 -c 'xdotool mousemove 100 100'"
probe: tools/xclient
	./tests/x/probe.sh $(PROBE_ARGS)

# Standalone test (no XCB dependencies required)
# FORCE because the binary is shared by the variants, like wm and test above.
# Coverage and fuzzing build their own trees under build/; neither is part of
# `make` or the hooks, because both take a minute and the docs say when to run
# them. docs/QUALITY.md has both.
coverage: FORCE
	./scripts/check-coverage.sh

complexity: FORCE
	./scripts/check-complexity.sh

fuzz: FORCE
	./scripts/fuzz.sh

test-standalone: $(BUILD_DIR)/src/core/wm-hub.o $(BUILD_DIR)/src/core/wm-log.o tests/unit/test-wm-hub-standalone.c FORCE
	$(CC) $(CFLAGS) -o $@ $(filter-out FORCE,$^)
	./test-standalone

test-sm-standalone: $(BUILD_DIR)/src/core/wm-hub.o $(BUILD_DIR)/src/core/wm-log.o $(filter-out %.c,$(SRC_SM:%.c=$(BUILD_DIR)/%.o)) tests/unit/test-sm-standalone.c FORCE
	$(CC) $(CFLAGS) -o $@ $(filter-out FORCE,$^)
	./test-sm-standalone

# ---------------------------------------------------------------------------
# Test tools
#
# tools/xclient spawns bare X11 windows for integration and performance runs in
# a nested X server (Xephyr). It needs only libxcb, so it works where xterm does
# not (Xephyr usually has no usable fonts).
# ---------------------------------------------------------------------------

tools/xclient: tools/xclient.c
	$(CC) $(PKG_CFLAGS) -o $@ $< $(PKG_LDFLAGS)

# Preloaded by tests/x/test-86-queued-event.sh to hold one startup reply, so an
# event can be in libxcb's queue when the socket is no longer readable.
tools/xcb-ewmh-delay.so: tools/xcb-ewmh-delay.c
	$(CC) $(PKG_CFLAGS) -shared -fPIC -o $@ $< -ldl

# ---------------------------------------------------------------------------
# Docker build and test targets
# ----------------------------------------------------------------------------

# Build Docker image (multi-stage: Nix builder -> scratch runtime)
container-build:
	docker build -t $(NAME) .

# Run the container with VNC exposed on port 5900
container-run:
	docker run --rm -d -p 5900:5900 $(NAME)

# Clean up Docker image
container-clean:
	docker rmi $(NAME)

.PHONY: all clean test test-x check-async print-src print-built-src print-test-src hooks test-standalone test-sm-standalone coverage complexity fuzz check format tidy probe tools/xclient tools/xcb-ewmh-delay.so container-build container-run container-test container-clean FORCE
