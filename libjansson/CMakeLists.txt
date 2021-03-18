#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libjansson C)

file(GLOB deps jansson-2.7/src/*.c)

list(SORT deps)

add_library(jansson EXCLUDE_FROM_ALL ${deps})
target_include_directories(jansson PUBLIC "include/" "jansson-2.7/src/")
target_link_libraries(jansson muslc utils sel4_autoconf)
