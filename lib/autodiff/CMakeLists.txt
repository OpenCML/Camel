include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(
  NAME autodiff
  SOURCES
    module.cpp
    operators.cpp
    pullback.cpp
    rules.cpp
)
