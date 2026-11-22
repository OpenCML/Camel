include(${PROJECT_SOURCE_DIR}/cmake/camel_module.cmake)
add_camel_module(NAME fs SOURCES executor.cpp module.cpp operators.cpp binary.cpp)
