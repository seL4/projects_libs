#
# Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libsdhcdrivers C)

file(GLOB deps src/*.c src/plat/${KernelPlatform}/*.c)

list(SORT deps)

add_library(sdhcdrivers STATIC EXCLUDE_FROM_ALL ${deps})
target_include_directories(sdhcdrivers PUBLIC include plat_include/${KernelPlatform})
target_link_libraries(sdhcdrivers muslc utils platsupport)
