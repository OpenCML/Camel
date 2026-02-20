cmake_minimum_required(VERSION 3.25)
project(camel)

if(NOT DEFINED BUILD_FOOTPRINT)
    string(TIMESTAMP BUILD_FOOTPRINT "%y%m%d_%H%M%S")
endif()

# CLI 的 config 在 camel-cli 中编译，此处仅用于 BUILD_FOOTPRINT 占位（由 camel-cli 传入）
if(NOT DEFINED BUILD_FOOTPRINT_FOR_CLI)
    set(BUILD_FOOTPRINT_FOR_CLI "${BUILD_FOOTPRINT}")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 跨平台RPATH设置
if(APPLE)
    set(CMAKE_MACOSX_RPATH OFF)
    set(CMAKE_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
endif()

if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
elseif(UNIX)
    add_definitions(-DPLATFORM_LINUX)
endif()

file(GLOB_RECURSE _ALL_SRC
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/*.cpp"
)
list(REMOVE_ITEM _ALL_SRC
    "${PROJECT_SOURCE_DIR}/src/main.cpp"
    "${PROJECT_SOURCE_DIR}/src/config.cpp"
)
set(LIBCAMEL_SRC ${_ALL_SRC})

find_package(fmt REQUIRED)
find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Taskflow REQUIRED)
find_package(Eigen3 REQUIRED)

# 优先使用当前目录的 venv（最可靠）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/venv/Scripts/python.exe")
    set(Python_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/venv")
    message(STATUS "Found venv in current directory: ${CMAKE_CURRENT_SOURCE_DIR}/venv")
elseif(DEFINED ENV{VIRTUAL_ENV})
    set(Python_ROOT_DIR "$ENV{VIRTUAL_ENV}")
    message(STATUS "Using Python from VIRTUAL_ENV: $ENV{VIRTUAL_ENV}")
endif()

# 查找 Python（包含 Interpreter 和 Development 组件）
find_package(Python COMPONENTS Interpreter Development REQUIRED)

# 验证找到的 Python 是否在虚拟环境中
if(Python_EXECUTABLE)
    message(STATUS "Python executable: ${Python_EXECUTABLE}")
    if(NOT "${Python_EXECUTABLE}" MATCHES "venv")
        message(WARNING "Python executable is not from virtual environment! This may cause module import issues.")
    endif()
endif()

find_package(pybind11 REQUIRED)

add_definitions(-DNOMINMAX)

# 共享库：camel 核心（libcamel）
add_library(libcamel SHARED ${LIBCAMEL_SRC})

# Windows: 导出所有符号供 camel-cli 和 tools 链接
if(WIN32)
    set_target_properties(libcamel PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif()

target_compile_features(libcamel PRIVATE cxx_std_23)
set_target_properties(libcamel PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (NOT ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang"))
    message(FATAL_ERROR "This project requires Clang as the C++ compiler. Current compiler is ${CMAKE_CXX_COMPILER_ID}.")
endif()

# 开启 LTO 支持
include(CheckIPOSupported)
check_ipo_supported(RESULT result OUTPUT output)

if(result)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
endif()

# 选项通过 interface 库统一应用，便于 libcamel 与 camel-cli 共享
add_library(camel_options INTERFACE)
set(CAMEL_OPTIONS_TARGET camel_options)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Options.cmake)

target_compile_options(libcamel PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -Wno-c99-extensions # 使用灵活数组
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
    -Wno-unused-private-field
    -Wno-gnu-label-as-value # for Computed Goto
    -Wno-language-extension-token # for antlr4-runtime
    -Wno-dollar-in-identifier-extension # for antlr4-runtime
    -Wno-nan-infinity-disabled
    -fcolor-diagnostics
    -fansi-escape-codes # windows only
    -march=native
    -mfma
    # -ffast-math # 不要开启这个，在build模式优化下会导致计算结果失真
)

target_link_options(libcamel PRIVATE
    $<$<CONFIG:RelWithDebInfo>:-no-pie>
)

target_compile_definitions(libcamel PRIVATE ANTLR4CPP_STATIC CAMEL_DLL_EXPORTS)

target_include_directories(libcamel PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(libcamel PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
    ${PROJECT_SOURCE_DIR}/third_party/antlr4-runtime
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(libcamel PRIVATE
    camel_options
    fmt::fmt
    gtest::gtest
    nlohmann_json::nlohmann_json
    Taskflow::Taskflow
    Eigen3::Eigen
    pybind11::embed
    Python::Python
)

# CLI 与工具：链接 libcamel 的 shell（camel-cli 在 tools 下）
add_subdirectory(tools)
