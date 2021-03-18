#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libmsgpack C)

file(GLOB deps msgpack-c/src/*.c)

list(SORT deps)

add_library(msgpack EXCLUDE_FROM_ALL ${deps})
target_include_directories(msgpack PUBLIC "msgpack-c/include/")
target_link_libraries(msgpack muslc)
