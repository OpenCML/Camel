# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT
include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME transforms
  SOURCES
    module.cpp
    first_with_as_param_operator.cpp
    jvp.cpp
    jvp_operator.cpp
    node_order.cpp
    remat_operator.cpp
    rule_expand.cpp
    split.cpp
    split_operator.cpp
    transpose.cpp
    transpose_operator.cpp
    vmap.cpp
    vmap_operator.cpp
)