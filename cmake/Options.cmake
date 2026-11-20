# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

# Camel project build options
# Option definitions live in cmake/options.json and can be configured interactively with `npm run config`.
# User overrides are stored in .camel-opts.json and are read automatically by build/debug/profile.

include_guard(GLOBAL)

# Use INTERFACE when CAMEL_OPTIONS_TARGET is set (an interface library); otherwise use PRIVATE.
set(_opt_visibility PRIVATE)
set(_opt_target ${PROJECT_NAME})
if(DEFINED CAMEL_OPTIONS_TARGET)
    set(_opt_visibility INTERFACE)
    set(_opt_target ${CAMEL_OPTIONS_TARGET})
endif()

# Simple boolean option: option(NAME) + target_compile_definitions(NAME=0|1).
macro(add_bool_option name description default)
    option(${name} ${description} ${default})
    if(${name})
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=1)
    else()
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=0)
    endif()
    message(STATUS "${name}=${${name}}")
endmacro()

# Platform-checked option: only enabled when the platform matches and option=ON.
# extra_on/off: additional compile definitions (for example JIT_TARGET_X64=1).
macro(add_platform_option name description default platform_regex extra_on extra_off)
    option(${name} ${description} ${default})
    set(_enabled FALSE)
    if(${name} AND CMAKE_SYSTEM_PROCESSOR MATCHES ${platform_regex})
        set(_enabled TRUE)
    endif()
    if(_enabled)
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=1 ${extra_on})
        message(STATUS "${name}=ON")
    else()
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=0 ${extra_off})
        if(${name} AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES ${platform_regex})
            message(STATUS "${name}=OFF (platform ${CMAKE_SYSTEM_PROCESSOR})")
        else()
            message(STATUS "${name}=OFF")
        endif()
    endif()
endmacro()

# ========== FastVM options ==========

message(STATUS "============ Build options: ============")

add_platform_option(
    ENABLE_FASTVM_JIT
    "Enable JIT compilation (x64 only)"
    ON
    "x86_64|AMD64|amd64|X86_64"
    "JIT_TARGET_X64=1"
    "JIT_TARGET_X64=0"
)

# ========== Python embedding modules (python.cmo / pyplot.cmo / py_bridge*) ==========
# Defined in cmake/options.json; CAMEL_SKIP_PYTHON=1 forces them off during configuration (higher priority than cache).
option(
    CAMEL_ENABLE_PYTHON
    "Build python.cmo, pyplot.cmo, and py_bridge* (requires Python dev layout)"
    ON
)
if(DEFINED ENV{CAMEL_SKIP_PYTHON} AND "$ENV{CAMEL_SKIP_PYTHON}" MATCHES "^(1|ON|YES|TRUE)$")
    set(CAMEL_ENABLE_PYTHON OFF CACHE BOOL "disabled via CAMEL_SKIP_PYTHON" FORCE)
endif()
message(STATUS "CAMEL_ENABLE_PYTHON=${CAMEL_ENABLE_PYTHON}")

# ========== The Python host binding (bindings/python: `camel._camel`) ==========
# Independent of CAMEL_ENABLE_PYTHON, which embeds Python in Camel (the other direction): this is
# Camel inside Python. It builds only when Python's development files are present, but a caller may
# also want it gone by name -- a tree that is read rather than installed, or a source package whose
# readers have no CPython 3.10.
option(
    CAMEL_ENABLE_PYTHON_BINDINGS
    "Build the CPython extension camel._camel (bindings/python)"
    ON
)
message(STATUS "CAMEL_ENABLE_PYTHON_BINDINGS=${CAMEL_ENABLE_PYTHON_BINDINGS}")

# ========== The module set ==========
# Which modules/*.cmo an install carries. The whole tree builds the whole list; the two knobs below
# exist because a source package is read and built by people who do not need every library, and a
# module that is never built is a directory they never have to compile.
#
#   CAMEL_PACKAGE_MINIMAL=ON   drop the sets that are about running AI workloads and about hosting
#                              Camel somewhere else: tiles (GPU), onnx (deployment), ckpt, the
#                              Python embedding modules and the Python host binding. The numeric
#                              core stays -- tensor, random, autodiff and transforms are what makes
#                              the graph story concrete -- and so do the rewrite/verify modules.
#                              nn stays too: it is one of the four modules this switch chooses
#                              between (tiles, onnx, nn, ckpt), and the case study and the
#                              MLP/transformer training steps import it for sgd and save_params --
#                              three of the four are dropped, not all four.
#   CAMEL_MODULES=<list>       expert form: exactly these modules, in this order. Empty means "the
#                              set the line above decides". A name that is not a module, or a
#                              module whose link dependency is missing, fails the configure with a
#                              message naming both (the link would otherwise fail much later).
option(
    CAMEL_PACKAGE_MINIMAL
    "Build the subset an anonymous source package ships (no tiles/onnx/ckpt, no Python embedding or binding)"
    OFF
)
set(CAMEL_MODULES "" CACHE STRING
    "Semicolon list of modules to build (empty = every module, or the CAMEL_PACKAGE_MINIMAL subset)")
message(STATUS "CAMEL_PACKAGE_MINIMAL=${CAMEL_PACKAGE_MINIMAL} (CAMEL_MODULES=\"${CAMEL_MODULES}\")")

# ========== Tensor module ==========
# Consumed by modules/tensor/CMakeLists.txt; OFF uses the builtin GEMM kernels.
option(
    CAMEL_TENSOR_BLAS
    "Use a system CBLAS (find_package(BLAS)) for a matmul whose node the 7.3 rule lets run on the library GEMM (tensor.kernel=\"library\", written by tensor::fuse); safe mode and every withheld node stay on the canonical builtin kernels. On by default; when no CBLAS is found the builtin GEMM is the library path"
    ON
)
message(STATUS "CAMEL_TENSOR_BLAS=${CAMEL_TENSOR_BLAS}")

# ========== Target instruction set of libcamel ==========
# CAMEL_TARGET_ARCH selects the -march value libcamel is compiled with:
#   native    (default) tune for the build machine; the binary may not run on another CPU.
#   baseline  no -march flag: the compiler's default for the target (x86-64: SSE2 only).
#   <value>   passed through as -march=<value>, e.g. x86-64-v2, x86-64-v3, haswell, armv8-a.
# The CAMEL_TARGET_ARCH environment variable, when set and non-empty, takes precedence over the
# cache for that configure run only (it is not written back), so release builds can pin a
# portable baseline without touching the scripts.
set(CAMEL_TARGET_ARCH "native" CACHE STRING
    "-march value for libcamel: native (build machine), baseline (compiler default), or a -march value such as x86-64-v2")
set(_camel_target_arch "${CAMEL_TARGET_ARCH}")
if(DEFINED ENV{CAMEL_TARGET_ARCH} AND NOT "$ENV{CAMEL_TARGET_ARCH}" STREQUAL "")
    set(_camel_target_arch "$ENV{CAMEL_TARGET_ARCH}")
endif()
set(CAMEL_TARGET_ARCH_FLAGS "")
if(_camel_target_arch STREQUAL "native")
    list(APPEND CAMEL_TARGET_ARCH_FLAGS -march=native)
    # Historical local-build behaviour: FMA is requested on x86-64 even if -march=native lacks it.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|X86_64")
        list(APPEND CAMEL_TARGET_ARCH_FLAGS -mfma)
    endif()
elseif(NOT _camel_target_arch STREQUAL "baseline")
    list(APPEND CAMEL_TARGET_ARCH_FLAGS "-march=${_camel_target_arch}")
endif()
message(STATUS "CAMEL_TARGET_ARCH=${_camel_target_arch} (flags: ${CAMEL_TARGET_ARCH_FLAGS})")

message(STATUS "========================================")
