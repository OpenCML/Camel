include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(NAME algo SOURCES executor.cpp module.cpp operators.cpp sequence.cpp)
