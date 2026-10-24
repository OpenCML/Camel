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
#include "camel/core/error/runtime.h"
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
            // An index outside the array is one fault whatever its element type is (design
            // principles 3.9), and this op answers for the value whose element type the type does
            // not fix (the core's resolver declines those, so that the element is read at the type
            // the value carries). The fault is therefore raised here with the core's own message
            // -- `Index 8 is out of range for an array of length 3.` -- rather than a message of
            // this module's own, which is what a locationless kernel error would read as.
            camel::core::error::throwRuntimeFault(
                camel::core::error::RuntimeDiag::IndexOutOfRange, index, "an array", t->size());
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
        if (row < 0 || row >= t->dim(0)) {
            // The same fault as a one-index access, naming the axis the index left (3.9).
            camel::core::error::throwRuntimeFault(
                camel::core::error::RuntimeDiag::IndexOutOfRange, row, "an array", t->dim(0));
        }
        if (col < 0 || col >= t->dim(1)) {
            camel::core::error::throwRuntimeFault(
                camel::core::error::RuntimeDiag::IndexOutOfRange, col, "an array", t->dim(1));
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

/// A value with no dimension, asked for the first dimension a loop walks: the element it holds
/// (the rank-zero ruling, docs/handover/reviews/questions-2026-10-23.md section rank-zero-tensor)
/// has none to give. One message for both ways a loop meets one — the rank-0 array `wrap` keeps for
/// a slot whose type promises an array, and the element itself where such a slot holds no pointer
/// at all (the transition-period bare `Tensor`, the gap the author's ruling of 2026-10-25 on
/// no-dimension-value-in-an-array-typed-slot builds no mechanism for). It names what the loop
/// needed; reading the element's bits as a pointer is what used to say an argument "cannot be null".
constexpr const char *kNoFirstDimensionToIterate =
    "a value with no dimension has no first dimension to iterate";

/// The sequence `for x in xs` walks. `xs` is read as the array its type promises when it is a
/// numeric array — a tensor — and through `tensorArg` otherwise, which boxes an element whose type
/// states one (a rank-0 tensor's value is a scalar slot) so a kernel can read it.
Array *iterationSequence(ArgsView &norm) {
    Type *type = norm.type(0);
    if (type != nullptr && isTensorType(type) && norm.get<Array *>(0) == nullptr) {
        throw std::invalid_argument(kNoFirstDimensionToIterate);
    }
    return tensorArg(norm, 0);
}

/// `for x in t`: the elements of a tensor along its first dimension, in order. A tensor of
/// rank n >= 2 gives, for each i, the sub-tensor of shape shape[1:] that starts at row i (a
/// view of the same block); a rank-1 tensor gives, for each i, the rank-0 sub-tensor at i.
/// A value with no dimension has no first dimension: it is not iterable. A type never states a
/// rank-0 shape (`tensorOf` answers an empty shape with the element type, so `reshape(x, [])` is a
/// scalar and a `for` over it is refused where it is written, with `LoopSequenceNotIterable`); the
/// guard below stays for a shape a value carries that no type stated.
std::optional<Type *> iterInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    if (in.shape) {
        if (in.shape->empty()) {
            throw std::invalid_argument(kNoFirstDimensionToIterate);
        }
        StaticShape row(in.shape->begin() + 1, in.shape->end());
        return ArrayType::create(tensorOf(in.dtype, row));
    }
    return ArrayType::create(tensorOf(in.dtype, std::nullopt));
}

slot_t iterKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("iter", [&] {
        Array *t = iterationSequence(norm);
        if (t->rank() == 0) {
            throw std::invalid_argument(kNoFirstDimensionToIterate);
        }
        const auto shape    = t->shapeSpan();
        const uint64_t rows = static_cast<uint64_t>(shape[0]);
        // The round's type is the sequence's element type, as iterInfer declares it: the element
        // itself when the input's shape is known (the round of a rank-1 tensor is its rank-0
        // sub-tensor, which is the element — the rank-zero ruling), and a tensor of unknown rank
        // when the input's is not. Each round is handed on in the representation that type asks
        // for: a consumer reads the round by its declared type, and a round handed on as the
        // element under an array type left `tensor:multiply` reading an Array * out of a float
        // slot (the `gpu.profile/host_loop` crash, reached as soon as a loop iterates a tensor
        // whose shape is not fixed).
        Type *roundType = nullptr;
        if (Type *resultType = norm.resultType();
            resultType != nullptr && resultType->code() == TypeCode::Array) {
            roundType = static_cast<ArrayType *>(resultType)->elemType();
        }
        if (t->rank() == 1) {
            // One dimension: its elements are values of the element type (the rank-zero ruling,
            // docs/handover/reviews/questions-2026-10-23.md section rank-zero-tensor). The round
            // is the element itself and the sequence is `T[]`, the type iterInfer states.
            auto *arrayType = ArrayType::create(const_cast<Type *>(t->elemType()));
            ::Array *result = ::Array::create(mm::autoSpace(), static_cast<size_t>(rows));
            for (uint64_t i = 0; i < rows; ++i) {
                const slot_t element = t->getSlot(i);
                if (roundType == nullptr || roundType->code() != TypeCode::Array) {
                    result->setSlot(i, element, arrayType);
                    continue;
                }
                Array *round = createTensor(t->dtype(), StaticShape{}, mm::autoSpace());
                round->setSlot(0, element);
                result->setSlot(i, wrap(roundType, round), arrayType);
            }
            return camel::core::rtdata::toSlot(result);
        }
        const StaticShape rowShape(shape.begin() + 1, shape.end());
        const uint64_t inner = numelOf(rowShape);
        auto *arrayType      = ArrayType::create(tensorOf(t->dtype(), rowShape));
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
        t->print(std::cout, nullptr);
        std::cout << std::endl;
        return NullSlot;
    });
}

slot_t loadNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("load_npy", [&] {
        return wrap(
            norm.resultType(),
            loadNpy(camel::utils::pathFromUtf8(stringArg(norm, 0)), resultAllocator()));
    });
}

slot_t saveNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("save_npy", [&] {
        saveNpy(tensorArg(norm, 0, true), camel::utils::pathFromUtf8(stringArg(norm, 1)));
        return NullSlot;
    });
}

slot_t toBytesKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("to_bytes", [&] {
        const auto bytes = tensorToLittleEndian(tensorArg(norm, 0, true));
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

/// The thread count the GEMM path had for its most recent call: the pool chunks that split a
/// whole-work library product, the pool's width for a canonical call, 1 inside a region (gemm.h).
slot_t gemmThreadsKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Int64>(k::gemmThreadsUsed()));
}

/// The library's own thread count for the most recent call: 1 when a library call ran, 0 when
/// the canonical kernel did (gemm.h).
slot_t gemmLibraryThreadsKernel(ArgsView &, ArgsView &, context::Context &) {
    return camel::core::rtdata::toSlot(
        static_cast<camel::core::rtdata::Int64>(k::gemmLibraryThreadsUsed()));
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
            .params    = {{"t", ParamKind::TensorLike}},
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
                return ArrayType::numericUnknown();
            },
            .kernel = &loadNpyKernel,
            .traits = impure});
    defs.push_back(
        OpDef{
            .name      = "save_npy",
            .exports   = {"save_npy"},
            .params    = {{"t", ParamKind::TensorLike}, {"path", ParamKind::String}},
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
            .name      = "gemm_library_threads",
            .exports   = {"gemm_library_threads"},
            .params    = {},
            .resultDoc = "int",
            .infer  = [](const InferContext &) -> std::optional<Type *> { return Type::Int64(); },
            .kernel = &gemmLibraryThreadsKernel,
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
