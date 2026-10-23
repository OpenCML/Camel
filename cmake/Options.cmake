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

add_bool_option(ENABLE_FASTVM_COMPUTED_GOTO "Use computed goto in interpreter" ON)

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
