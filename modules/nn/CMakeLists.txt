include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME nn
  SOURCES
    executor.cpp
    module.cpp
    operators.cpp
    runtime.cpp
    type.cpp
    vjp_registry.cpp
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
  EXTRA_LIBS
    tensor_cmo
)
