# pyplot 模块：基于 matplotlib 的绘图，编译为 pyplot.cmo
# 与 python 模块共享 Python/pybind11 依赖，使用方式：import pyplot
# Python DLL 由 collect-out 复制到 out/tag/libs，与 libcamel 同目录便于共享

cmake_minimum_required(VERSION 3.25)

# 与 python 模块共享 Python 依赖，由 modules/python 先 find_package
if(NOT pybind11_FOUND)
    if(EXISTS "${CMAKE_SOURCE_DIR}/venv/Scripts/python.exe")
        set(Python_ROOT_DIR "${CMAKE_SOURCE_DIR}/venv")
    elseif(DEFINED ENV{VIRTUAL_ENV})
        set(Python_ROOT_DIR "$ENV{VIRTUAL_ENV}")
    endif()
    find_package(Python COMPONENTS Interpreter Development REQUIRED)
    find_package(pybind11 REQUIRED)
endif()

add_library(pyplot_cmo SHARED
    executor.cpp
    module.cpp
    operators.cpp
)

target_compile_features(pyplot_cmo PRIVATE cxx_std_23)
target_include_directories(pyplot_cmo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/third_party/antlr4-runtime
    ${CMAKE_SOURCE_DIR}
)

set_target_properties(pyplot_cmo PROPERTIES
    OUTPUT_NAME pyplot
    SUFFIX ".cmo"
    PREFIX ""
)

if(WIN32)
    set_target_properties(pyplot_cmo PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

target_link_libraries(pyplot_cmo PRIVATE
    libcamel
    fmt::fmt
    pybind11::embed
    Python::Python
)

target_compile_options(pyplot_cmo PRIVATE
    $<$<CONFIG:Release>:-O3>
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-private-field
)

if(WIN32)
    target_compile_definitions(pyplot_cmo PRIVATE NOMINMAX)
endif()
