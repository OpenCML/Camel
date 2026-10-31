# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT
include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME transforms
  SOURCES
    module.cpp
    jvp.cpp
)
