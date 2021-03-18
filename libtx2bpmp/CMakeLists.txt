#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: GPL-2.0-only
#

cmake_minimum_required(VERSION 3.7.2)

project(libtx2bpmp C)

file(GLOB deps src/*.c)

add_library(tx2bpmp STATIC EXCLUDE_FROM_ALL ${deps})

target_include_directories(tx2bpmp PUBLIC include)
target_link_libraries(tx2bpmp muslc platsupport "-Wl,--undefined=bpmp_ptr")
