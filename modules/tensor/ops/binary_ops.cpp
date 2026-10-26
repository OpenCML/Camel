// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A type argument or expected Tensor dtype selects one concrete kernel; no type value is run.

#include "binary_ops.h"
#include "../binary.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/resolver.h"
#include "support.h"

#include <iostream>

namespace camel::tensor::ops {
using namespace core::type;
namespace {
/// The result of a call whose element type is an aggregate: the array the call names, `P[..]`.
/// The element type is the call's own (a type variable that only the result mentions), so the
/// call writes it and the array is built here rather than computed from a dtype.
Type *aggregateResultType(Type *requested) { return ArrayType::openRank(requested); }

Type *resultType(TypeCode dtype, const InferContext &ctx) {
    std::optional<StaticShape> shape;
    if (auto values = ctx.constInts(1)) {
        for (const auto extent : *values) {
            if (extent < 0)
                throw ShapeError("binary tensor shape cannot contain negative extents");
        }
        shape = knownShape(*values);
    } else if (auto values = ctx.partialInts(1)) {
        shape = StaticShape{};
        for (const auto &value : *values) {
            if (value && *value < 0)
                throw ShapeError("binary tensor shape cannot contain negative extents");
            shape->push_back(value);
        }
    }
    return tensorOf(dtype, shape);
}

template <TypeCode DType> slot_t fromBytesKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("from_bytes", [&] {
        const auto shape = shapeArg(norm, 1);
        return wrap(norm.resultType(), tensorFromLittleEndian(
            norm.get<core::rtdata::BytesObject *>(0)->view(),
            DType,
            shape,
            resultAllocator()));
    });
}

/// The same read for an element type that is not one scalar the tensor module stores but a
/// compact aggregate of pure data: the result's own element type is the one to fill (the call
/// names it, `from_bytes::<P>(b, [2])`), and its layout says where each field's bytes go.
slot_t fromBytesAggregateKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("from_bytes", [&] {
        const auto *arrayType = asArrayType(norm.resultType());
        ASSERT(arrayType != nullptr, "a sequence read declares the array it gives");
        const auto shape = shapeArg(norm, 1);
        Array *result    = plainSequenceFromLittleEndian(
            norm.get<core::rtdata::BytesObject *>(0)->view(), arrayType, shape, resultAllocator());
        return wrap(norm.resultType(), result);
    });
}

class FromBytesResolver final : public FuncTypeResolver {
  public:
    explicit FromBytesResolver(TypeCode dtype) : dtype_(dtype) {}
    std::optional<FunctionType *>
    resolve(const type_vec_t &, const type_vec_t &, const ModifierSet &) const override {
        return std::nullopt;
    }
    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expected, const type_vec_t &with, const type_vec_t &norm,
        static_args_t statics, const ModifierSet &modifiers) const override {
        if (!with.empty() || norm.size() != 2 || !BytesType::get()->equals(norm[0]) ||
            norm[1]->code() != TypeCode::Array)
            return std::nullopt;
        const auto elem = static_cast<ArrayType *>(norm[1])->elemType()->code();
        if (elem != TypeCode::Int64 && elem != TypeCode::Int32)
            return std::nullopt;
        // An element type that is an aggregate of pure data is not one the tensor module stores,
        // so the call names it rather than declaring it by dtype (`from_bytes::<P>(b, [2])`). The
        // aggregate entry is the one that answers for it: the dtype entries keep to their own
        // element type, so a call has one candidate and not one per entry.
        if (dtype_ == TypeCode::Void && types.size() == 1) {
            const DataLayout *layout = pureDataLayout(types[0]);
            if (layout != nullptr && layout->aggregate) {
                return FunctionType::create(
                    {},
                    {{norm[0], false}, {norm[1], false}},
                    aggregateResultType(types[0]));
            }
        }
        std::optional<TypeCode> requested;
        if (types.size() == 1)
            requested = types[0]->code();
        else if (types.empty()) {
            if (const auto target = tensorFactsOf(expected); target.dtype)
                requested = target.dtype;
        }
        if (!requested || *requested != dtype_)
            return std::nullopt;
        const StaticFacts facts(norm, statics);
        try {
            Type *result = resultType(dtype_, InferContext(norm, facts.constants, facts.partials));
            return FunctionType::create(
                {},
                {{norm[0], false}, {norm[1], false}},
                result);
        } catch (const std::invalid_argument &) {
            return std::nullopt;
        }
    }
    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, static_args_t statics,
        const ModifierSet &) const override {
        if (!with.empty() || norm.size() != 2 || !BytesType::get()->equals(norm[0]))
            return std::nullopt;
        try {
            const StaticFacts facts(norm, statics);
            (void)resultType(dtype_, InferContext(norm, facts.constants, facts.partials));
        } catch (const std::invalid_argument &error) {
            return std::string(error.what());
        }
        return std::nullopt;
    }
    std::string signature() const override {
        // The aggregate arm carries no dtype: its element type is the one the call names, so the
        // signature says that rather than a letter standing in for it.
        if (dtype_ == TypeCode::Void) {
            return "::<T>(bytes: Bytes, shape: int[]) => T[..]";
        }
        return "::<" + std::string(dtypeName(dtype_)) + ">(bytes: Bytes, shape: int[]) => " + std::string(dtypeName(dtype_)) + "[..]";
    }

  private:
    TypeCode dtype_;
};
} // namespace

std::vector<OpDef> binaryOps() {
    std::vector<OpDef> defs;
    const auto add = [&](std::string_view name, TypeCode dtype, operator_t kernel) {
        defs.push_back(
            OpDef{
                .name      = name,
                .exports   = {},
                .params    = {{"bytes", ParamKind::Bytes}, {"shape", ParamKind::IntArray}},
                .resultDoc = "float[..]",
                .infer     = [dtype](const InferContext &ctx) -> std::optional<Type *> {
                    return resultType(dtype, ctx);
                },
                .kernel = kernel,
                .traits = {}});
    };
    add("from_bytes_f32", TypeCode::Float32, fromBytesKernel<TypeCode::Float32>);
    add("from_bytes_f64", TypeCode::Float64, fromBytesKernel<TypeCode::Float64>);
    add("from_bytes_i32", TypeCode::Int32, fromBytesKernel<TypeCode::Int32>);
    add("from_bytes_i64", TypeCode::Int64, fromBytesKernel<TypeCode::Int64>);
    add("from_bytes_bool", TypeCode::Bool, fromBytesKernel<TypeCode::Bool>);
    // The reduced-precision element types are read from their stored bits, exactly as written
    // by `to_bytes`: no conversion, no rounding, so no change_precision permission. The value
    // they hold was rounded when it was written (the `cast` that produced it).
    add("from_bytes_bf16", TypeCode::BFloat16, fromBytesKernel<TypeCode::BFloat16>);
    add("from_bytes_f16", TypeCode::Float16, fromBytesKernel<TypeCode::Float16>);
    // The read of an element type that is a compact aggregate of pure data. Its element type is
    // the call's own, and a declaration's parameters cannot mention a type variable that only
    // the result mentions, so the signature is computed rather than declared: the group entry
    // below is what a call resolves against and the kernel takes the result type it fixed.
    defs.push_back(
        OpDef{
            .name      = "from_bytes_aggregate",
            .exports   = {},
            .params    = {{"bytes", ParamKind::Bytes}, {"shape", ParamKind::IntArray}},
            .resultDoc = "void[..]",
            .kernel    = &fromBytesAggregateKernel,
            .traits    = {}});
    return defs;
}

oper_group_ptr_t fromBytesOperatorGroup() {
    return OperatorGroup::create(
        "from_bytes",
        {
            {"tensor:from_bytes_f32", std::make_shared<FromBytesResolver>(TypeCode::Float32)},
            {"tensor:from_bytes_f64", std::make_shared<FromBytesResolver>(TypeCode::Float64)},
            {"tensor:from_bytes_i32", std::make_shared<FromBytesResolver>(TypeCode::Int32)},
            {"tensor:from_bytes_i64", std::make_shared<FromBytesResolver>(TypeCode::Int64)},
            {"tensor:from_bytes_bool", std::make_shared<FromBytesResolver>(TypeCode::Bool)},
            {"tensor:from_bytes_bf16", std::make_shared<FromBytesResolver>(TypeCode::BFloat16)},
            {"tensor:from_bytes_f16", std::make_shared<FromBytesResolver>(TypeCode::Float16)},
            // The aggregate read comes first: it is the one this resolver answers for an element
            // type that is a struct or a tuple, and the dtype entries refuse those in any order.
            {"tensor:from_bytes_aggregate",
             std::make_shared<FromBytesResolver>(TypeCode::Void)},
        });
}
} // namespace camel::tensor::ops
