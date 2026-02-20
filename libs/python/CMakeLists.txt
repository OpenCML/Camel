# python 模块：作为 .cmo 外源动态库示例，单独编译为 python.cmo
# 使用方式：将 python.cmo 放入模块搜索路径，在 Camel 中 import python 即可加载

cmake_minimum_required(VERSION 3.25)

add_library(python_cmo SHARED
    module.cpp
    operators.cpp
    type.cpp
)

target_compile_features(python_cmo PRIVATE cxx_std_23)
target_include_directories(python_cmo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)

# 输出为 python.cmo，便于运行时按“模块名.cmo”查找
set_target_properties(python_cmo PROPERTIES
    OUTPUT_NAME python
    SUFFIX ".cmo"
    PREFIX ""
)

# 与 libcamel 头文件依赖一致，避免包含 context 等时缺 fmt、antlr4 等
target_link_libraries(python_cmo PRIVATE
    libcamel
    fmt::fmt
    pybind11::embed
    Python::Python
)

# 与 libcamel 一致的编译选项（按需简化）
target_compile_options(python_cmo PRIVATE
    $<$<CONFIG:Release>:-O3>
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-private-field
)

if(WIN32)
    target_compile_definitions(python_cmo PRIVATE NOMINMAX)
endif()
