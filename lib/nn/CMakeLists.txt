include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(
  NAME nn
  SOURCES
    executor.cpp
    layers.cpp
    losses.cpp
    module.cpp
    optim.cpp
    state.cpp
    tensor_ops.cpp
  LINK_MODULES
    tensor
)
