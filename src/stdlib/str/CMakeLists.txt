include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(NAME str SOURCES executor.cpp module.cpp operators.cpp case.cpp case_tables.cpp)
