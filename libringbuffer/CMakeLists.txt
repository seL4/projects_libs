#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libringbuffer C)

add_library(ringbuffer STATIC EXCLUDE_FROM_ALL src/ringbuffer.c)
target_include_directories(ringbuffer PUBLIC include)
target_link_libraries(ringbuffer muslc)
