include(${CMAKE_CURRENT_SOURCE_DIR}/../camel_module.cmake)
add_camel_module(
  NAME tensor
  SOURCES
    dtype.cpp
    interop.cpp
    npy.cpp
    module.cpp
    tensor.cpp
    type.cpp
    kernels/conv.cpp
    kernels/cpu.cpp
    kernels/elementwise.cpp
    kernels/gradient.cpp
    kernels/gemm.cpp
    kernels/gemm_generic.cpp
    kernels/layout.cpp
    kernels/parallel.cpp
    kernels/reduce.cpp
    kernels/unary_generic.cpp
    ops/catalog.cpp
    ops/creation_ops.cpp
    ops/elementwise_ops.cpp
    ops/gradient_ops.cpp
    ops/layout_ops.cpp
    ops/linalg_ops.cpp
    ops/op_def.cpp
    ops/reduction_ops.cpp
    ops/registry.cpp
    ops/support.cpp
    ops/utility_ops.cpp
    ops/vjp.cpp
    passes/fuse.cpp
  EXTRA_INCLUDES
    ${CMAKE_SOURCE_DIR}/src
)

# x86-64: add AVX2+FMA and AVX-512F GEMM and unary units, selected at runtime after a CPU check.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|X86_64")
  target_sources(tensor_cmo PRIVATE
    kernels/gemm_avx2.cpp kernels/gemm_avx512.cpp kernels/unary_avx2.cpp kernels/unary_avx512.cpp)
  set_source_files_properties(kernels/gemm_avx2.cpp kernels/unary_avx2.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
  set_source_files_properties(kernels/gemm_avx512.cpp kernels/unary_avx512.cpp
    PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx2;-mfma")
  target_compile_definitions(tensor_cmo PRIVATE CAMEL_TENSOR_HAS_AVX2_UNIT=1)
endif()

# Optional system BLAS for GEMM (cmake/options.json: CAMEL_TENSOR_BLAS).
if(CAMEL_TENSOR_BLAS)
  find_package(BLAS REQUIRED)
  find_path(CAMEL_CBLAS_INCLUDE_DIR cblas.h PATH_SUFFIXES openblas REQUIRED)
  target_include_directories(tensor_cmo PRIVATE ${CAMEL_CBLAS_INCLUDE_DIR})
  target_link_libraries(tensor_cmo PRIVATE ${BLAS_LIBRARIES})
  target_compile_definitions(tensor_cmo PRIVATE CAMEL_TENSOR_USE_CBLAS=1)
endif()
