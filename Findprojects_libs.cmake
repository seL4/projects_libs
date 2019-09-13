#
# Copyright 2019, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#

set(PROJECTS_LIBS_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE STRING "")
mark_as_advanced(PROJECTS_LIBS_DIR)

function(projects_libs_import_libraries)
    add_subdirectory(${PROJECTS_LIBS_DIR} projects_libs)
endfunction()

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(projects_libs DEFAULT_MSG PROJECTS_LIBS_DIR)
