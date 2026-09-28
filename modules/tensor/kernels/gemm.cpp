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
 * GEMM backend selection and the matmul/linear operators built on it.
 *
 * The builtin backend is compiled from gemm_blocked.inl once per ISA: a
 * portable unit, an AVX2+FMA unit, and an AVX-512F unit (x86-64 builds). The
 * widest unit the CPU and OS support is selected at first use, so default
 * builds stay portable while still using wide vectors where available. With
 * CAMEL_TENSOR_USE_CBLAS the call is forwarded to cblas_sgemm instead.
 */

#include "gemm.h"

#include "elementwise.h"
#include "parallel.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

#if defined(CAMEL_TENSOR_USE_CBLAS)
#include <cblas.h>
#endif

namespace camel::tensor::kernels {

namespace detail {
using SgemmFn = void (*)(
    bool, bool, int64_t, int64_t, int64_t, float, const float *, int64_t, const float *, int64_t,
    float, float *, int64_t);
void sgemmGeneric(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
void sgemmAvx2(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);
void sgemmAvx512(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc);
#endif
} // namespace detail

using type::TypeCode;

namespace {

#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
struct CpuFeatures {
    bool avx2Fma = false; // AVX2 + FMA with OS-enabled YMM state
    bool avx512f = false; // AVX-512F with OS-enabled ZMM and opmask state
};

CpuFeatures detectCpuFeatures() {
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    auto cpuid = [&](unsigned int leaf, unsigned int sub) {
#if defined(_MSC_VER) && !defined(__clang__)
        int regs[4];
        __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(sub));
        eax = regs[0], ebx = regs[1], ecx = regs[2], edx = regs[3];
#else
        __asm__ __volatile__("cpuid"
                             : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                             : "a"(leaf), "c"(sub));
#endif
    };
    CpuFeatures features;
    cpuid(0, 0);
    if (eax < 7) {
        return features;
    }
    cpuid(1, 0);
    const bool fma     = (ecx & (1u << 12)) != 0;
    const bool osxsave = (ecx & (1u << 27)) != 0;
    const bool avx     = (ecx & (1u << 28)) != 0;
    if (!(osxsave && avx)) {
        return features;
    }
    unsigned int xcr0Low = 0, xcr0High = 0;
    __asm__ __volatile__("xgetbv" : "=a"(xcr0Low), "=d"(xcr0High) : "c"(0));
    const bool ymmState = (xcr0Low & 0x06u) == 0x06u; // XMM, YMM
    const bool zmmState = (xcr0Low & 0xE6u) == 0xE6u; // + opmask, ZMM_Hi256, Hi16_ZMM
    cpuid(7, 0);
    features.avx2Fma = ymmState && fma && (ebx & (1u << 5)) != 0;
    features.avx512f = zmmState && features.avx2Fma && (ebx & (1u << 16)) != 0;
    return features;
}
#endif

struct BuiltinBackend {
    detail::SgemmFn fn;
    std::string_view name;
};

const BuiltinBackend &builtinBackend() {
    static const BuiltinBackend backend = [] {
#if defined(CAMEL_TENSOR_HAS_AVX2_UNIT)
        const CpuFeatures cpu = detectCpuFeatures();
        if (cpu.avx512f) {
            return BuiltinBackend{&detail::sgemmAvx512, "builtin-avx512"};
        }
        if (cpu.avx2Fma) {
            return BuiltinBackend{&detail::sgemmAvx2, "builtin-avx2"};
        }
#endif
        return BuiltinBackend{&detail::sgemmGeneric, "builtin"};
    }();
    return backend;
}

/// Integer matmul for int64 (and promoted bool) operands; not performance critical.
void integerGemm(int64_t M, int64_t N, int64_t K, const int64_t *A, const int64_t *B, int64_t *C) {
    for (int64_t i = 0; i < M; ++i) {
        for (int64_t j = 0; j < N; ++j) {
            int64_t acc = 0;
            for (int64_t k = 0; k < K; ++k) {
                acc += A[i * K + k] * B[k * N + j];
            }
            C[i * N + j] = acc;
        }
    }
}

struct MatmulPlan {
    Shape outShape;
    Shape batchShape;
    int64_t M = 1, N = 1, K = 1;
    Shape lhsBatch, rhsBatch; // batch extents right-aligned to batchShape
};

MatmulPlan planMatmul(std::span<const int64_t> lhs, std::span<const int64_t> rhs) {
    if (lhs.empty() || rhs.empty()) {
        throw std::invalid_argument("matmul does not accept rank-0 tensors");
    }
    MatmulPlan plan;
    const bool lhsVector = lhs.size() == 1;
    const bool rhsVector = rhs.size() == 1;
    plan.M               = lhsVector ? 1 : lhs[lhs.size() - 2];
    plan.K               = lhs.back();
    const int64_t rhsK   = rhsVector ? rhs[0] : rhs[rhs.size() - 2];
    plan.N               = rhsVector ? 1 : rhs.back();
    if (plan.K != rhsK) {
        throw ShapeError(
            "matmul inner dimensions differ: " + std::to_string(plan.K) + " vs " +
            std::to_string(rhsK));
    }
    const auto lhsBatch = lhs.subspan(0, lhs.size() - (lhsVector ? 1 : 2));
    const auto rhsBatch = rhs.subspan(0, rhs.size() - (rhsVector ? 1 : 2));
    plan.batchShape     = broadcastShapes(lhsBatch, rhsBatch);
    plan.lhsBatch.assign(plan.batchShape.size() - lhsBatch.size(), 1);
    plan.lhsBatch.insert(plan.lhsBatch.end(), lhsBatch.begin(), lhsBatch.end());
    plan.rhsBatch.assign(plan.batchShape.size() - rhsBatch.size(), 1);
    plan.rhsBatch.insert(plan.rhsBatch.end(), rhsBatch.begin(), rhsBatch.end());
    plan.outShape = plan.batchShape;
    if (!lhsVector) {
        plan.outShape.push_back(plan.M);
    }
    if (!rhsVector) {
        plan.outShape.push_back(plan.N);
    }
    return plan;
}

/// Offset (in matrices) of batch index `b` within an operand whose batch extents are `extents`.
int64_t batchOffset(const Shape &batchShape, const Shape &extents, int64_t b) {
    int64_t offset = 0, stride = 1;
    for (size_t d = batchShape.size(); d-- > 0;) {
        const int64_t idx = b % batchShape[d];
        b /= batchShape[d];
        if (extents[d] != 1) {
            offset += idx * stride;
        }
        stride *= extents[d];
    }
    return offset;
}

} // namespace

void sgemm(
    bool transA, bool transB, int64_t M, int64_t N, int64_t K, float alpha, const float *A,
    int64_t lda, const float *B, int64_t ldb, float beta, float *C, int64_t ldc) {
#if defined(CAMEL_TENSOR_USE_CBLAS)
    cblas_sgemm(
        CblasRowMajor,
        transA ? CblasTrans : CblasNoTrans,
        transB ? CblasTrans : CblasNoTrans,
        static_cast<int>(M),
        static_cast<int>(N),
        static_cast<int>(K),
        alpha,
        A,
        static_cast<int>(lda),
        B,
        static_cast<int>(ldb),
        beta,
        C,
        static_cast<int>(ldc));
#else
    builtinBackend().fn(transA, transB, M, N, K, alpha, A, lda, B, ldb, beta, C, ldc);
#endif
}

std::string_view gemmBackendName() {
#if defined(CAMEL_TENSOR_USE_CBLAS)
    return "cblas";
#else
    return builtinBackend().name;
#endif
}

Shape matmulShape(std::span<const int64_t> lhs, std::span<const int64_t> rhs) {
    return planMatmul(lhs, rhs).outShape;
}

TensorObject *matmul(const TensorObject *lhs, const TensorObject *rhs, mm::IAllocator &allocator) {
    const MatmulPlan plan  = planMatmul(lhs->shapeSpan(), rhs->shapeSpan());
    const TypeCode outType = promoteTensorTypes(lhs->dtype(), rhs->dtype());
    const TypeCode compute = outType == TypeCode::Bool ? TypeCode::Int64 : outType;
    const TensorObject *a  = lhs->dtype() == compute ? lhs : cast(lhs, compute, allocator);
    const TensorObject *b  = rhs->dtype() == compute ? rhs : cast(rhs, compute, allocator);
    TensorObject *out      = TensorObject::create(compute, plan.outShape, allocator);

    const int64_t batches = static_cast<int64_t>(numelOf(plan.batchShape));
    const int64_t aSize = plan.M * plan.K, bSize = plan.K * plan.N, cSize = plan.M * plan.N;
    // Many small matrices: parallelize over the batch (inner GEMMs then run serially).
    parallelFor(batches, 1, [&](int64_t begin, int64_t end) {
        for (int64_t batch = begin; batch < end; ++batch) {
            const int64_t aOff = batchOffset(plan.batchShape, plan.lhsBatch, batch) * aSize;
            const int64_t bOff = batchOffset(plan.batchShape, plan.rhsBatch, batch) * bSize;
            if (compute == TypeCode::Float32) {
                sgemm(
                    false,
                    false,
                    plan.M,
                    plan.N,
                    plan.K,
                    1.0f,
                    a->dataAs<float>() + aOff,
                    plan.K,
                    b->dataAs<float>() + bOff,
                    plan.N,
                    0.0f,
                    out->dataAs<float>() + batch * cSize,
                    plan.N);
            } else {
                integerGemm(
                    plan.M,
                    plan.N,
                    plan.K,
                    a->dataAs<int64_t>() + aOff,
                    b->dataAs<int64_t>() + bOff,
                    out->dataAs<int64_t>() + batch * cSize);
            }
        }
    });
    if (compute != outType) {
        return cast(out, outType, allocator);
    }
    return out;
}

TensorObject *linear(
    const TensorObject *x, const TensorObject *weight, const TensorObject *bias,
    mm::IAllocator &allocator) {
    if (weight->rank() != 2) {
        throw std::invalid_argument("linear expects a rank-2 weight [in, out]");
    }
    if (x->rank() == 0 || x->dim(x->rank() - 1) != weight->dim(0)) {
        throw ShapeError("linear: input feature dimension does not match weight rows");
    }
    const int64_t out = weight->dim(1);
    if (bias && !(bias->rank() == 1 && bias->dim(0) == out)) {
        throw ShapeError("linear expects a rank-1 bias [out]");
    }
    const bool allFloat = x->dtype() == TypeCode::Float32 && weight->dtype() == TypeCode::Float32 &&
                          (!bias || bias->dtype() == TypeCode::Float32);
    if (!allFloat) {
        TensorObject *product = matmul(x, weight, allocator);
        return bias ? binary(BinaryOp::Add, Operand::of(product), Operand::of(bias), allocator)
                    : product;
    }
    const int64_t in = weight->dim(0);
    const int64_t M  = static_cast<int64_t>(x->numel()) / std::max<int64_t>(in, 1);
    Shape outShape   = x->shapeVector();
    outShape.back()  = out;
    TensorObject *y  = TensorObject::create(TypeCode::Float32, outShape, allocator);
    float *dst       = y->dataAs<float>();
    float beta       = 0.0f;
    if (bias) {
        const float *pb = bias->dataAs<float>();
        for (int64_t i = 0; i < M; ++i) {
            std::memcpy(dst + i * out, pb, static_cast<size_t>(out) * sizeof(float));
        }
        beta = 1.0f;
    }
    sgemm(
        false,
        false,
        M,
        out,
        in,
        1.0f,
        x->dataAs<float>(),
        in,
        weight->dataAs<float>(),
        out,
        beta,
        dst,
        out);
    return y;
}

} // namespace camel::tensor::kernels
