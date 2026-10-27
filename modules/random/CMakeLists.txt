include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME random
  SOURCES executor.cpp module.cpp operators.cpp
  EXTRA_LIBS tensor_cmo
)
