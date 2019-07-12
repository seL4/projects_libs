#
# Copyright 2017, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#

cmake_minimum_required(VERSION 3.7.2)

project(libusbdrivers C)

config_option(LibUSB LIB_USB "Build USB interface library" DEFAULT ON)

add_config_library(usbdrivers "${configure_string}")
if(LibUSB)

    file(
        GLOB
            deps
            src/*.c
            src/drivers/*.c
            src/drivers/arch/${KernelArch}/*.c
            src/ehci/*.c
            src/plat/${KernelPlatform}/*.c
    )

    list(SORT deps)

    add_library(usbdrivers STATIC EXCLUDE_FROM_ALL ${deps})
    target_include_directories(usbdrivers PUBLIC include plat_include/${KernelPlatform})
    target_link_libraries(usbdrivers sel4 muslc platsupport usbdrivers_Config lwip_Config)
endif()
