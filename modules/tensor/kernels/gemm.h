/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * General matrix multiply and the tensor-level operators built on it.
 *
 * `sgemm` computes C = alpha * op(A) * op(B) + beta * C for row-major float
 * matrices, where op(X) is X or X^T. Each element of op(A) * op(B) is matmul's
 * canonical algorithm (design principles 2.13): a chain of fused multiply-adds
 * in ascending k from +0; alpha and beta are applied after the chain
 * (gemm_blocked.inl). It has two backends selected at build time:
 *   - builtin: cache-blocked, packed, register-tiled, parallel over tiles; the
 *     same bits on every processor.
 *   - cblas:   forwards to cblas_sgemm (CMake option CAMEL_TENSOR_BLAS, off by
 *     default). A BLAS library chooses its own order of accumulation, so this
 *     backend does not follow the canonical algorithm: results may differ from
 *     the builtin backend and between machines.
 * `gemmBackendName()` reports which one is active.
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

std::string_view gemmBackendName();

/// Result shape of matmul(lhs, rhs). Throws std::invalid_argument when inner extents differ.
Shape matmulShape(std::span<const int64_t> lhs, std::span<const int64_t> rhs);

TensorObject *matmul(const TensorObject *lhs, const TensorObject *rhs, mm::IAllocator &allocator);

/// y = x @ weight (+ bias). `weight` is [in, out]; `bias` is [out] or null.
TensorObject *linear(
    const TensorObject *x, const TensorObject *weight, const TensorObject *bias,
    mm::IAllocator &allocator);

/**
 * matmul(x, weight) + bias, then relu when `relu` is set: the kernel behind the fused operators
 * produced by the `tensor::fuse` pass. Shapes, dtypes, and errors are those of the unfused
 * expression. When x and weight are float, weight is rank 2, and bias broadcasts only along the
 * last axis, the bias rows are preloaded as C and added by the GEMM after each element's chain
 * (beta = 1), which is the unfused addition `product + bias` bit for bit; relu is then applied
 * in place. Otherwise the unfused operations run. Either way the result is the unfused
 * expression's.
 */
TensorObject *matmulAdd(
    const TensorObject *x, const TensorObject *weight, Operand bias, bool relu,
    mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
