// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A type argument or expected Tensor dtype selects one concrete kernel; no type value is run.

#include "binary_ops.h"
#include "../binary.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/resolver.h"
#include "support.h"

namespace camel::tensor::ops {
using namespace core::type;
namespace {
Type *resultType(TypeCode dtype, const InferContext &ctx) {
    std::optional<StaticShape> shape;
    if (auto values = ctx.constInts(1)) {
        for (const auto extent : *values) {
            if (extent < 0)
                throw ShapeError("binary tensor shape cannot contain negative extents");
        }
        shape = *values;
    } else if (auto values = ctx.partialInts(1)) {
        shape = StaticShape{};
        for (const auto &value : *values) {
            if (value && *value < 0)
                throw ShapeError("binary tensor shape cannot contain negative extents");
            shape->push_back(value.value_or(kUnknownDim));
        }
    }
    return TensorType::get(dtype, shape);
}

template <TypeCode DType> slot_t fromBytesKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("from_bytes", [&] {
        const auto shape = shapeArg(norm, 1);
        return wrap(tensorFromLittleEndian(
            norm.get<core::rtdata::BytesObject *>(0)->view(),
            DType,
            shape,
            resultAllocator()));
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
        std::optional<TypeCode> requested;
        if (types.size() == 1)
            requested = types[0]->code();
        else if (types.empty()) {
            if (const auto target = asTensorType(expected))
                requested = target->dtype();
        }
        if (!requested || *requested != dtype_)
            return std::nullopt;
        const StaticFacts facts(norm, statics);
        try {
            Type *result = resultType(dtype_, InferContext(norm, facts.constants, facts.partials));
            return FunctionType::create(
                {},
                {{norm[0], false}, {norm[1], false}},
                result,
                modifiers);
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
        return "::<" + std::string(dtypeName(dtype_)) + ">(bytes: Bytes, shape: int[]) => Tensor";
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
                .resultDoc = "Tensor",
                .infer     = [dtype](const InferContext &ctx) -> std::optional<Type *> {
                    return resultType(dtype, ctx);
                },
                .kernel = kernel,
                .traits = {}});
    };
    add("from_bytes_f32", TypeCode::Float32, fromBytesKernel<TypeCode::Float32>);
    add("from_bytes_i64", TypeCode::Int64, fromBytesKernel<TypeCode::Int64>);
    add("from_bytes_bool", TypeCode::Bool, fromBytesKernel<TypeCode::Bool>);
    return defs;
}

oper_group_ptr_t fromBytesOperatorGroup() {
    return OperatorGroup::create(
        "from_bytes",
        {
            {"tensor:from_bytes_f32", std::make_shared<FromBytesResolver>(TypeCode::Float32)},
            {"tensor:from_bytes_i64", std::make_shared<FromBytesResolver>(TypeCode::Int64)},
            {"tensor:from_bytes_bool", std::make_shared<FromBytesResolver>(TypeCode::Bool)},
        });
}
} // namespace camel::tensor::ops
