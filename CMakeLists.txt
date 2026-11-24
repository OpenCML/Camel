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

# `src/` is the umbrella the blocks were moved under on 2026-11-22: `src/kernel`, `src/parse`,
# `src/compile`, `src/bindings`, `src/tools` and the standard library `src/stdlib`. None of them is
# collected here, and that is the point of the exclude below:
#
#   * `cmake/layout-blocks.cmake` globs every block's sources itself (`camel_layout_add_blocks`) and
#     its objects are what libcamel is linked from;
#   * a module directory under `src/stdlib/<module>/` is its own `.cmo` target (`CMakeLists.txt`,
#     `CAMEL_ALL_MODULES`), not a part of libcamel;
#   * `src/tools/` and `src/bindings/` are separate executables and the CPython extension, each of
#     which links libcamel.
#
# A recursive glob over `src/` therefore compiles the same translation units a second time and the
# link fails on duplicate symbols -- which is exactly what happened the day the move landed. So the
# glob keeps only what no block claims: `third_party/`, and a source sitting directly in `src/`
# (the `unplaced` block of `layout-blocks.cmake` applies the same rule, without recursing).
# A block added under `src/` later has to be added to this list as well as to the layout.
file(GLOB_RECURSE _ALL_SRC
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/*.cpp"
)
list(FILTER _ALL_SRC EXCLUDE REGEX
    "^${PROJECT_SOURCE_DIR}/src/(kernel|parse|compile|bindings|tools|stdlib)/")
# A checkout older than the handwritten front end can still carry `src/parse/src/antlr/` on disk: that
# path was ignored for the ANTLR-generated sources until 2026-11-02, when the ignore rules went. This
# filter is not a dependency on it and nothing in this tree needs it -- the globs above take every
# block, and this keeps a stale output directory out of a build that finds one.
list(FILTER _ALL_SRC EXCLUDE REGEX "/src/parse/src/antlr/")
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
# The tensor unary kernels opt back in (src/stdlib/tensor/CMakeLists.txt); the GEMM writes its
# fused multiply-adds explicitly.
add_compile_options(-ffp-contract=off)

# Shared library: Camel core (libcamel).
#
# The sources are compiled into an OBJECT library and the DLL is linked from those objects. That
# split is how the export list stops being a hand-maintained file without a dependency cycle:
# compiling a dependent target's sources needs only the headers, so its objects exist before
# libcamel links, and the undefined symbols in them can be read to decide what libcamel exports
# (the generation is at the end of this file; cmake/generate_libcamel_exports.mjs does the read).
include(${PROJECT_SOURCE_DIR}/cmake/layout-blocks.cmake)
camel_layout_add_blocks()
add_library(libcamel SHARED ${CAMEL_LAYOUT_ALL_OBJECTS})

# Diagnostic codes are assigned in src/kernel/diagnostics/registry.tsv, not written by hand: the
# headers and message tables under core/error/diagnostics/messages are generated from it, and
# this step refuses a duplicate number, a number outside its section's range, a duplicate name
# and a generated file that no longer matches the registry. It runs before libcamel_objects, so
# no translation unit is compiled against a stale code table.
find_program(CAMEL_DIAGNOSTICS_NODE NAMES node nodejs REQUIRED)
set(_camel_diagnostic_sources
    "${PROJECT_SOURCE_DIR}/src/kernel/diagnostics/registry.tsv"
    "${PROJECT_SOURCE_DIR}/scripts/diagnostics-catalog.mjs"
    "${PROJECT_SOURCE_DIR}/src/kernel/include/camel/core/error/diagnostics/base.h")
foreach(_section internal lexical syntax semantic runtime)
    list(APPEND _camel_diagnostic_sources
        "${PROJECT_SOURCE_DIR}/src/kernel/include/camel/core/error/diagnostics/messages/${_section}.h"
        "${PROJECT_SOURCE_DIR}/src/kernel/core/error/diagnostics/messages/${_section}.cpp")
endforeach()
add_custom_command(
    OUTPUT "${PROJECT_BINARY_DIR}/diagnostics-registry.stamp"
    COMMAND "${CAMEL_DIAGNOSTICS_NODE}" "${PROJECT_SOURCE_DIR}/scripts/diagnostics-catalog.mjs" --check
    COMMAND "${CMAKE_COMMAND}" -E touch "${PROJECT_BINARY_DIR}/diagnostics-registry.stamp"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    DEPENDS ${_camel_diagnostic_sources}
    COMMENT "Checking src/kernel/diagnostics/registry.tsv against the generated code tables"
    VERBATIM)
add_custom_target(camel_diagnostics_check DEPENDS "${PROJECT_BINARY_DIR}/diagnostics-registry.stamp")
add_dependencies(camel_layout_blocks camel_diagnostics_check)

# The target name already carries "lib": without this, the Unix prefix produces liblibcamel.so,
# which the scripts (scripts/common.js: libName) do not find, so out/latest/libs stays empty and
# the tools only run through the build-tree runpath.
set_target_properties(libcamel PROPERTIES
    LINKER_LANGUAGE CXX
    PREFIX "")

# The library carries the same two entries the tools carry (src/tools/libcamel_tool.cmake): the
# directory it sits in, and `libs/` below it. A tool finds the collected install through its own
# rpath, but a consumer that is not one of our tools -- the Python binding, anything that loads the
# library directly -- has no entry to inherit and would then resolve libcamel's own dependencies,
# the C++ runtime among them, against the host's libraries. With `$ORIGIN` written here, everything
# the install puts beside libcamel (scripts/collect-out.js places the toolchain's libstdc++ there)
# resolves from inside the install for any loader.
if(UNIX)
    set_target_properties(libcamel PROPERTIES
        BUILD_RPATH "$ORIGIN;$ORIGIN/../libs"
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../libs")
endif()

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

camel_layout_block_standards()

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

# The warning set is written once, in cmake/Warnings.cmake, and reaches a target by the interface
# target that carries that target's list: camel_warnings_libcamel here, camel_warnings_tool for the
# tools (src/tools/libcamel_tool.cmake), camel_warnings_module for the modules (cmake/camel_module.cmake).
# No file writes a warning flag of its own.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Warnings.cmake)

camel_layout_block_options(
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-fno-PIE>
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
    -fcolor-diagnostics
    -fansi-escape-codes # windows only
    ${CAMEL_TARGET_ARCH_FLAGS} # cmake/Options.cmake: CAMEL_TARGET_ARCH
    # -ffast-math # Do not enable this; build-mode optimization can make results inaccurate.
)

# -no-pie is only for non-Windows (GCC/Linux); Clang on Windows will warn about it being unused.
target_link_options(libcamel PRIVATE
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<NOT:$<PLATFORM_ID:Windows>>>:-no-pie>
)

camel_layout_block_definitions()

camel_layout_block_includes()
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
camel_layout_block_links()

# Workers of the parallel scheduler with an 8 MiB stack on every platform.
include(${PROJECT_SOURCE_DIR}/cmake/TaskflowWorkerStack.cmake)
camel_layout_block_taskflow()

# External modules: compile python / pyplot / io and similar sources into .cmo.
# Python embedding (python.cmo, pyplot.cmo, py_bridge*) requires Development headers and import libraries.
# Without a venv/SDK, src/stdlib/python/CMakeLists.txt returns early; calling add_subdirectory(pyplot) anyway will fail on find_package(REQUIRED).
# Set CAMEL_SKIP_PYTHON=1 or -DCAMEL_ENABLE_PYTHON=OFF to skip python + pyplot while building the rest normally (options live in cmake/Options.cmake and options.json).
set(_camel_skip_python_modules FALSE)
if(NOT CAMEL_ENABLE_PYTHON OR CAMEL_PACKAGE_MINIMAL)
    set(_camel_skip_python_modules TRUE)
    set(_camel_skip_python_why "")
    if(CAMEL_PACKAGE_MINIMAL)
        set(_camel_skip_python_why " CAMEL_PACKAGE_MINIMAL is on.")
    endif()
    message(STATUS "Skipping src/stdlib/python and src/stdlib/pyplot.${_camel_skip_python_why}")
endif()
if(NOT _camel_skip_python_modules)
    add_subdirectory(src/stdlib/python)
    if(CAMEL_PYTHON_DEV_AVAILABLE)
        add_subdirectory(src/stdlib/pyplot)
    else()
        message(STATUS "Skipping modules/pyplot: no Python development environment (same condition as python.cmo).")
    endif()
endif()

# ---------------------------------------------------------------------------------------------
# The module set: one list, one place.
#
# Every module is a separate `<name>.cmo` that a Camel program imports by name, so the set decides
# what an install can run and nothing else does -- except the one relation CMake cannot discover,
# a module that *links* another one's target. A module names that relation beside its own sources
# (LINK_MODULES in modules/<name>/CMakeLists.txt) and the check below reads the pairs back, so no
# list of links is kept here; a subset that drops a dependency fails at configure, by name, instead
# of at the link of a target several minutes later.
#
# CAMEL_MODULES (cmake/Options.cmake) selects a subset; empty means the full list below, or -- with
# CAMEL_PACKAGE_MINIMAL -- the full list minus the modules CAMEL_MINIMAL_DROPS names.
set(CAMEL_ALL_MODULES
    io process math time random this fs hash re str env json para test_support
    sim tensor tiles nn transforms autodiff laws ckpt bytes graph state verify rewrite onnx
)
# The only place the minimal package's module set is written: what a minimal package leaves out is
# the GPU back end, the deployment exporter, and the checkpoint library built on tensor, plus the
# two Python directions (in/bindings above). Of the four modules this switch chooses between --
# tiles, onnx, nn, ckpt -- three go and `nn` stays, because the case study and the MLP/transformer
# training steps import it for `sgd` and `save_params`.
set(CAMEL_MINIMAL_DROPS tiles onnx ckpt)

if(CAMEL_PACKAGE_MINIMAL)
    set(_camel_modules ${CAMEL_ALL_MODULES})
    foreach(_dropped IN LISTS CAMEL_MINIMAL_DROPS)
        list(REMOVE_ITEM _camel_modules ${_dropped})
    endforeach()
    message(STATUS "CAMEL_PACKAGE_MINIMAL: building ${_camel_modules}")
else()
    set(_camel_modules ${CAMEL_ALL_MODULES})
endif()
if(NOT "${CAMEL_MODULES}" STREQUAL "")
    set(_camel_modules "")
    foreach(_requested IN LISTS CAMEL_MODULES)
        if(NOT _requested IN_LIST CAMEL_ALL_MODULES)
            message(FATAL_ERROR
                "CAMEL_MODULES names \"${_requested}\", which is not a module. The modules are: ${CAMEL_ALL_MODULES}")
        endif()
        list(APPEND _camel_modules ${_requested})
    endforeach()
    message(STATUS "CAMEL_MODULES: building ${_camel_modules}")
endif()

foreach(_module IN LISTS _camel_modules)
    add_subdirectory(src/stdlib/${_module})
endforeach()

# The link relations the modules declared, checked against the selected set. Each entry is
# "<module>|<module it links>"; a dependent whose dependency is missing is a configure error naming
# both, which is earlier than the link that would otherwise fail.
get_property(_camel_module_links GLOBAL PROPERTY CAMEL_MODULE_LINKS)
foreach(_entry IN LISTS _camel_module_links)
    string(REPLACE "|" ";" _linked_pair "${_entry}")
    list(GET _linked_pair 0 _dependent)
    list(GET _linked_pair 1 _needed)
    if(NOT _needed IN_LIST _camel_modules)
        message(FATAL_ERROR
            "src/stdlib/${_dependent} links src/stdlib/${_needed} (LINK_MODULES in src/stdlib/${_dependent}/CMakeLists.txt), "
            "and CAMEL_MODULES did not select it. Add ${_needed}, or drop ${_dependent} as well.")
    endif()
endforeach()

# The Python binding: the CPython extension `camel._camel`, a host of libcamel that a
# Python project calls a Camel module through. Independent of src/stdlib/python (which embeds Python
# in Camel, the other direction), so it is added whether or not CAMEL_ENABLE_PYTHON is on; the
# directory itself returns early when Python's development files are absent.
if(CAMEL_ENABLE_PYTHON_BINDINGS AND NOT CAMEL_PACKAGE_MINIMAL)
    add_subdirectory(src/bindings/python)
else()
    message(STATUS "Skipping src/bindings/python (CAMEL_ENABLE_PYTHON_BINDINGS=OFF or CAMEL_PACKAGE_MINIMAL=ON).")
endif()

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
add_subdirectory(src/tools)
add_subdirectory(test/tools/parsecheck)
add_subdirectory(test/tools/oprecord)
add_subdirectory(test/tools/facts)
camel_layout_append_catchall()

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
# link and with the symbol named; the helpers in cmake/camel_module.cmake and
# src/tools/libcamel_tool.cmake register every target they create.
if(WIN32)
    find_program(CAMEL_EXPORTS_NM NAMES llvm-nm REQUIRED)
    find_program(CAMEL_EXPORTS_NODE NAMES node nodejs REQUIRED)

    get_property(_camel_export_scan_targets GLOBAL PROPERTY CAMEL_EXPORT_SCAN_TARGETS)
    set(_core_objects ${CAMEL_LAYOUT_ALL_OBJECTS})
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
    add_dependencies(libcamel_exports_scan camel_layout_blocks ${_camel_export_scan_targets})
    # The generation compiles nothing, but it must finish before the library links: the .def is
    # an input of the link, and its content is what the import library exports.
    add_dependencies(libcamel libcamel_exports_scan)
    set_property(
        TARGET libcamel APPEND PROPERTY LINK_DEPENDS
        "${LIBCAMEL_EXPORTS_BUILT}")
endif()
