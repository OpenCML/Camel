# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT
include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME transforms
  SOURCES
    module.cpp
    jvp.cpp
    jvp_instantiate.cpp
    jvp_operator.cpp
    remat_operator.cpp
    split.cpp
    split_operator.cpp
    transpose.cpp
    transpose_operator.cpp
    vmap.cpp
    vmap_operator.cpp
    vjp.cpp
)
