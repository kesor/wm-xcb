NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

# Required pkg-config packages
PKGLIST = xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput xcb-keysyms

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGLIST))
PKG_LDFLAGS := $(shell pkg-config --libs $(PKGLIST))

# Always add glibc include path for clang tools
# clang-tidy uses a standalone clang binary that does not include glibc in its search path
GLIBC_DEV := $(shell clang -E -Wp,-v -x c /dev/null 2>&1 | grep "glibc.*include" | head -1 | tr -d " ")
PKG_CFLAGS += -I$(GLIBC_DEV)
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
	CFLAGS += -g3 -pedantic -Wall -O0 -DDEBUG
	LDFLAGS += -g
else
	CFLAGS += -Os
endif

# ---------------------------------------------------------------------------
# Source file lists - grouped by module for easy extension
# ---------------------------------------------------------------------------

# Root-level source files
ROOT_SRC = \
	config.c \
	wm-log.c \
	wm-signals.c \
	wm-running.c \
	wm-hub.c \
	wm-xcb-ewmh.c \
	wm-xcb-events.c \
	wm-states.c \
	wm-xcb.c \
	wm.c

# Subdirectory sources (use wildcards for easy extension)
SRC_SM      = $(wildcard src/sm/*.c)
SRC_XCB     = $(wildcard src/xcb/*.c)
SRC_TARGET  = $(wildcard src/target/*.c)
SRC_COMP    = $(wildcard src/components/*.c)
SRC_ACTIONS = $(wildcard src/actions/*.c)

# Main executable object (must be excluded from test link)
MAIN_OBJ = $(NAME).o

# All main source files
SRC = $(ROOT_SRC) $(SRC_SM) $(SRC_XCB) $(SRC_TARGET) $(SRC_COMP) $(SRC_ACTIONS)
OBJ = $(SRC:.c=.o)

# Test source files
TEST_SRC = \
	test-registry.c \
	test-runner.c \
	test-wm-hub.c \
	test-wm-xcb-handler.c \
	test-wm-monitor.c \
	test-wm-tag.c \
	test-target-client.c \
	test-client-list-component.c \
	test-focus-component.c \
	test-wm-keybinding.c \
	test-wm-monitor-manager.c

TEST_OBJ = $(TEST_SRC:.c=.o)

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------

all: compile_flags.txt $(NAME)
	@echo "Build complete."

compile_flags.txt:
	@echo "${CFLAGS}" > compile_flags.txt

# Generic pattern rule for all .c -> .o
%.o: %.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

-include $(OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)

$(NAME): $(OBJ)
	${CC} -o $@ $(OBJ) ${LDFLAGS}

# ---------------------------------------------------------------------------
# Development tools
# ---------------------------------------------------------------------------

# Generate compile_commands.json for clang tools
compile-commands: clean
	@bear -- $(MAKE) -k CC=clang all

# Format source files with clang-format
format:
	clang-format --style=file -i $(SRC) $(TEST_SRC)
	clang-format --style=file -i $(shell find . -name '*.c' -o -name '*.h' -not -path './vendor/*')

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

test: $(TEST_OBJ) $(filter-out $(MAIN_OBJ),$(OBJ))
	${CC} -o $@ $^ ${LDFLAGS}
	./test

clean:
	rm -f $(NAME) $(OBJ) $(TEST_OBJ) test compile_commands.json compile_flags.txt

# Standalone test (no XCB dependencies required)
test-standalone: wm-hub.o test-wm-hub-standalone.c
	$(CC) $(CFLAGS) -o $@ $^
	./test-standalone

test-sm-standalone: wm-hub.o wm-log.o $(filter-out %.c,$(SRC_SM:.c=.o)) test-sm-standalone.c
	$(CC) $(CFLAGS) -o $@ $^
	./test-sm-standalone

# ---------------------------------------------------------------------------
# Docker build and test targets
# ----------------------------------------------------------------------------

# Build Docker image (multi-stage: Nix builder -> scratch runtime)
container-build:
	docker build -t $(NAME):test .

# Run the container (starts Xvfb and wm)
container-run:
	docker run --rm $(NAME):test

# Test container build by running tests
container-test: container-build
	@echo "Checking binary exists in container..."
	@docker run --rm wm:test ls -la /wm/bin/wm

# Clean up Docker image
container-clean:
	docker rmi $(NAME):test

.PHONY: all clean test test-standalone test-sm-standalone check format tidy container-build container-run container-test container-clean
