#
# Copyright 2014, NICTA
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(NICTA_BSD)
#

TARGETS := libjansson.a

HDRFILES := ${SOURCE_DIR}/jansson-2.7/src/jansson.h ${SOURCE_DIR}/include/jansson_config.h
CFILES := $(patsubst $(SOURCE_DIR)/%,%,$(wildcard $(SOURCE_DIR)/jansson-2.7/src/*.c))

include ${SEL4_COMMON}/common.mk
