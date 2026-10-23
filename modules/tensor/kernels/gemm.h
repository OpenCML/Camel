// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * General matrix multiply and the tensor-level operators built on it.
 *
 * `sgemm` computes C = alpha * op(A) * op(B) + beta * C for row-major float
 * matrices, where op(X) is X or X^T. Each element of op(A) * op(B) is matmul's
 * canonical algorithm (design principles 7.1): a chain of fused multiply-adds
 * in ascending k from +0; alpha and beta are applied after the chain
 * (gemm_blocked.inl). It is the builtin backend: cache-blocked, packed,
 * register-tiled, parallel over tiles; the same bits on every processor.
 * `gemmBackendName()` reports which ISA unit runs it.
 *
 * `sgemmLibrary` forwards to cblas_sgemm when the build has a CBLAS (CMake
 * option CAMEL_TENSOR_BLAS, on by default; without one it forwards to `sgemm`).
 * A BLAS library chooses its own order and grouping of the accumulation (and
 * may not fuse the multiply-adds), so it does not follow the canonical
 * algorithm: it runs behind `tensor:matmul` and `matmul_grad_lhs/rhs` when the
 * node's `tensor.kernel` attribute says "library", which `tensor::fuse` writes
 * for a product the 7.3 rule lets use it (default mode; safe mode withholds it).
 * Below 2^30 multiply-adds per call, sgemmLibrary uses the canonical kernel:
 * paired measurements put the library's advantage only above that size, and a
 * batched attention product must not pay a per-call overhead it cannot repay.
 * The operator stays `matmul` (design principles 9.6). `libraryGemmName()`
 * reports what sgemmLibrary calls.
 *
 * `matmul` follows NumPy semantics: rank-1 operands are promoted to a row or
 * column vector and the promoted dimension is removed from the result; leading
 * (batch) dimensions broadcast.
 */

#pragma once

#include "../tensor.h"
#include "elementwise.h"

#include <string_view>

namespace camel::tensor::kernels {

void sgemm(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);

void sgemmLibrary(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);

std::string_view gemmBackendName();
std::string_view libraryGemmName();

/// Caps the library GEMM's own pool at the run's thread budget and at the measured knee (four
/// threads on the reference host, where 1024^3 is 458 ms canonical, 404 ms via the library at one
/// thread, 142 ms at four and 693 ms at 32). The cap keeps a batch loop from multiplying pools. A
/// build without a library does nothing.
void setGemmThreads(int threads);

/// Result shape of matmul(lhs, rhs). Throws std::invalid_argument when inner extents differ.
Shape matmulShape(std::span<const int64_t> lhs, std::span<const int64_t> rhs);

/// lhs @ rhs; f32 products by sgemm (canonical), or by sgemmLibrary when `library` is set.
Array *matmul(
    const Array *lhs, const Array *rhs, mm::IAllocator &allocator,
    bool library = false);

/// y = x @ weight (+ bias). `weight` is [in, out]; `bias` is [out] or null.
Array *linear(
    const Array *x, const Array *weight, const Array *bias,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
