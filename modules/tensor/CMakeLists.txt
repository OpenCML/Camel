include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME tensor
  SOURCES
    attrs.cpp
    binary.cpp
    dtype.cpp
    fusion_group.cpp
    interop.cpp
    npy.cpp
    module.cpp
    tensor.cpp
    type.cpp
    kernels/conv.cpp
    kernels/cpu.cpp
    kernels/elementwise.cpp
    kernels/gradient.cpp
    kernels/gradient_generic.cpp
    kernels/gemm.cpp
    kernels/gemm_generic.cpp
    kernels/layout.cpp
    kernels/parallel.cpp
    kernels/reduce.cpp
    kernels/unary_generic.cpp
    kernels/update.cpp
    kernels/update_generic.cpp
    ops/catalog.cpp
    ops/binary_ops.cpp
    ops/creation_ops.cpp
    ops/elementwise_ops.cpp
    ops/gradient_ops.cpp
    ops/layout_ops.cpp
    ops/linalg_ops.cpp
    ops/op_def.cpp
    ops/op_stats.cpp
    ops/reduction_ops.cpp
    ops/registry.cpp
    ops/index_builder.cpp
    ops/indexing.cpp
    ops/semantics.cpp
    ops/support.cpp
    ops/utility_ops.cpp
    ops/vjp.cpp
    passes/fuse.cpp
    passes/index_eval.cpp
)

# Contraction (a * b + c into one fused multiply-add) is off in the whole project (top-level
# CMakeLists.txt), the GEMM units included: matmul's canonical algorithm is a chain of fused
# multiply-adds in ascending k (design principles 7.1), and gemm_blocked.inl writes each one
# explicitly, so every unit computes the same bits, with or without FMA hardware.
# The unary units (exp, tanh, sigmoid, gelu: bounded, not correctly rounded) are compiled with
# contraction on: their polynomials may then round differently on processors with and without
# FMA, within the functions' declared error bounds; the correctly rounded operators among them
# (neg, abs, sqrt, rsqrt, relu) contain no multiply-add to contract.
set_source_files_properties(kernels/unary_generic.cpp
  PROPERTIES COMPILE_OPTIONS "-ffp-contract=on")
# The fused GELU gradient follows the same rule: its tangent is the unary unit's tangent, so the
# units carry the unary units' contraction opt-in, and the polynomial and the slope around it opt
# back out with a pragma in gradient_loops.inl (their products are the operator chain's, which ran
# with the project's -ffp-contract=off).
set_source_files_properties(kernels/gradient_generic.cpp
  PROPERTIES COMPILE_OPTIONS "-ffp-contract=on")

# x86-64: add AVX2+FMA and AVX-512F GEMM and unary units, selected at runtime after a CPU check.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|X86_64")
  target_sources(tensor_cmo_objects PRIVATE
    kernels/gemm_avx2.cpp kernels/gemm_avx512.cpp kernels/unary_avx2.cpp kernels/unary_avx512.cpp
    kernels/update_avx2.cpp kernels/gradient_avx2.cpp)
  set_source_files_properties(kernels/gemm_avx2.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
  set_source_files_properties(kernels/unary_avx2.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-ffp-contract=on")
  set_source_files_properties(kernels/gemm_avx512.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx2;-mfma")
  set_source_files_properties(kernels/unary_avx512.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx2;-mfma;-ffp-contract=on")
  # The fused update is the one per-ISA unit whose arithmetic must stay rounded as written: its
  # values are the bits the scalar loop it replaced produced, so it is compiled with the project's
  # -ffp-contract=off, not with the unary units' opt-in.
  set_source_files_properties(kernels/update_avx2.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
  # The fused GELU gradient is compiled with the unary units' contraction opt-in, because the
  # tangent inside it is the unary unit's tangent; the polynomial and the slope opt back out with
  # a pragma in gradient_loops.inl, so their products stay rounded as written.
  set_source_files_properties(kernels/gradient_avx2.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma;-ffp-contract=on")
  target_compile_definitions(tensor_cmo_objects PRIVATE CAMEL_TENSOR_HAS_AVX2_UNIT=1)
endif()

# System CBLAS for GEMM (cmake/options.json: CAMEL_TENSOR_BLAS, on by default). A build that
# cannot find one keeps the builtin kernels: sgemmLibrary then forwards to sgemm, so the program
# is unchanged and nothing fails. What the build chose is printed at configure time.
#
# find_package(BLAS) is tried first; the explicit search below covers the distributions whose
# headers live under an architecture suffix (Debian/Ubuntu install cblas.h in
# /usr/include/x86_64-linux-gnu) and configurations where FindBLAS is restricted by a package
# manager's toolchain. Only a CBLAS (cblas_sgemm), not a Fortran-only BLAS, can back the kernel.
if(CAMEL_TENSOR_BLAS)
  find_package(BLAS)
  find_library(
    CAMEL_CBLAS_LIBRARY
    NAMES openblas cblas mkl_rt
    PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu openblas-pthread)
  find_path(
    CAMEL_CBLAS_INCLUDE_DIR
    NAMES cblas.h
    PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu openblas openblas-pthread)
  if(CAMEL_CBLAS_LIBRARY AND CAMEL_CBLAS_INCLUDE_DIR)
    set(CAMEL_BLAS_LIBRARIES ${CAMEL_CBLAS_LIBRARY})
    if(BLAS_FOUND)
      list(APPEND CAMEL_BLAS_LIBRARIES ${BLAS_LIBRARIES})
    endif()
    target_include_directories(tensor_cmo_objects PRIVATE ${CAMEL_CBLAS_INCLUDE_DIR})
    target_link_libraries(tensor_cmo PRIVATE ${CAMEL_BLAS_LIBRARIES})
    target_compile_definitions(tensor_cmo_objects PRIVATE CAMEL_TENSOR_USE_CBLAS=1)
    string(TOLOWER "${CAMEL_CBLAS_LIBRARY}" _camel_cblas_lower)
    if(_camel_cblas_lower MATCHES "openblas")
      target_compile_definitions(tensor_cmo_objects PRIVATE CAMEL_TENSOR_HAS_OPENBLAS=1)
    elseif(_camel_cblas_lower MATCHES "mkl")
      target_compile_definitions(tensor_cmo_objects PRIVATE CAMEL_TENSOR_HAS_MKL=1)
    endif()
    message(STATUS "CAMEL_TENSOR_BLAS: library GEMM is ${CAMEL_BLAS_LIBRARIES}")
  else()
    message(STATUS "CAMEL_TENSOR_BLAS: no CBLAS found (library=${CAMEL_CBLAS_LIBRARY}, cblas.h=${CAMEL_CBLAS_INCLUDE_DIR}); the builtin GEMM is the library path")
  endif()
endif()
