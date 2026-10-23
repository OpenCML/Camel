// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Element indexing, printing, and runtime controls (thread count, GEMM backend
 * name). Printing and the controls are effectful and marked impure.
 */

#include "../binary.h"
#include "../interop.h"
#include "../kernels/gemm.h"
#include "../kernels/parallel.h"
#include "../npy.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/bytes.h"
#include "camel/utils/path.h"
#include "catalog.h"
#include "support.h"
#include <format>

#include <iostream>

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

slot_t idxKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("idx", [&] {
        Array *t     = tensorArg(norm, 0);
        const int64_t index = intArg(norm, 1);
        if (t->rank() != 1) {
            throw std::invalid_argument("single-index access requires a rank-1 tensor");
        }
        if (index < 0 || index >= t->dim(0)) {
            throw std::invalid_argument("tensor index out of bounds");
        }
        return camel::core::rtdata::toSlot(t->getAsDouble(static_cast<uint64_t>(index)));
    });
}

slot_t idx2dKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("idx2d", [&] {
        Array *t   = tensorArg(norm, 0);
        const int64_t row = intArg(norm, 1), col = intArg(norm, 2);
        if (t->rank() != 2) {
            throw std::invalid_argument("two-index access requires a rank-2 tensor");
        }
        if (row < 0 || row >= t->dim(0) || col < 0 || col >= t->dim(1)) {
            throw std::invalid_argument("tensor index out of bounds");
        }
        return camel::core::rtdata::toSlot(
            t->getAsDouble(static_cast<uint64_t>(row * t->dim(1) + col)));
    });
}

/// `t[i]`, `t[i, j]`: a float. When the tensor's shape and an index are known at compile time,
/// a wrong rank or an index outside the extent is an error here (design principles 3.9);
/// otherwise the kernel checks at run time.
std::optional<Type *> indexInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    const size_t indices = ctx.size() - 1;
    if (in.shape) {
        if (in.shape->size() != indices) {
            throw std::invalid_argument(
                std::format(
                    "{}-index access requires a rank-{} tensor, this one has rank {}",
                    indices == 1 ? "single" : "two",
                    indices,
                    in.shape->size()));
        }
        for (size_t k = 0; k < indices; ++k) {
            const auto index     = ctx.constInt(k + 1);
            const int64_t extent = (*in.shape)[k];
            if (index && extent != kUnknownDim && (*index < 0 || *index >= extent)) {
                throw std::invalid_argument(
                    std::format("index {} is outside axis {} of extent {}", *index, k, extent));
            }
        }
    }
    return Type::Float64();
}
/// `for x in t`: the elements of a tensor along its first dimension, in order. A tensor of
/// rank n >= 2 gives, for each i, the sub-tensor of shape shape[1:] that starts at row i (a
/// view of the same block); a rank-1 tensor gives, for each i, the rank-0 sub-tensor at i.
/// A rank-0 tensor has no first dimension: it is not iterable, which is an error when the
/// shape is known and a fault otherwise.
std::optional<Type *> iterInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    if (in.shape) {
        if (in.shape->empty()) {
            throw std::invalid_argument("a rank-0 tensor has no first dimension");
        }
        StaticShape row(in.shape->begin() + 1, in.shape->end());
        return ArrayType::create(TensorType::get(in.dtype, row));
    }
    return ArrayType::create(TensorType::get(in.dtype, std::nullopt));
}

slot_t iterKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("iter", [&] {
        Array *t = tensorArg(norm, 0);
        if (t->rank() == 0) {
            throw std::invalid_argument("a rank-0 tensor has no first dimension");
        }
        const auto shape    = t->shapeSpan();
        const uint64_t rows = static_cast<uint64_t>(shape[0]);
        const StaticShape rowShape(shape.begin() + 1, shape.end());
        const uint64_t inner = numelOf(rowShape);
        auto *arrayType      = ArrayType::create(TensorType::get(t->dtype(), rowShape));
        ::Array *result      = ::Array::create(mm::autoSpace(), static_cast<size_t>(rows));
        for (uint64_t i = 0; i < rows; ++i) {
            Array *row = viewTensor(t, rowShape, mm::autoSpace(), i * inner);
            result->set<rtdata::Object *>(i, row, arrayType);
        }
        return camel::core::rtdata::toSlot(result);
    });
}

std::optional<Type *> voidResult(const InferContext &) { return Type::Void(); }

slot_t showKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("show", [&] {
        Array *t = tensorArg(norm, 0, true);
        t->print(std::cout, TensorType::Default());
        std::cout << std::endl;
        return NullSlot;
    });
}

slot_t loadNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("load_npy", [&] {
        return wrap(loadNpy(camel::utils::pathFromUtf8(stringArg(norm, 0)), resultAllocator()));
    });
}

slot_t saveNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("save_npy", [&] {
        saveNpy(tensorArg(norm, 0), camel::utils::pathFromUtf8(stringArg(norm, 1)));
        return NullSlot;
    });
}

slot_t toBytesKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("to_bytes", [&] {
        const auto bytes = tensorToLittleEndian(tensorArg(norm, 0));
        return camel::core::rtdata::toSlot(
            camel::core::rtdata::BytesObject::from(bytes, resultAllocator()));
    });
}

slot_t setThreadsKernel(ArgsView &, ArgsView &norm, context::Context &) {
    k::setNumThreads(static_cast<int>(intArg(norm, 0)));
    return NullSlot;
}

slot_t numThreadsKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Int64>(k::numThreads()));
}

/// The thread count the GEMM path had for its most recent call: 1 for a library call, the pool's
/// width for a canonical call, 1 inside a region (gemm.h).
slot_t gemmThreadsKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Int64>(k::gemmThreadsUsed()));
}

slot_t backendKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(
        ::String::from(std::string(k::gemmBackendName()), mm::autoSpace()));
}

slot_t libraryKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(
        ::String::from(std::string(k::libraryGemmName()), mm::autoSpace()));
}

} // namespace

std::vector<OpDef> utilityOps() {
    const OpTraits impure{.pure = false};
    std::vector<OpDef> defs;
    defs.push_back(
        OpDef{
            .name      = "to_bytes",
            .exports   = {"to_bytes"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "Bytes",
            .infer = [](const InferContext &) -> std::optional<Type *> { return BytesType::get(); },
            .kernel = &toBytesKernel,
            .traits = {}});
    defs.push_back(
        OpDef{
            .name      = "idx",
            .exports   = {"__idx__"},
            .params    = {{"t", ParamKind::Tensor}, {"i", ParamKind::Int}},
            .resultDoc = "float",
            .infer     = indexInfer,
            .kernel    = &idxKernel,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "idx2d",
            .exports   = {"__idx__"},
            .params    = {{"t", ParamKind::Tensor}, {"i", ParamKind::Int}, {"j", ParamKind::Int}},
            .resultDoc = "float",
            .infer     = indexInfer,
            .kernel    = &idx2dKernel,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "iter",
            .exports   = {"__iter__"},
            .params    = {{"t", ParamKind::Tensor}},
            .resultDoc = "Tensor[]",
            .infer     = iterInfer,
            .kernel    = &iterKernel,
            .traits    = {}});
    defs.push_back(
        OpDef{
            .name      = "show",
            .exports   = {"show"},
            .params    = {{"t", ParamKind::TensorLike}},
            .resultDoc = "void",
            .infer     = voidResult,
            .kernel    = &showKernel,
            .traits    = impure});
    defs.push_back(
        OpDef{
            .name      = "load_npy",
            .exports   = {"load_npy"},
            .params    = {{"path", ParamKind::String}},
            .resultDoc = "Tensor",
            .infer     = [](const InferContext &) -> std::optional<Type *> {
                return TensorType::Default();
            },
            .kernel = &loadNpyKernel,
            .traits = impure});
    defs.push_back(
        OpDef{
            .name      = "save_npy",
            .exports   = {"save_npy"},
            .params    = {{"t", ParamKind::Tensor}, {"path", ParamKind::String}},
            .resultDoc = "void",
            .infer     = voidResult,
            .kernel    = &saveNpyKernel,
            .traits    = impure});
    defs.push_back(
        OpDef{
            .name      = "set_num_threads",
            .exports   = {"set_num_threads"},
            .params    = {{"n", ParamKind::Int}},
            .resultDoc = "void",
            .infer     = voidResult,
            .kernel    = &setThreadsKernel,
            .traits    = impure});
    defs.push_back(
        OpDef{
            .name      = "num_threads",
            .exports   = {"num_threads"},
            .params    = {},
            .resultDoc = "int",
            .infer  = [](const InferContext &) -> std::optional<Type *> { return Type::Int64(); },
            .kernel = &numThreadsKernel,
            .traits = impure});
    defs.push_back(
        OpDef{
            .name      = "gemm_threads",
            .exports   = {"gemm_threads"},
            .params    = {},
            .resultDoc = "int",
            .infer  = [](const InferContext &) -> std::optional<Type *> { return Type::Int64(); },
            .kernel = &gemmThreadsKernel,
            .traits = impure});
    defs.push_back(
        OpDef{
            .name      = "gemm_backend",
            .exports   = {"gemm_backend"},
            .params    = {},
            .resultDoc = "string",
            .infer  = [](const InferContext &) -> std::optional<Type *> { return Type::String(); },
            .kernel = &backendKernel,
            .traits = {}});
    defs.push_back(
        OpDef{
            .name      = "gemm_library",
            .exports   = {"gemm_library"},
            .params    = {},
            .resultDoc = "string",
            .infer  = [](const InferContext &) -> std::optional<Type *> { return Type::String(); },
            .kernel = &libraryKernel,
            .traits = {}});
    return defs;
}

} // namespace camel::tensor::ops
