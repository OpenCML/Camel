cmake_minimum_required(VERSION 3.25)
project(camel)

if(NOT DEFINED BUILD_FOOTPRINT)
    string(TIMESTAMP BUILD_FOOTPRINT "%y%m%d_%H%M%S")
endif()

# CLI config is compiled in camel-cli; this file only reserves BUILD_FOOTPRINT (passed in by camel-cli).
if(NOT DEFINED BUILD_FOOTPRINT_FOR_CLI)
    set(BUILD_FOOTPRINT_FOR_CLI "${BUILD_FOOTPRINT}")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Cross-platform RPATH settings
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
set(LIBCAMEL_SRC ${_ALL_SRC})

find_package(fmt REQUIRED)
find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Taskflow REQUIRED)

add_definitions(-DNOMINMAX)

# Shared library: Camel core (libcamel)
add_library(libcamel SHARED ${LIBCAMEL_SRC})

# Windows: export all symbols for camel-cli and tools linkage.
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

# Enable LTO support
include(CheckIPOSupported)
check_ipo_supported(RESULT result OUTPUT output)

if(result)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO FALSE)
endif()

# Apply options through a shared interface library so libcamel and camel-cli use the same definitions.
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
    -Wno-c99-extensions # Uses flexible array members.
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
    # -ffast-math # Do not enable this; build-mode optimization can make results inaccurate.
)

# -no-pie is only for non-Windows (GCC/Linux); Clang on Windows will warn about it being unused.
target_link_options(libcamel PRIVATE
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>
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
)

# External modules: compile python / pyplot / io and similar sources into .cmo.
# Python embedding (python.cmo, pyplot.cmo, py_bridge*) requires Development headers and import libraries.
# Without a venv/SDK, modules/python/CMakeLists.txt returns early; calling add_subdirectory(pyplot) anyway will fail on find_package(REQUIRED).
# Set CAMEL_SKIP_PYTHON=1 or -DCAMEL_ENABLE_PYTHON=OFF to skip python + pyplot while building the rest normally (options live in cmake/Options.cmake and options.json).
set(_camel_skip_python_modules FALSE)
if(NOT CAMEL_ENABLE_PYTHON)
    set(_camel_skip_python_modules TRUE)
    message(STATUS "Skipping modules/python and modules/pyplot.")
endif()
if(NOT _camel_skip_python_modules)
    add_subdirectory(modules/python)
    if(CAMEL_PYTHON_DEV_AVAILABLE)
        add_subdirectory(modules/pyplot)
    else()
        message(STATUS "Skipping modules/pyplot: no Python development environment (same condition as python.cmo).")
    endif()
endif()
add_subdirectory(modules/io)
add_subdirectory(modules/os)
add_subdirectory(modules/math)
add_subdirectory(modules/time)
add_subdirectory(modules/random)
add_subdirectory(modules/algo)
add_subdirectory(modules/statistics)
add_subdirectory(modules/this)
add_subdirectory(modules/fs)
add_subdirectory(modules/crypto)
add_subdirectory(modules/re)
add_subdirectory(modules/sys)
add_subdirectory(modules/json)
add_subdirectory(modules/para)
add_subdirectory(modules/sim)
add_subdirectory(modules/tensor)

# CLI and tools: link the libcamel shell (camel-cli lives under tools).
add_subdirectory(tools)
