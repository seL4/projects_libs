# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.7.2)

project(libfdtgen C)

add_compile_options(-O2)
target_compile_options(fdt PRIVATE -O2)
add_library(fdtgen STATIC EXCLUDE_FROM_ALL fdtgen.c)
target_include_directories(fdtgen PUBLIC include)
target_link_libraries(fdtgen muslc utils fdt)
