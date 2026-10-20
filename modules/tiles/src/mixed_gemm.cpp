/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Optional BF16 cuBLAS lowering. No host tensor computation or precision fallback.
#include "tiles/internal/mixed_gemm.h"

#include <memory>
#include <vector>
#ifdef CAMEL_TILES_CUDA
#include <algorithm>
#include <climits>
#include <cublas_v2.h>
#include <stdexcept>
#include <string>
#include <utility>

namespace camel::tiles::cuda_detail {
namespace {
void check(cublasStatus_t status, const char *where) {
    if (status != CUBLAS_STATUS_SUCCESS)
        throw std::runtime_error(
            std::string("cuBLAS BF16 ") + where + " failed: " + std::to_string(status));
}
int dimension(size_t size) {
    if (size > INT_MAX)
        throw std::runtime_error("cuBLAS BF16 dimension exceeds int32");
    return static_cast<int>(size);
}
// Map an output batch ordinal to the broadcast operand's matrix ordinal.
size_t batchOffset(size_t ordinal, const Shape &out, const Shape &operand) {
    size_t offset = 0, stride = 1;
    for (size_t d = out.size() - 2; d-- > 0;) {
        const auto coordinate = ordinal % out[d];
        ordinal /= out[d];
        const auto shift = out.size() - operand.size();
        if (d >= shift) {
            if (operand[d - shift] != 1)
                offset += coordinate * stride;
            stride *= operand[d - shift];
        }
    }
    return offset * elements(Shape(operand.end() - 2, operand.end()));
}
} // namespace

struct MixedGemm::Impl {
    std::shared_ptr<Backend> backend;
    Library library;
    cublasHandle_t handle = nullptr;
    decltype(&::cublasCreate_v2) create;
    decltype(&::cublasDestroy_v2) destroy;
    decltype(&::cublasSetStream_v2) setStream;
    decltype(&::cublasSetMathMode) setMath;
    decltype(&::cublasSetAtomicsMode) setAtomics;
    using Gemm = cublasStatus_t (*)(
        cublasHandle_t, cublasOperation_t, cublasOperation_t, int, int, int, const void *,
        const void *, cudaDataType_t, int, const void *, cudaDataType_t, int, const void *, void *,
        cudaDataType_t, int, cublasComputeType_t, cublasGemmAlgo_t);
    using Batched = cublasStatus_t (*)(
        cublasHandle_t, cublasOperation_t, cublasOperation_t, int, int, int, const void *,
        const void *, cudaDataType_t, int, long long, const void *, cudaDataType_t, int, long long,
        const void *, void *, cudaDataType_t, int, long long, int, cublasComputeType_t,
        cublasGemmAlgo_t);
    Gemm gemm;
    Batched batched;
    explicit Impl(std::shared_ptr<Backend> value, CUstream stream)
        : backend(std::move(value)),
          library({"libcublas.so.12", "libcublas.so.13", "cublas64_12.dll", "cublas64_13.dll"}) {
        int major = 0;
        backend->api.check(
            backend->api.cuDeviceGetAttribute(
                &major,
                CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,
                backend->device),
            "check BF16 device");
        if (major < 8)
            throw std::runtime_error("BF16 mixed GEMM requires an Ampere or newer GPU");
        create     = library.symbol<decltype(create)>("cublasCreate_v2");
        destroy    = library.symbol<decltype(destroy)>("cublasDestroy_v2");
        setStream  = library.symbol<decltype(setStream)>("cublasSetStream_v2");
        setMath    = library.symbol<decltype(setMath)>("cublasSetMathMode");
        setAtomics = library.symbol<decltype(setAtomics)>("cublasSetAtomicsMode");
        gemm       = library.symbol<Gemm>("cublasGemmEx");
        batched    = library.symbol<Batched>("cublasGemmStridedBatchedEx");
        check(create(&handle), "create");
        try {
            check(setStream(handle, reinterpret_cast<cudaStream_t>(stream)), "set stream");
            check(
                setMath(handle, CUBLAS_MATH_DISALLOW_REDUCED_PRECISION_REDUCTION),
                "set FP32 reduction");
            check(setAtomics(handle, CUBLAS_ATOMICS_NOT_ALLOWED), "disable atomics");
        } catch (...) {
            destroy(handle);
            handle = nullptr;
            throw;
        }
    }
    ~Impl() {
        if (handle && backend->api.cuCtxPushCurrent(backend->context) == CUDA_SUCCESS) {
            destroy(handle);
            CUcontext popped;
            backend->api.cuCtxPopCurrent(&popped);
        }
    }
};
MixedGemm::MixedGemm(std::shared_ptr<Backend> backend, CUstream stream)
    : impl(std::make_unique<Impl>(std::move(backend), stream)) {}
MixedGemm::~MixedGemm() = default;

void MixedGemm::run(const Program &p, const Node &node, const std::vector<CUdeviceptr> &pointers) {
    const auto &a = p.nodes[node.inputs[0]].shape, &b = p.nodes[node.inputs[1]].shape;
    size_t m = a[a.size() - 2], k = a.back(), n = b.back();
    auto *left        = reinterpret_cast<const float *>(pointers[node.inputs[0]]);
    auto *right       = reinterpret_cast<const float *>(pointers[node.inputs[1]]);
    const auto id     = static_cast<size_t>(&node - p.nodes.data());
    auto *output      = reinterpret_cast<float *>(pointers[id]);
    const float alpha = 1, beta = 0;
    const auto invoke = [&](size_t rows, const float *x, const float *y, float *z) {
        // Row-major C=A*B is column-major C^T=B^T*A^T. FAST_16BF
        // explicitly selects BF16 operand down-conversion and FP32 accumulation.
        check(
            impl->gemm(
                impl->handle,
                CUBLAS_OP_N,
                CUBLAS_OP_N,
                dimension(n),
                dimension(rows),
                dimension(k),
                &alpha,
                y,
                CUDA_R_32F,
                dimension(n),
                x,
                CUDA_R_32F,
                dimension(k),
                &beta,
                z,
                CUDA_R_32F,
                dimension(n),
                CUBLAS_COMPUTE_32F_FAST_16BF,
                CUBLAS_GEMM_DEFAULT),
            "GEMM");
    };
    if (b.size() == 2) {
        invoke(elements(a) / k, left, right, output);
        return;
    }
    const auto batches = elements(Shape(node.shape.begin(), node.shape.end() - 2));
    if (a.size() == b.size() && std::equal(a.begin(), a.end() - 2, b.begin())) {
        check(
            impl->batched(
                impl->handle,
                CUBLAS_OP_N,
                CUBLAS_OP_N,
                dimension(n),
                dimension(m),
                dimension(k),
                &alpha,
                right,
                CUDA_R_32F,
                dimension(n),
                k * n,
                left,
                CUDA_R_32F,
                dimension(k),
                m * k,
                &beta,
                output,
                CUDA_R_32F,
                dimension(n),
                m * n,
                dimension(batches),
                CUBLAS_COMPUTE_32F_FAST_16BF,
                CUBLAS_GEMM_DEFAULT),
            "batched GEMM");
    } else {
        for (size_t batch = 0; batch < batches; ++batch)
            invoke(
                m,
                left + batchOffset(batch, node.shape, a),
                right + batchOffset(batch, node.shape, b),
                output + batch * m * n);
    }
}
} // namespace camel::tiles::cuda_detail
#endif
