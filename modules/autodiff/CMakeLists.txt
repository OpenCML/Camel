include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME autodiff
  SOURCES
    module.cpp
    operators.cpp
    pullback.cpp
    rules.cpp
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
)
