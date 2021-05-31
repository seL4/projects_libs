SRC = $(wildcard src/*.c)
OBJECTS = $(patsubst %.c,%.o,${SRC})
CFLAGS += -Iinclude

all: test

test: ${OBJECTS}
	gcc -o $@ $^

static-check:
	frama-c -eva -cpp-extra-args="-Iinclude" ${SRC}
