include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(NAME math SOURCES executor.cpp module.cpp operators.cpp numeric.cpp)
