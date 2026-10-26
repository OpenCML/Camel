include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(NAME str SOURCES executor.cpp module.cpp operators.cpp case.cpp case_tables.cpp)
