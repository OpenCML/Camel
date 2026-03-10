include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME tensor
  SOURCES
    executor.cpp
    module.cpp
    operators.cpp
    runtime.cpp
    type.cpp
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
)
