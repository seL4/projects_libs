TARGETS := libmsgpack.a

CFILES := msgpack-c/src/objectc.c \
          msgpack-c/src/unpack.c \
          msgpack-c/src/version.c \
          msgpack-c/src/vrefbuffer.c \
          msgpack-c/src/zone.c

HDRFILES := $(sort $(wildcard ${SOURCE_DIR}/msgpack-c/include/*))

include ${SEL4_COMMON}/common.mk
