# python 模块：.cmo 外源动态库示例，import python 加载
include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(NAME python SOURCES executor.cpp module.cpp operators.cpp type.cpp USE_PYTHON)
