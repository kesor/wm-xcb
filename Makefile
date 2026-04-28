NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

# Core pkg-config packages (always available)
PKGLIST = xcb xcb-util xcb-randr xcb-errors

# Optional packages (may not be available in all environments)
PKG_EWMH := $(shell pkg-config --exists xcb-ewmh 2>/dev/null && echo xcb-ewmh)
PKG_ATOM := $(shell pkg-config --exists xcb-atom 2>/dev/null && echo xcb-atom)
PKG_XINPUT := $(shell pkg-config --exists xcb-xinput 2>/dev/null && echo xcb-xinput)

# Build pkg-config flags from available packages
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGLIST) $(PKG_EWMH) $(PKG_ATOM) $(PKG_XINPUT) 2>/dev/null)
PKG_LDFLAGS := $(shell pkg-config --libs $(PKGLIST) $(PKG_EWMH) $(PKG_ATOM) $(PKG_XINPUT) 2>/dev/null)

CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = $(PKG_CFLAGS) $(CPPFLAGS)
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
	$(NAME)-window-list.c \
	$(NAME)-clients.c \
	$(NAME)-hub.c \
	$(NAME)-xcb-ewmh.c \
	$(NAME)-xcb-events.c \
	$(NAME)-states.c \
	$(NAME)-xcb.c \
	$(NAME).c \
	src/xcb/xcb-handler.c

SRC += \
	$(NAME)-hub.c

OBJ = ${SRC:.c=.o}

TEST_SRC = \
	test-$(NAME)-window-list.c \
	test-$(NAME)-hub.c

TEST_OBJ = ${TEST_SRC:.c=.o}

all: $(NAME)

compile_flags.txt:
	@echo "${CFLAGS}" > compile_flags.txt

%.o: %.c %.h $(wildcard *.h)
	${CC} -c ${CFLAGS} $<

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

.PHONY: all clean container-start container-exec container-build test-standalone
