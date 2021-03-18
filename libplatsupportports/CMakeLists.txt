#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: GPL-2.0-only
#

cmake_minimum_required(VERSION 3.7.2)

project(libplatsupportports C)

file(GLOB deps src/plat/${KernelPlatform}/*.c src/*.c)

add_library(platsupportports EXCLUDE_FROM_ALL ${deps})

target_include_directories(platsupportports PUBLIC include plat_include/${KernelPlatform})
target_link_libraries(platsupportports muslc platsupport)

if("${KernelPlatform}" STREQUAL "tx2")
    target_link_libraries(platsupportports tx2bpmp)
endif()
