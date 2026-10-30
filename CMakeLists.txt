cmake_minimum_required(VERSION 3.25)
project(camel)

# Object libraries whose undefined symbols decide what libcamel exports. Every target that
# is compiled separately and links libcamel registers its OBJECT library here; the generation at
# the end of this file reads them all. The function is defined in the top-level directory, so the
# subdirectories added below can call it.
function(camel_register_export_scan_target target)
    set_property(GLOBAL APPEND PROPERTY CAMEL_EXPORT_SCAN_TARGETS "${target}")
endfunction()

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

# glibc before 2.34 keeps libpthread, libdl and librt apart from libc; a newer one carries them
# inside it. Where they are apart, the link of a target that reaches pthread_join through
# libstdc++ fails with "/lib64/libpthread.so.0: error adding symbols: DSO missing from command
# line" and libcamel's own dlopen fails the same way through libdl -- exactly the two failures the
# manylinux_2_28 wheel build meets (AlmaLinux 8, glibc 2.28). Both libraries therefore go on every
# link line: Threads::Threads is the pthread library of the platform (nothing on Windows, and the
# stub for it where glibc has merged it) and CMAKE_DL_LIBS is dl or the empty string (macOS).
# Link-only: no compile flag, definition or source changes, so a platform that linked before
# links the same libraries it did.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    link_libraries(Threads::Threads ${CMAKE_DL_LIBS})
endif()

add_definitions(-DNOMINMAX)

# Every floating-point expression is rounded as written: no contraction of a * b + c into one
# fused multiply-add. Clang contracts by default wherever the target has an FMA instruction,
# which is always on arm64 and only with -mfma (or -march levels that imply it) on x86-64, so
# the same source printed different numbers on macOS (docs/design-principles.cn.md 11.5: the
# output is the same bytes on every platform). Applies to libcamel, the modules and the tools.
# The tensor unary kernels opt back in (modules/tensor/CMakeLists.txt); the GEMM writes its
# fused multiply-adds explicitly.
add_compile_options(-ffp-contract=off)

# Shared library: Camel core (libcamel).
#
# The sources are compiled into an OBJECT library and the DLL is linked from those objects. That
# split is how the export list stops being a hand-maintained file without a dependency cycle:
# compiling a dependent target's sources needs only the headers, so its objects exist before
# libcamel links, and the undefined symbols in them can be read to decide what libcamel exports
# (the generation is at the end of this file; cmake/generate_libcamel_exports.mjs does the read).
add_library(libcamel_objects OBJECT ${LIBCAMEL_SRC})
add_library(libcamel SHARED $<TARGET_OBJECTS:libcamel_objects>)

# Diagnostic codes are assigned in diagnostics/registry.tsv, not written by hand: the
# headers and message tables under core/error/diagnostics/messages are generated from it, and
# this step refuses a duplicate number, a number outside its section's range, a duplicate name
# and a generated file that no longer matches the registry. It runs before libcamel_objects, so
# no translation unit is compiled against a stale code table.
find_program(CAMEL_DIAGNOSTICS_NODE NAMES node nodejs REQUIRED)
set(_camel_diagnostic_sources
    "${PROJECT_SOURCE_DIR}/diagnostics/registry.tsv"
    "${PROJECT_SOURCE_DIR}/scripts/diagnostics-catalog.mjs"
    "${PROJECT_SOURCE_DIR}/include/camel/core/error/diagnostics/base.h")
foreach(_section internal lexical syntax semantic runtime)
    list(APPEND _camel_diagnostic_sources
        "${PROJECT_SOURCE_DIR}/include/camel/core/error/diagnostics/messages/${_section}.h"
        "${PROJECT_SOURCE_DIR}/src/core/error/diagnostics/messages/${_section}.cpp")
endforeach()
add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/diagnostics-registry.stamp"
    COMMAND "${CAMEL_DIAGNOSTICS_NODE}" "${PROJECT_SOURCE_DIR}/scripts/diagnostics-catalog.mjs" --check
    COMMAND "${CMAKE_COMMAND}" -E touch "${PROJECT_BINARY_DIR}/diagnostics-registry.stamp"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    DEPENDS ${_camel_diagnostic_sources}
    COMMENT "Checking diagnostics/registry.tsv against the generated code tables"
    VERBATIM)
add_custom_target(camel_diagnostics_check DEPENDS "${PROJECT_BINARY_DIR}/diagnostics-registry.stamp")
add_dependencies(libcamel_objects camel_diagnostics_check)

# The target name already carries "lib": without this, the Unix prefix produces liblibcamel.so,
# which the scripts (scripts/common.js: libName) do not find, so out/latest/libs stays empty and
# the tools only run through the build-tree runpath.
set_target_properties(libcamel PROPERTIES
    LINKER_LANGUAGE CXX
    PREFIX "")

# Windows: explicit exports for camel-cli, the tools and the .cmo modules.
#
# WINDOWS_EXPORT_ALL_SYMBOLS is off: a Debug build emits every inline and template function as a
# real symbol, so "export everything" exceeded the PE limit of 65,535 exported names
# (lld-link: "too many exported symbols (got 75682, max 65535)") and only the Release build ever
# linked. The list is generated (cmake/generate_libcamel_exports.mjs): for each configuration it is the intersection of the
# symbols the dependent targets' object files leave undefined and the symbols libcamel's own
# object files define. A new module, or a module that starts calling a new libcamel symbol, is
# therefore in the list the next time libcamel links; no list edit is needed.
#
# What cannot be derived is kept in cmake/libcamel_exports_manual.def, where each entry says why.
# The Debug-only exports (the profiler entry points) are no longer a separate list: a Debug
# library defines them and the Debug tools import them, so the same intersection finds them.
if(WIN32)
    set(LIBCAMEL_EXPORTS_MANUAL "${PROJECT_SOURCE_DIR}/cmake/libcamel_exports_manual.def")
    set(LIBCAMEL_EXPORTS_BUILT "${PROJECT_BINARY_DIR}/libcamel_exports_$<CONFIG>.def")
    set_target_properties(libcamel PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS FALSE)
    target_link_options(libcamel PRIVATE "LINKER:/DEF:${LIBCAMEL_EXPORTS_BUILT}")
    # The manual list is a link input the command line does not make a build dependency: an edit
    # must relink the library, or the stale import library keeps failing the dependent targets.
    set_property(
        TARGET libcamel APPEND PROPERTY LINK_DEPENDS
        "${LIBCAMEL_EXPORTS_MANUAL}")
endif()

target_compile_features(libcamel_objects PRIVATE cxx_std_23)
set_target_properties(libcamel_objects PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
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

target_compile_options(libcamel_objects PRIVATE
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

target_compile_definitions(libcamel_objects PRIVATE CAMEL_DLL_EXPORTS)

target_include_directories(libcamel_objects PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/third_party
    ${PROJECT_SOURCE_DIR}
)
target_include_directories(libcamel PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(libcamel PRIVATE
    camel_options
    fmt::fmt
    gtest::gtest
    nlohmann_json::nlohmann_json
    Taskflow::Taskflow
)

# The object library compiles the same sources, so it needs the same usage requirements. Linking
# it to the interface and header-only packages gives its translation units the definitions
# (ENABLE_FASTVM_JIT and friends) and include paths the library had when it was one target.
target_link_libraries(libcamel_objects PRIVATE
    camel_options
    fmt::fmt
    gtest::gtest
    nlohmann_json::nlohmann_json
    Taskflow::Taskflow
)

# Workers of the parallel scheduler with an 8 MiB stack on every platform.
include(${PROJECT_SOURCE_DIR}/cmake/TaskflowWorkerStack.cmake)
camel_taskflow_with_worker_stack(libcamel_objects)

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
add_subdirectory(modules/process)
add_subdirectory(modules/math)
add_subdirectory(modules/time)
add_subdirectory(modules/random)
add_subdirectory(modules/this)
add_subdirectory(modules/fs)
add_subdirectory(modules/hash)
add_subdirectory(modules/re)
add_subdirectory(modules/str)
add_subdirectory(modules/env)
add_subdirectory(modules/json)
add_subdirectory(modules/para)
# The facilities of a test, not of a program: `rendezvous` and `thread_count`.
add_subdirectory(modules/test_support)
add_subdirectory(modules/sim)
add_subdirectory(modules/tensor)
add_subdirectory(modules/tiles)
add_subdirectory(modules/nn)
add_subdirectory(modules/transforms)
add_subdirectory(modules/autodiff)
add_subdirectory(modules/ckpt)
add_subdirectory(modules/bytes)
add_subdirectory(modules/graph)
# The translation layer of the rule verifier: a source list for modules/rewrite, not a module
# (modules/verify/CMakeLists.txt).
add_subdirectory(modules/verify)
add_subdirectory(modules/rewrite)
add_subdirectory(modules/onnx)

# The Python binding: the CPython extension `camel._camel`, a host of libcamel that a
# Python project calls a Camel module through. Independent of modules/python (which embeds Python
# in Camel, the other direction), so it is added whether or not CAMEL_ENABLE_PYTHON is on; the
# directory itself returns early when Python's development files are absent.
add_subdirectory(bindings/python)

# ---------------------------------------------------------------------------------------------
# The module manifest: one line per .cmo this configure builds, per configuration.
#
# The build tree keeps the output of a module after the module is renamed or removed, because
# CMake does not delete the artifact of a target that no longer exists. scripts/common.js used to
# copy every `.cmo` it found under build/{libs,modules}/<dir>/<config>/ into stdlib/, so a retired
# module's leftover was reinstalled on every build and `import <retired name>` kept working; the
# install then copied stdlib/ into out/latest/stdlib, which is on the module search path. The
# install step now copies exactly what this manifest lists (and removes any other .cmo it finds
# in stdlib/), so a .cmo cannot outlive the module that produced it.
#
# file(GENERATE) is evaluated at generate time: $<TARGET_FILE:...> is the real path of the file
# this configuration links, and $<CONFIG> in OUTPUT writes one manifest per configuration.
get_property(_camel_cmo_targets GLOBAL PROPERTY CAMEL_CMO_TARGETS)
set(_camel_cmo_manifest "")
foreach(_camel_cmo_target IN LISTS _camel_cmo_targets)
    string(APPEND _camel_cmo_manifest "$<TARGET_FILE:${_camel_cmo_target}>\n")
endforeach()
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/camel-module-outputs-$<CONFIG>.txt"
    CONTENT "${_camel_cmo_manifest}")

# CLI and tools: link the libcamel shell (camel-cli lives under tools).
add_subdirectory(tools)

# ---------------------------------------------------------------------------------------------
# libcamel's export list, generated per configuration.
#
# The cycle this breaks: the modules and the tools link libcamel, but libcamel needs the list of
# symbols they import. Their *objects* are what carry the imports, and compiling an object needs
# only headers, so every dependent target's sources are staged as an OBJECT library and compiled
# before libcamel links. This step intersects the undefined symbols of those objects with the
# defined symbols of libcamel_objects: the result is what the PE export table contains, and it
# stays within the 65,535-symbol limit because only imported symbols are listed.
#
# A target that links libcamel without registering its objects would still fail, but at its own
# link and with the symbol named; the helpers in modules/camel_module.cmake and
# tools/libcamel_tool.cmake register every target they create.
if(WIN32)
    find_program(CAMEL_EXPORTS_NM NAMES llvm-nm REQUIRED)
    find_program(CAMEL_EXPORTS_NODE NAMES node nodejs REQUIRED)

    get_property(_camel_export_scan_targets GLOBAL PROPERTY CAMEL_EXPORT_SCAN_TARGETS)
    set(_core_objects "$<TARGET_OBJECTS:libcamel_objects>")
    set(_import_objects "")
    foreach(_target IN LISTS _camel_export_scan_targets)
        list(APPEND _import_objects "$<TARGET_OBJECTS:${_target}>")
    endforeach()

    # One command line cannot carry every object path, so the per-configuration lists are written
    # at generate time and read by the generator script.
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/exports")
    set(_core_manifest "${PROJECT_BINARY_DIR}/exports/core_objects_$<CONFIG>.txt")
    set(_import_manifest "${PROJECT_BINARY_DIR}/exports/import_objects_$<CONFIG>.txt")
    file(GENERATE OUTPUT "${_core_manifest}" CONTENT "${_core_objects}\n")
    set(_import_manifest_content "")
    foreach(_target IN LISTS _camel_export_scan_targets)
        string(APPEND _import_manifest_content "$<TARGET_OBJECTS:${_target}>\n")
    endforeach()
    file(GENERATE OUTPUT "${_import_manifest}" CONTENT "${_import_manifest_content}")

    add_custom_command(
        OUTPUT "${LIBCAMEL_EXPORTS_BUILT}"
        COMMAND "${CAMEL_EXPORTS_NODE}"
            "${PROJECT_SOURCE_DIR}/cmake/generate_libcamel_exports.mjs"
            --nm "${CAMEL_EXPORTS_NM}"
            --config "$<CONFIG>"
            --core "${_core_manifest}"
            --imports "${_import_manifest}"
            --manual "${LIBCAMEL_EXPORTS_MANUAL}"
            --out "${LIBCAMEL_EXPORTS_BUILT}"
        DEPENDS
            ${_core_objects}
            ${_import_objects}
            "${LIBCAMEL_EXPORTS_MANUAL}"
            "${PROJECT_SOURCE_DIR}/cmake/generate_libcamel_exports.mjs"
            "${_core_manifest}"
            "${_import_manifest}"
        COMMENT "Generating libcamel exports for $<CONFIG> from the dependent targets' objects"
        VERBATIM)
    add_custom_target(libcamel_exports_scan DEPENDS "${LIBCAMEL_EXPORTS_BUILT}")
    add_dependencies(libcamel_exports_scan libcamel_objects ${_camel_export_scan_targets})
    # The generation compiles nothing, but it must finish before the library links: the .def is
    # an input of the link, and its content is what the import library exports.
    add_dependencies(libcamel libcamel_exports_scan)
    set_property(
        TARGET libcamel APPEND PROPERTY LINK_DEPENDS
        "${LIBCAMEL_EXPORTS_BUILT}")
endif()
