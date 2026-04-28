NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

# Core pkg-config packages (always required)
PKGLIST_CORE = xcb xcb-util xcb-randr xcb-errors

# Optional packages (detected at build time)
PKGLIST_EXTRA = xcb-ewmh xcb-xinput

# Build pkg-config flags - core packages are required
PKG_CFLAGS_CORE := $(shell pkg-config --cflags $(PKGLIST_CORE))
PKG_LDFLAGS_CORE := $(shell pkg-config --libs $(PKGLIST_CORE))

# Add optional packages if available (pkg-config fails if not installed)
PKG_CFLAGS_EXTRA := $(shell pkg-config --cflags $(PKGLIST_EXTRA) 2>/dev/null)
PKG_LDFLAGS_EXTRA := $(shell pkg-config --libs $(PKGLIST_EXTRA) 2>/dev/null)

# Vendor dependencies - symlink external headers for portable builds
VENDOR_INCLUDES = -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include

CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = $(PKG_CFLAGS_CORE) $(PKG_CFLAGS_EXTRA) $(VENDOR_INCLUDES) $(CPPFLAGS)
LDFLAGS = $(PKG_LDFLAGS_CORE) $(PKG_LDFLAGS_EXTRA) -pthread -lc

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
	$(NAME)-window-list.c \
	$(NAME)-clients.c \
	$(NAME)-hub.c \
	$(NAME)-xcb-ewmh.c \
	$(NAME)-xcb-events.c \
	$(NAME)-states.c \
	$(NAME)-xcb.c \
	$(NAME).c \
	src/xcb/xcb-handler.c

OBJ = ${SRC:.c=.o}

TEST_SRC = \
	test-$(NAME)-window-list.c \
	test-$(NAME)-hub.c

TEST_OBJ = ${TEST_SRC:.c=.o}

all: compile_flags.txt $(NAME)
	@echo "Build complete."
	@# Optionally regenerate compile_commands.json if bear is available
	@if command -v bear >/dev/null 2>&1; then \
		echo "Note: Run 'make compile-commands' to generate compile_commands.json for clang tooling"; \
	fi

# Generate compile_commands.json for clang tools using bear
# Usage: bear -- make (or make rebuild-compile-commands)
compile-commands:
	@if command -v bear >/dev/null 2>&1; then \
		echo "Generating compile_commands.json with bear..."; \
		bear -- make; \
	else \
		echo "bear not found. Install with: nix profile install nixpkgs#bear"; \
	fi

rebuild-compile-commands: clean
	@if command -v bear >/dev/null 2>&1; then \
		bear -- make; \
	else \
		echo "bear not found. Install with: nix profile install nixpkgs#bear"; \
	fi

compile_flags.txt:
	@echo "${CFLAGS}" > compile_flags.txt

%.o: %.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

src/xcb/%.o: src/xcb/%.c
	${CC} -c ${CFLAGS} -MD -MP -o $@ $<

-include $(OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)

$(NAME): ${OBJ} $(NAME).o
	${CC} -o $@ ${OBJ} ${LDFLAGS}

test: ${TEST_OBJ} ${OBJ}
	${CC} -o $@ ${TEST_OBJ} $(filter-out $(NAME).o, ${OBJ}) ${LDFLAGS} \
	&& ./test

clean:
	rm -f $(NAME) ${OBJ} ${TEST_OBJ} test compile_commands.json compile_flags.txt

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

.PHONY: all clean container-start container-exec container-build test-standalone compile-commands rebuild-compile-commands