#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

set(PROJECTS_LIBS_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE STRING "")
mark_as_advanced(PROJECTS_LIBS_DIR)

function(projects_libs_import_libraries)
    add_subdirectory(${PROJECTS_LIBS_DIR} projects_libs)
endfunction()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(projects_libs DEFAULT_MSG PROJECTS_LIBS_DIR)
