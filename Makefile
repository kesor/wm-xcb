NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

# Core pkg-config packages
PKGLIST_BASE = xcb xcb-util xcb-randr xcb-errors xcb-xinput

# Additional packages (may not be available)
PKGLIST_EXTRA = xcb-ewmh

# Find available packages
PKG_AVAILABLE := $(shell pkg-config --exists $(PKGLIST_BASE) $(PKGLIST_EXTRA) 2>/dev/null && echo yes || echo no)

# Build pkg-config flags
PKG_CFLAGS_BASE := $(shell pkg-config --cflags $(PKGLIST_BASE) 2>/dev/null)
PKG_LDFLAGS_BASE := $(shell pkg-config --libs $(PKGLIST_BASE) 2>/dev/null)

# Add extra packages if available
PKG_CFLAGS_EXTRA := $(shell pkg-config --cflags $(PKGLIST_EXTRA) 2>/dev/null)
PKG_LDFLAGS_EXTRA := $(shell pkg-config --libs $(PKGLIST_EXTRA) 2>/dev/null)

# Add xcb-ewmh and xcb-errors include paths directly if pkg-config doesn't find them
XCB_EWMH_INCLUDE := $(shell pkg-config --variable=includedir xcb-ewmh 2>/dev/null || echo "/nix/store/qhr2rq3z42ikdwhvca50abffm1sm0a20-libxcb-wm-0.4.2-dev/include")
XCB_EWMH_LIB := /nix/store/l8cnjnd5143cfrad9a55g598wfxs3w32-libxcb-wm-0.4.2/lib

# Vendor dependencies - symlink external headers for portable builds
VENDOR_INCLUDES = -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include

CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = $(PKG_CFLAGS_BASE) $(PKG_CFLAGS_EXTRA) -I$(XCB_EWMH_INCLUDE) $(VENDOR_INCLUDES) $(CPPFLAGS)
LDFLAGS = $(PKG_LDFLAGS_BASE) $(PKG_LDFLAGS_EXTRA) -L$(XCB_EWMH_LIB) -lxcb-ewmh -pthread -lc

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

%.o: %.c %.h $(wildcard *.h)
	${CC} -c ${CFLAGS} -o $@ $<

src/xcb/%.o: src/xcb/%.c src/xcb/%.h $(wildcard *.h)
	${CC} -c ${CFLAGS} -o $@ $<

$(NAME): ${OBJ} $(NAME).o
	${CC} -o $@ ${OBJ} ${LDFLAGS}

test: ${TEST_OBJ} ${OBJ}
	${CC} -o $@ ${TEST_OBJ} $(filter-out $(NAME).o, ${OBJ}) ${LDFLAGS} \
	&& ./test

clean:
	rm -f $(NAME) ${OBJ} ${TEST_OBJ} test

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