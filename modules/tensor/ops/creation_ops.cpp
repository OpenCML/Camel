// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor creation operators. Shape arguments are int[] values; when their
 * contents are constant (export time) the result shape is known statically,
 * otherwise only the rank is unknown. Random operators and `seed` touch the
 * module RNG and are therefore not pure.
 */

#include "../interop.h"
#include "camel/core/array_convert.h"
#include "../kernels/elementwise.h"
#include "../kernels/layout.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

/// Result type of an operator whose shape is given by argument `index`.
std::optional<StaticShape> shapeFromArg(const InferContext &ctx, size_t index) {
    if (auto values = ctx.constInts(index)) {
        return StaticShape(values->begin(), values->end());
    }
    // A shape written in place with computed extents, `[n, 3]`: the rank and the constant
    // extents are known, a computed extent is not.
    if (auto partial = ctx.partialInts(index)) {
        StaticShape shape;
        for (const auto &extent : *partial) {
            shape.push_back(extent && *extent >= 0 ? std::optional<int64_t>(*extent) : std::nullopt);
        }
        return shape;
    }
    return std::nullopt;
}

/// The element type a creation operator's call settled on (`OpDef::elementFromCall`): the
/// result type its node carries.
TypeCode resultElement(ArgsView &norm) {
    const ArrayType *array = asArrayType(valueTypeOf(norm.resultType()));
    return array != nullptr && array->elemType() != nullptr ? array->elemTypeCode()
                                                            : TypeCode::Float32;
}

/// The result of an operator whose shape is argument 0 and whose element type is the call's.
InferFn shapedInferFromCall() {
    return [](const InferContext &ctx) -> std::optional<Type *> {
        if (!ctx.callElement()) {
            return std::nullopt;
        }
        return tensorOf(ctx.callElement(), shapeFromArg(ctx, 0));
    };
}

InferFn shapedInfer(std::optional<TypeCode> dtype) {
    return [dtype](const InferContext &ctx) -> std::optional<Type *> {
        return tensorOf(dtype, shapeFromArg(ctx, 0));
    };
}

slot_t zerosKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("zeros", [&] {
        return wrap(norm.resultType(),
            createTensor(resultElement(norm), shapeArg(norm, 0), resultAllocator(), true));
    });
}

slot_t onesKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("ones", [&] {
        return wrap(
            norm.resultType(),
            k::full(resultElement(norm), shapeArg(norm, 0), 1.0, resultAllocator()));
    });
}

slot_t fullKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("full", [&] {
        return wrap(norm.resultType(), k::full(
            norm.type(1)->code(),
            shapeArg(norm, 0),
            numberArg(norm, 1),
            resultAllocator()));
    });
}

std::optional<Type *> fullInfer(const InferContext &ctx) {
    return tensorOf(ctx.type(1)->code(), shapeFromArg(ctx, 0));
}

slot_t eyeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(
        "eye",
        [&] {
            Array *identity = k::eye(intArg(norm, 0), resultAllocator());
            const TypeCode element = resultElement(norm);
            if (element != identity->dtype()) {
                identity = camel::core::convertArrayElements(identity, element, resultAllocator());
            }
            return wrap(norm.resultType(), identity);
        });
}

std::optional<Type *> eyeInfer(const InferContext &ctx) {
    if (!ctx.callElement()) {
        return std::nullopt;
    }
    const std::optional<int64_t> n = ctx.constInt(0);
    return tensorOf(ctx.callElement(), StaticShape{n, n});
}

const std::vector<ParamSpec> kShapeParam = {{"shape", ParamKind::IntArray}};

} // namespace

std::vector<OpDef> creationOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name            = "zeros",
        .exports         = {"zeros"},
        .params          = kShapeParam,
        .resultDoc       = "T[..]",
        .infer           = shapedInferFromCall(),
        .kernel          = &zerosKernel,
        .traits          = {},
        .elementFromCall = true});
    defs.push_back(OpDef{
        .name            = "ones",
        .exports         = {"ones"},
        .params          = kShapeParam,
        .resultDoc       = "T[..]",
        .infer           = shapedInferFromCall(),
        .kernel          = &onesKernel,
        .traits          = {},
        .elementFromCall = true});
    defs.push_back(OpDef{
        .name      = "full",
        .exports   = {"full"},
        .params    = {{"shape", ParamKind::IntArray}, {"value", ParamKind::Number}},
        .resultDoc = "float[..]",
        .infer     = fullInfer,
        .kernel    = &fullKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name            = "eye",
        .exports         = {"eye"},
        .params          = {{"n", ParamKind::Int}},
        .resultDoc       = "T[..]",
        .infer           = eyeInfer,
        .kernel          = &eyeKernel,
        .traits          = {},
        .elementFromCall = true});
    return defs;
}

} // namespace camel::tensor::ops
