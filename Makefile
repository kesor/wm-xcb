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
LDFLAGS = $(PKG_LDFLAGS) -pthread -lc

ifeq ($(strip $(DEBUG)),1)
	CFLAGS += -g3 -pedantic -Wall -O0 -DDEBUG
	LDFLAGS += -g
else
	CFLAGS += -Os -flto -fuse-linker-plugin
	LDFLAGS += -Os
endif

SRC = \
	$(NAME)-log.c \
	$(NAME)-signals.c \
	$(NAME)-running.c \
	$(NAME)-hub.c \
	$(NAME)-xcb-ewmh.c \
	$(NAME)-xcb-events.c \
	$(NAME)-states.c \
	$(NAME)-xcb.c \
	$(NAME).c \
	src/xcb/xcb-handler.c \
	src/sm/sm-template.c \
	src/sm/sm-registry.c \
	src/sm/sm-instance.c \
	src/target/client.c \
	src/target/monitor.c \
	src/components/keybinding.c \
	src/components/connection-sm.c \
	src/components/monitor-manager.c

OBJ = ${SRC:.c=.o}

TEST_SRC = \
	test-registry.c \
	test-runner.c \
	test-$(NAME)-hub.c \
	test-$(NAME)-xcb-handler.c \
	test-$(NAME)-monitor.c \
	test-$(NAME)-keybinding.c
	test-$(NAME)-monitor-manager.c \
	test-target-client.c

# Header dependencies
TEST_HDR = test-registry.h test-wm.h test-wm-window-list.h test-wm-hub.h test-wm-monitor.h

TEST_OBJ = ${TEST_SRC:.c=.o}

all: compile_flags.txt $(NAME)
	@echo "Build complete."

compile_flags.txt:
	@echo "${CFLAGS}" > compile_flags.txt

%.o: %.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

src/sm/%.o: src/sm/%.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

src/xcb/%.o: src/xcb/%.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

src/target/%.o: src/target/%.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

src/components/%.o: src/components/%.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

-include $(OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)

$(NAME): ${OBJ} $(NAME).o
	${CC} -o $@ ${OBJ} ${LDFLAGS}

# Generate compile_commands.json for clang tools
# Forces recompilation with clang so clang-tidy can understand the flags
compile-commands: clean
	@bear -- $(MAKE) -k CC=clang all

# Format source files with clang-format
format:
	clang-format --style=file -i ${SRC} ${TEST_SRC}

# Run clang-tidy on source files
# Uses -p . to read compile_commands.json; adds glibc include path for nix
tidy: compile-commands
	@clang-tidy -p . --quiet \
		-extra-arg=-DWM_HUB_TESTING \
		-extra-arg=-D_DEFAULT_SOURCE \
		-extra-arg=-I$(GLIBC_DEV) \
		-extra-arg=-I$(abspath .) \
		-extra-arg=-I$(abspath src) \
		-extra-arg=-I$(abspath vendor/xcb-errors-include) \
		-extra-arg=-I$(abspath vendor/libxcb-errors/include) \
		$(shell pkg-config --cflags xcb xcb-util xcb-randr xcb-ewmh xcb-keysyms xproto xcb-errors 2>/dev/null | tr " " "\n" | grep "^-I" | sed "s/^-I/-extra-arg=-I/") \
		${SRC} ${TEST_SRC}
# Run clang static analyzer (requires compile_commands.json from bear)
analyze:
	@clang-tidy -p . --quiet \
		-extra-arg=-DWM_HUB_TESTING \
		-extra-arg=-D_DEFAULT_SOURCE \
		-extra-arg=-I$(GLIBC_DEV) \
		-extra-arg=-I$(abspath .) \
		-extra-arg=-I$(abspath src) \
		-extra-arg=-I$(abspath vendor/xcb-errors-include) \
		-extra-arg=-I$(abspath vendor/libxcb-errors/include) \
		$(shell pkg-config --cflags xcb xcb-util xcb-randr xcb-ewmh xcb-keysyms xproto xcb-errors 2>/dev/null | tr " " "\n" | grep "^-I" | sed "s/^-I/-extra-arg=-I/") \
		${SRC} ${TEST_SRC}

# Run all development checks
check: format tidy analyze

# Unified test runner - builds and runs all tests in a single executable
test: test-registry.o test-runner.o test-wm-hub.o test-wm-xcb-handler.o test-wm-monitor.o test-target-client.o test-wm-monitor-manager.o test-wm-keybinding.o $(filter-out $(NAME).o, ${OBJ})
	${CC} -o $@ test-registry.o test-runner.o test-wm-hub.o test-wm-xcb-handler.o test-wm-monitor.o test-target-client.o test-wm-keybinding.o $(filter-out $(NAME).o, ${OBJ}) ${LDFLAGS}
	./test

clean:
	rm -f $(NAME) ${OBJ} ${TEST_OBJ} test compile_commands.json compile_flags.txt test-window-list test-hub test-xcb-handler

container-start:
	docker run --rm -p5900:5900 --name x11vnc -v ${PWD}:/workspace -ti x11vnc

container-exec:
	docker exec -ti x11vnc /bin/bash

container-build:
	docker build -t x11vnc .

# Standalone test (no XCB dependencies required)
test-standalone: wm-hub.o test-wm-hub-standalone.c
	$(CC) $(CFLAGS) -o $@ test-wm-hub-standalone.c wm-hub.o
	./test-standalone

# State Machine standalone test (no XCB dependencies required)
test-sm-standalone: wm-hub.o wm-log.o src/sm/sm-template.o src/sm/sm-registry.o src/sm/sm-instance.o test-sm-standalone.c
	$(CC) $(CFLAGS) -o $@ wm-hub.o wm-log.o src/sm/sm-template.o src/sm/sm-registry.o src/sm/sm-instance.o test-sm-standalone.c
	./test-sm-standalone

.PHONY: all clean container-start container-exec container-build test test-standalone test-sm-standalone
