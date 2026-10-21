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
# Older checkouts may still hold ignored ANTLR outputs. The handwritten frontend does not use them.
list(FILTER _ALL_SRC EXCLUDE REGEX "/src/parse/antlr/")
set(LIBCAMEL_SRC ${_ALL_SRC})

find_package(fmt REQUIRED)
find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Taskflow REQUIRED)

add_definitions(-DNOMINMAX)

# Every floating-point expression is rounded as written: no contraction of a * b + c into one
# fused multiply-add. Clang contracts by default wherever the target has an FMA instruction,
# which is always on arm64 and only with -mfma (or -march levels that imply it) on x86-64, so
# the same source printed different numbers on macOS (docs/design-principles.cn.md 11.5: the
# output is the same bytes on every platform). Applies to libcamel, the modules and the tools.
# The tensor unary kernels opt back in (modules/tensor/CMakeLists.txt); the GEMM writes its
# fused multiply-adds explicitly.
add_compile_options(-ffp-contract=off)

# Shared library: Camel core (libcamel)
add_library(libcamel SHARED ${LIBCAMEL_SRC})

# The target name already carries "lib": without this, the Unix prefix produces liblibcamel.so,
# which the scripts (scripts/common.js: libName) do not find, so out/latest/libs stays empty and
# the tools only run through the build-tree runpath.
set_target_properties(libcamel PROPERTIES PREFIX "")

# Windows: explicit exports for camel-cli, the tools and the .cmo modules (H30, H16 item 5).
#
# WINDOWS_EXPORT_ALL_SYMBOLS is off: a Debug build emits every inline and template function as a
# real symbol, so "export everything" exceeded the PE limit of 65,535 exported names
# (lld-link: "too many exported symbols (got 75682, max 65535)") and only the Release build ever
# linked. cmake/libcamel_exports.def names the symbols the dependent targets import, which is a
# few hundred. A new cross-boundary symbol fails at link time with its mangled name; that name is
# added to the list. The list records what the dependent targets need, so it is generated from
# their import tables, not a second declaration of the API.
#
# The Debug export surface is larger than the Release one: profiling entry points
# (profiler::start_advanced_tracing and friends) are only defined in a Debug library, and the
# debug tools import them. lld-link accepts one /DEF, so the two lists are concatenated before
# the link into a per-configuration file.
if(WIN32)
    set(LIBCAMEL_EXPORTS_DEF "${PROJECT_SOURCE_DIR}/cmake/libcamel_exports.def")
    set(LIBCAMEL_EXPORTS_DEBUG_DEF "${PROJECT_SOURCE_DIR}/cmake/libcamel_exports_debug.def")
    set(LIBCAMEL_EXPORTS_BUILT "${PROJECT_BINARY_DIR}/libcamel_exports_$<CONFIG>.def")
    set_target_properties(libcamel PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS FALSE)
    add_custom_command(
        TARGET libcamel PRE_LINK
        COMMAND "${CMAKE_COMMAND}"
            "-DCFG=$<CONFIG>"
            "-DCOMMON=${LIBCAMEL_EXPORTS_DEF}"
            "-DEXTRA=${LIBCAMEL_EXPORTS_DEBUG_DEF}"
            "-DOUT=${LIBCAMEL_EXPORTS_BUILT}"
            -P "${PROJECT_SOURCE_DIR}/cmake/make_libcamel_def.cmake"
        VERBATIM)
    target_link_options(libcamel PRIVATE "LINKER:/DEF:${LIBCAMEL_EXPORTS_BUILT}")
    # The export lists are link inputs the command line does not make build dependencies: an edit
    # must relink the library, or the stale import library keeps failing the dependent targets.
    set_property(
        TARGET libcamel APPEND PROPERTY LINK_DEPENDS
        "${LIBCAMEL_EXPORTS_DEF}" "${LIBCAMEL_EXPORTS_DEBUG_DEF}")
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
    -Wno-nan-infinity-disabled
    -fcolor-diagnostics
    -fansi-escape-codes # windows only
    ${CAMEL_TARGET_ARCH_FLAGS} # cmake/Options.cmake: CAMEL_TARGET_ARCH
    # -ffast-math # Do not enable this; build-mode optimization can make results inaccurate.
)

# -no-pie is only for non-Windows (GCC/Linux); Clang on Windows will warn about it being unused.
target_link_options(libcamel PRIVATE
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>
)

target_compile_definitions(libcamel PRIVATE CAMEL_DLL_EXPORTS)

target_include_directories(libcamel PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
target_include_directories(libcamel PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(libcamel PRIVATE
    camel_options
    fmt::fmt
    gtest::gtest
    nlohmann_json::nlohmann_json
    Taskflow::Taskflow
)

# Workers of the parallel scheduler with an 8 MiB stack on every platform.
include(${PROJECT_SOURCE_DIR}/cmake/TaskflowWorkerStack.cmake)
camel_taskflow_with_worker_stack(libcamel)

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
add_subdirectory(modules/str)
add_subdirectory(modules/sys)
add_subdirectory(modules/json)
add_subdirectory(modules/para)
add_subdirectory(modules/sim)
add_subdirectory(modules/tensor)
add_subdirectory(modules/tiles)
add_subdirectory(modules/nn)
add_subdirectory(modules/autodiff)
add_subdirectory(modules/tree)
add_subdirectory(modules/bytes)
add_subdirectory(modules/onnx)

# CLI and tools: link the libcamel shell (camel-cli lives under tools).
add_subdirectory(tools)
