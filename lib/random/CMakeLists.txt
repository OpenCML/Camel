include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(
  NAME random
  SOURCES executor.cpp module.cpp operators.cpp
  LINK_MODULES tensor
)
