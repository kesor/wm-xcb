NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

PKGLIST = xcb xcb-atom xcb-ewmh xcb-xinput

CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = $(shell pkg-config --cflags ${PKGLIST}) -Ivendor/libxcb-errors/include ${CPPFLAGS}
LDFLAGS = $(shell pkg-config --libs ${PKGLIST}) -Lvendor/libxcb-errors/lib -lxcb-errors -pthread -lc

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
	$(NAME)-xcb-ewmh.c \
	$(NAME)-xcb-events.c \
	$(NAME)-states.c \
	$(NAME)-xcb.c \
	$(NAME).c

OBJ = ${SRC:.c=.o}

TEST_SRC = \
	test-$(NAME)-window-list.c

TEST_OBJ = ${TEST_SRC:.c=.o}

all: $(NAME)

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

.PHONY: all clean container-start container-exec container-build
