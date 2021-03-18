#
# Copyright 2018, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(libvirtqueue C)

add_compile_options(-std=gnu99)

add_library(virtqueue STATIC EXCLUDE_FROM_ALL src/virtqueue.c)

target_include_directories(virtqueue PUBLIC include)
target_link_libraries(virtqueue PUBLIC muslc PRIVATE utils)
