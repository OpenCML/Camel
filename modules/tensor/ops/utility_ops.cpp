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
#include <stdexcept>

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

std::optional<Type *> voidResult(const InferContext &) { return Type::Void(); }

slot_t showKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("show", [&] {
        Array *t = tensorArg(norm, 0, true);
        t->print(std::cout, nullptr);
        std::cout << std::endl;
        return NullSlot;
    });
}

/**
 * The element types a `.npy` file can hold, with the operator name each one takes
 * (`tensor:load_npy_f32`). The reader maps the five storage types the format's `descr` names
 * (`'<f4'`, `'<f8'`, `'<i4'`, `'<i8'`, `'|b1'`) to exactly these (npy.cpp): a file of another
 * element type is refused when it is read, and no call can declare one. The names are literals
 * because an `OpDef` holds a `string_view`.
 */
constexpr std::pair<TypeCode, std::string_view> kNpyElementTypes[] = {
    {TypeCode::Float32, "load_npy_f32"},
    {TypeCode::Float64, "load_npy_f64"},
    {TypeCode::Int32, "load_npy_i32"},
    {TypeCode::Int64, "load_npy_i64"},
    {TypeCode::Bool, "load_npy_bool"}};

/// What a `load_npy` call of element type `dtype` gives: a numeric array of that element type with
/// the rank left open (`f32[..]` -- the flip's terminal spelling of `Tensor<f32>`, the same
/// instance a bare `Tensor` parameter is instantiated to at the call, design principles 5.1/5.2).
/// A tensor is a numeric array, so the result is the array type the element type states rather
/// than a marker of its own; every position that takes a tensor of that element type takes it.
ArrayType *npyTensorType(TypeCode dtype) {
    switch (dtype) {
    case TypeCode::Float32:
        return ArrayType::openRank(Type::Float32());
    case TypeCode::Float64:
        return ArrayType::openRank(Type::Float64());
    case TypeCode::Int32:
        return ArrayType::openRank(Type::Int32());
    case TypeCode::Int64:
        return ArrayType::openRank(Type::Int64());
    case TypeCode::Bool:
        return ArrayType::openRank(Type::Bool());
    default:
        // The reader maps the format's five `descr` spellings to exactly the table above, so a
        // call cannot declare another element type (npy.cpp). Loud rather than a silent fallback.
        throw std::invalid_argument("a .npy file holds no other element type");
    }
}

/**
 * The element type of a `load_npy` call's result: the explicit type argument the call writes
 * (`load_npy::<f32>(p)`) or the element type of the position the call stands in (`Tensor<f32,
 * [3]>`, `f32[..]`). The result is `Tensor<dtype>`, the concrete element type, so every position
 * that takes a tensor takes the load; the kernel checks the file's own element type against it
 * and a mismatch is a fault at the call (design principles 5.5; handover H466). One resolver per
 * element type a `.npy` file can hold, and none of them accepts a call that names no element type
 * and stands where none is expected.
 */
class LoadNpyResolver final : public FuncTypeResolver {
  public:
    explicit LoadNpyResolver(TypeCode dtype) : dtype_(dtype) {}

    std::optional<FunctionType *>
    resolve(const type_vec_t &, const type_vec_t &, const ModifierSet &) const override {
        return std::nullopt;
    }

    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types,
        Type *expected,
        const type_vec_t &with,
        const type_vec_t &norm,
        static_args_t,
        const ModifierSet &modifiers) const override {
        if (!with.empty() || norm.size() != 1 || norm[0]->code() != TypeCode::String) {
            return std::nullopt;
        }
        std::optional<TypeCode> requested;
        if (types.size() == 1) {
            requested = types[0]->code();
        } else if (types.empty()) {
            if (const auto target = tensorFactsOf(expected); target.dtype) {
                requested = target.dtype;
            }
        }
        if (!requested || *requested != dtype_) {
            return std::nullopt;
        }
        return FunctionType::create({}, {{Type::String(), false}}, npyTensorType(dtype_), modifiers);
    }

    /// The candidate line a failed resolution prints (`NoMatchingFunction`): the element type the
    /// call would have to name, and the result it gives.
    std::string signature() const override {
        return "::<" + std::string(dtypeName(dtype_)) + ">(path: string) => Tensor<" +
               std::string(dtypeName(dtype_)) + ">";
    }

  private:
    TypeCode dtype_;
};

slot_t loadNpyKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("load_npy", [&] {
// The element type the call declares (`load_npy::<f32>(p)`, or the element type of the
        // position the call stands in): the file's dtype must be that one. The scheduler puts the
        // call's own type here, so a load inside a generic function checks the instance's type,
        // not the declaration's `T` (design principles 5.5; handover H466). The value carries the
        // declared type too, which is what `wrap` puts on it (the element type follows the value).
        const TensorFacts declared = tensorFactsOf(norm.resultType());
        return wrap(
            norm.resultType(),
            loadNpy(
                camel::utils::pathFromUtf8(stringArg(norm, 0)), resultAllocator(),
                declared.dtype));
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
            .name      = "show",
            .exports   = {"show"},
            .params    = {{"t", ParamKind::TensorLike}},
            .resultDoc = "void",
            .infer     = voidResult,
            .kernel    = &showKernel,
            .traits    = impure});
// One operator per element type a `.npy` file can hold. `load_npy`'s element type is the one
    // the call declares, and no parameter can mention a type variable that is the *result's*
    // element type, so the signature is computed rather than declared: the entry in
    // `stdlib/signatures/tensor.cml` is the reference, `LoadNpyResolver` is the source, and the
    // group below is what a call resolves against (handover H466; design principles 6.4).
    for (const auto &[dtype, name] : kNpyElementTypes) {
        defs.push_back(
            OpDef{
                .name      = name,
                .exports   = {},
                .params    = {{"path", ParamKind::String}},
                .resultDoc = "Tensor",
                .infer     = [dtype](const InferContext &) -> std::optional<Type *> {
                    return npyTensorType(dtype);
                },
                .kernel = &loadNpyKernel,
                .traits = impure});
    }
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

oper_group_ptr_t loadNpyOperatorGroup() {
    std::vector<std::pair<std::string, type::resolver_ptr_t>> rules;
    for (const auto &[dtype, name] : kNpyElementTypes) {
        rules.emplace_back(
            std::string("tensor:") + std::string(name), std::make_shared<LoadNpyResolver>(dtype));
    }
    return OperatorGroup::create("load_npy", rules);
}

} // namespace camel::tensor::ops
