# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

# The test channel: the test-only passes a plan names as `test::<name>`. They are a `.cmo` of their
# own so that libcamel does not carry them -- see module.cpp for what that buys, and the top-level
# CMakeLists.txt for why this directory is entered before the `.cmo` manifest is written.
#
# There is no `.cml` beside this: the passes are not names a program exports or imports through a
# module, and the pass-declaration check exempts the `test` module by name for exactly that reason.

include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)

# One translation unit per pass, and the list of them is not written here: the directory is the list.
# A pass added as a file is in the module with no edit here, which is what the glob this replaces
# did (cmake/layout-blocks.cmake compiled this directory into libcamel), so the two cannot drift.
# `CONFIGURE_DEPENDS` makes a new file a reason to re-configure rather than a pass that silently
# does not exist until the next build from scratch.
#
# `module.cpp` is taken out of the glob and named on its own: it is the module's entry point and not
# one of the passes, and a source named twice is a target CMake refuses.
set(_camel_test_pass_sources "")
file(GLOB _camel_test_pass_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM _camel_test_pass_sources "${CMAKE_CURRENT_SOURCE_DIR}/module.cpp")
if(NOT _camel_test_pass_sources)
    message(FATAL_ERROR
        "src/stdlib/test has no pass source beside its module.cpp: the `test::` passes would be missing from "
        "test.cmo and every plan that names one would fail with `Unrecognized pass`. This is a build error and "
        "not an empty module.")
endif()

add_camel_module(
    NAME test
    SOURCES ${_camel_test_pass_sources} module.cpp
    EXTRA_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
)