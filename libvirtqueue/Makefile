SRC = $(wildcard src/*.c)
OBJECTS = $(patsubst %.c,%.o,${SRC})
CFLAGS += -Iinclude

all: test

test: ${OBJECTS}
	gcc -o $@ $^

static-check:
	frama-c -eva -lib-entry -main="virtqueue_init_device" -cpp-extra-args="-Iinclude" ${SRC}

wp-check:
	frama-c -wp -cpp-extra-args="-Iinclude" ${SRC}
