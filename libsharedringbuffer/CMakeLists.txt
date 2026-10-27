#
# Copyright 2022, UNSW
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libsharedringbuffer C)

set(configure_string "")

config_string(
    LibsharedringbufferDescCount
    LIB_SHARED_RINGBUFFER_DESC_COUNT
    "Number of buffer descriptors in the ring buffer"
    DEFAULT
    512
    UNQUOTE
)

mark_as_advanced(LibsharedringbufferDescCount)
add_config_library(shared_ringbuffer "${configure_string}")

add_library(shared_ringbuffer STATIC EXCLUDE_FROM_ALL src/shared_ringbuffer.c)
target_include_directories(shared_ringbuffer PUBLIC include)
target_link_libraries(shared_ringbuffer shared_ringbuffer_Config muslc utils sel4utils)
