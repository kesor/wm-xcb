NAME = wm
VERSION = 0.0.1

DEBUG = 0

CC = gcc

PKGLIST = xcb

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

SRC = $(NAME).c
OBJ = ${SRC:.c=.o}

all: $(NAME)

.c.o:
	${CC} -c ${CFLAGS} $<

$(NAME): ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f $(NAME) ${OBJ}

container-start:
	docker run --rm -p5900:5900 --name x11vnc -v ${PWD}:/workspace -ti x11vnc

container-exec:
	docker exec -ti x11vnc /bin/bash

container-build:
	docker build -t x11vnc .

.PHONY: all clean container-start container-exec container-build