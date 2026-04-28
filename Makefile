NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

# Required pkg-config packages
PKGLIST = xcb xcb-util xcb-randr xcb-errors xcb-ewmh xcb-xinput

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGLIST))
PKG_LDFLAGS := $(shell pkg-config --libs $(PKGLIST))

# Vendor dependencies - symlink external headers for portable builds
VENDOR_INCLUDES = -I. -Ivendor/xcb-errors-include -Ivendor/libxcb-errors/include

CPPFLAGS = -DVERSION=\"${VERSION}\"
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

# Generate compile_commands.json for clang tools
compile-commands: clean
	bear -- make

# Format source files with clang-format
format:
	clang-format --style=file -i ${SRC} ${TEST_SRC}

# Run clang-tidy on source files
tidy:
	clang-tidy --quiet ${SRC} ${TEST_SRC}

# Run clang static analyzer
analyze:
	clang --analyze ${CFLAGS} ${SRC}

# Run all development checks
check: format tidy analyze

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

.PHONY: all clean container-start container-exec container-build test-standalone compile-commands format tidy analyze check test