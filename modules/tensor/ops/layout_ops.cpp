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
 * Shape and data-movement operators: shape, reshape, flatten, unsqueeze,
 * transpose, permute, concat, slice.
 *
 * Static inference computes result shapes with the same helpers the kernels
 * use (layout.h) whenever every input fact is known, and degrades to unknown
 * extents otherwise.
 */

#include "../interop.h"
#include "../kernels/layout.h"
#include "camel/core/type/composite/array.h"
#include "catalog.h"
#include "support.h"

#include <format>
#include <array>

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

bool fullyKnown(const std::optional<StaticShape> &shape) {
    if (!shape) {
        return false;
    }
    for (int64_t extent : *shape) {
        if (extent == kUnknownDim) {
            return false;
        }
    }
    return true;
}

int64_t normalizeAxisOrThrow(int64_t axis, size_t rank) {
    const auto r = static_cast<int64_t>(rank);
    if (axis < 0) {
        axis += r;
    }
    if (axis < 0 || axis >= r) {
        throw ShapeError(
            std::format("axis {} is out of range for a rank-{} tensor", axis < 0 ? axis - r : axis, rank));
    }
    return axis;
}

// shape(t) -> int[]
slot_t shapeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("shape", [&] {
        return camel::core::rtdata::toSlot(makeShapeArray(tensorArg(norm, 0), resultAllocator()));
    });
}

std::optional<Type *> shapeInfer(const InferContext &) {
    static ArrayType *intArray = ArrayType::create(Type::Int64());
    return intArray;
}

/// The shape as a constant when the type fixes every dimension.
std::optional<slot_t> shapeFold(const InferContext &ctx, mm::IAllocator &allocator) {
    const auto shape = ctx.facts(0).shape;
    if (!shape || std::ranges::any_of(*shape, [](int64_t d) { return d == kUnknownDim; })) {
        return std::nullopt;
    }
    ::Array *array = ::Array::create(allocator, shape->size());
    for (size_t i = 0; i < shape->size(); ++i) {
        array->set(i, static_cast<camel::core::rtdata::Int64>((*shape)[i]));
    }
    return camel::core::rtdata::toSlot(array);
}

// reshape(t | number[], shape: int[])
slot_t reshapeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("reshape", [&] {
        const std::vector<int64_t> target = parseIntArray(norm.get<::Array *>(1), norm.type(1));
        return wrap(k::reshape(tensorArg(norm, 0, true), target, resultAllocator()));
    });
}

std::optional<Type *> reshapeInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    auto target          = ctx.constInts(1);
    if (!target) {
        return tensorOf(in.dtype, std::nullopt);
    }
    if (fullyKnown(in.shape)) {
        return tensorOf(in.dtype, k::resolveReshape(*in.shape, *target));
    }
    StaticShape shape(target->begin(), target->end());
    return tensorOf(in.dtype, shape); // a remaining -1 stays unknown
}

// flatten(t, axis = 1): [d0 * ... * d(axis-1), d(axis) * ... * dn]
Shape flattenShape(std::span<const int64_t> shape, int64_t axis) {
    const auto a = static_cast<size_t>(axis < 0 ? axis + static_cast<int64_t>(shape.size()) : axis);
    int64_t outer = 1, inner = 1;
    for (size_t d = 0; d < shape.size(); ++d) {
        (d < a ? outer : inner) *= shape[d];
    }
    return {outer, inner};
}

slot_t flattenKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("flatten", [&] {
        TensorObject *t    = tensorArg(norm, 0);
        const int64_t axis = hasArg(norm, 1) ? intArg(norm, 1) : 1;
        if (axis < -static_cast<int64_t>(t->rank()) || axis > static_cast<int64_t>(t->rank())) {
            throw std::invalid_argument("flatten axis out of range");
        }
        return wrap(k::reshape(t, flattenShape(t->shapeSpan(), axis), resultAllocator()));
    });
}

std::optional<Type *> flattenInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    const auto axis      = ctx.has(1) ? ctx.constInt(1) : std::optional<int64_t>(1);
    if (fullyKnown(in.shape) && axis) {
        return tensorOf(in.dtype, flattenShape(*in.shape, *axis));
    }
    return tensorOf(in.dtype, StaticShape{kUnknownDim, kUnknownDim});
}

// unsqueeze(t, axis)
slot_t unsqueezeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("unsqueeze", [&] {
        TensorObject *t = tensorArg(norm, 0);
        Shape shape     = t->shapeVector();
        const int64_t a = normalizeAxisOrThrow(intArg(norm, 1), shape.size() + 1);
        shape.insert(shape.begin() + a, 1);
        return wrap(k::reshape(t, shape, resultAllocator()));
    });
}

std::optional<Type *> unsqueezeInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    const auto axis      = ctx.constInt(1);
    if (!in.shape) {
        return tensorOf(in.dtype, std::nullopt);
    }
    StaticShape shape = *in.shape;
    if (!axis) {
        return tensorOf(in.dtype, StaticShape(shape.size() + 1, kUnknownDim));
    }
    shape.insert(shape.begin() + normalizeAxisOrThrow(*axis, shape.size() + 1), 1);
    return tensorOf(in.dtype, shape);
}

// transpose(t): swap the last two axes
slot_t transposeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("transpose", [&] {
        return wrap(k::transposeLast2(tensorArg(norm, 0), resultAllocator()));
    });
}

std::optional<Type *> transposeInfer(const InferContext &ctx) {
    TensorFacts in = ctx.facts(0);
    if (in.shape) {
        if (in.shape->size() < 2) {
            throw ShapeError(std::format(
                "transpose swaps the last two axes and needs a rank >= 2 tensor, got {}",
                formatShape(*in.shape)));
        }
        std::swap((*in.shape)[in.shape->size() - 1], (*in.shape)[in.shape->size() - 2]);
    }
    return tensorOf(in);
}

// permute(t, perm: int[])
slot_t permuteKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("permute", [&] {
        const std::vector<int64_t> perm = parseIntArray(norm.get<::Array *>(1), norm.type(1));
        return wrap(k::permute(tensorArg(norm, 0), perm, resultAllocator()));
    });
}

std::optional<Type *> permuteInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    auto perm            = ctx.constInts(1);
    if (in.shape && perm) {
        return tensorOf(in.dtype, k::permuteShape(*in.shape, *perm));
    }
    return tensorOf(
        in.dtype,
        in.shape ? std::optional(StaticShape(in.shape->size(), kUnknownDim)) : std::nullopt);
}

// concat(a, b, axis)
slot_t concatKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("concat", [&] {
        const TensorObject *parts[] = {tensorArg(norm, 0, true), tensorArg(norm, 1, true)};
        return wrap(k::concat(parts, intArg(norm, 2), resultAllocator()));
    });
}

std::optional<Type *> concatInfer(const InferContext &ctx) {
    const TensorFacts a = ctx.facts(0), b = ctx.facts(1);
    const auto axis  = ctx.constInt(2);
    const auto dtype = a.dtype == b.dtype ? a.dtype : std::nullopt;
    if (!a.shape || !b.shape || !axis) {
        return tensorOf(
            dtype,
            a.shape ? std::optional(StaticShape(a.shape->size(), kUnknownDim)) : std::nullopt);
    }
    if (a.shape->size() != b.shape->size()) {
        throw ShapeError(std::format(
            "concat requires tensors of the same rank: {} and {}",
            formatShape(*a.shape),
            formatShape(*b.shape)));
    }
    const auto ax = static_cast<size_t>(normalizeAxisOrThrow(*axis, a.shape->size()));
    StaticShape out(a.shape->size());
    for (size_t d = 0; d < out.size(); ++d) {
        const int64_t x = (*a.shape)[d], y = (*b.shape)[d];
        if (d == ax) {
            out[d] = (x == kUnknownDim || y == kUnknownDim) ? kUnknownDim : x + y;
        } else if (x != kUnknownDim && y != kUnknownDim && x != y) {
            throw ShapeError(std::format(
                "concat along axis {}: {} and {} differ in dimension {}",
                ax,
                formatShape(*a.shape),
                formatShape(*b.shape),
                d));
        } else {
            out[d] = x != kUnknownDim ? x : y;
        }
    }
    return tensorOf(dtype, out);
}

// slice(t, axis, start, end, step = 1)
slot_t sliceKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("slice", [&] {
        const int64_t step = hasArg(norm, 4) ? intArg(norm, 4) : 1;
        return wrap(k::slice(
            tensorArg(norm, 0),
            intArg(norm, 1),
            intArg(norm, 2),
            intArg(norm, 3),
            step,
            resultAllocator()));
    });
}

std::optional<Type *> sliceInfer(const InferContext &ctx) {
    TensorFacts in  = ctx.facts(0);
    const auto axis = ctx.constInt(1);
    if (!in.shape) {
        return tensorOf(in);
    }
    if (!axis) {
        return tensorOf(in.dtype, StaticShape(in.shape->size(), kUnknownDim));
    }
    const auto a     = static_cast<size_t>(normalizeAxisOrThrow(*axis, in.shape->size()));
    const auto start = ctx.constInt(2), end = ctx.constInt(3);
    const auto step   = ctx.has(4) ? ctx.constInt(4) : std::optional<int64_t>(1);
    const int64_t ext = (*in.shape)[a];
    if (ext == kUnknownDim || !start || !end || !step || *step <= 0) {
        (*in.shape)[a] = kUnknownDim;
        return tensorOf(in);
    }
    auto clamp      = [ext](int64_t v) { return std::clamp<int64_t>(v < 0 ? v + ext : v, 0, ext); };
    const int64_t s = clamp(*start), e = clamp(*end);
    (*in.shape)[a] = e > s ? (e - s + *step - 1) / *step : 0;
    return tensorOf(in);
}

// ---------------------------------------------------------------- VJP rules

// transpose swaps the last two axes, so it is its own inverse.
void transposeVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], addTensorOper(b, "tensor:transpose", {*dy}));
    }
}

/// reshape, flatten, unsqueeze: the gradient takes the input's shape back.
void reshapeLikeVjp(VjpBuilder &b, const VjpCall &call) {
    if (auto dy = b.gradientOf(call.output); dy && isTensorNode(b, call.inputs[0])) {
        const vjp_node_t shape = b.addOper(
            ArrayType::create(Type::Int64()),
            "tensor:shape",
            std::span<const vjp_node_t>(call.inputs.data(), 1));
        b.accumulateGradient(call.inputs[0], addTensorOper(b, "tensor:reshape", {*dy, shape}));
    }
}

void permuteVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:permute_inverse", {*dy, call.inputs[1]}));
    }
}

// concat(a, b, axis): each part receives its slice of dy.
void concatVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 3);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1], axis = call.inputs[2];
    if (!isTensorNode(b, lhs) || !isTensorNode(b, rhs)) {
        return; // numeric array parts take no gradient
    }
    const std::array<vjp_node_t, 2> lhsAxis{lhs, axis};
    const vjp_node_t split = b.addOper(Type::Int64(), "tensor:dim", lhsAxis);
    const std::array<vjp_node_t, 2> dyAxis{*dy, axis};
    const vjp_node_t total = b.addOper(Type::Int64(), "tensor:dim", dyAxis);
    b.accumulateGradient(
        lhs,
        addTensorOper(b, "tensor:slice", {*dy, axis, staticInt(b, 0), split}));
    b.accumulateGradient(rhs, addTensorOper(b, "tensor:slice", {*dy, axis, split, total}));
}

// slice(t, axis, start, end, step?)
void sliceVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 4, 5);
    if (auto dy = b.gradientOf(call.output)) {
        std::vector<vjp_node_t> inputs{*dy};
        inputs.insert(inputs.end(), call.inputs.begin(), call.inputs.end());
        b.accumulateGradient(
            call.inputs[0],
            b.addOper(vjpTensorType(), "tensor:slice_grad", inputs));
    }
}

} // namespace

std::vector<OpDef> layoutOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "shape",
        .exports   = {"shape"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "int[]",
        .infer         = shapeInfer,
        .kernel        = &shapeKernel,
        .traits        = {},
        .foldFromTypes = shapeFold});
    defs.push_back(OpDef{
        .name      = "reshape",
        .exports   = {"reshape"},
        .params    = {{"t", ParamKind::TensorLike}, {"shape", ParamKind::IntArray}},
        .resultDoc = "Tensor",
        .infer     = reshapeInfer,
        .kernel    = &reshapeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "flatten",
        .exports   = {"flatten"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int, true}},
        .resultDoc = "Tensor",
        .infer     = flattenInfer,
        .kernel    = &flattenKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "unsqueeze",
        .exports   = {"unsqueeze"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int}},
        .resultDoc = "Tensor",
        .infer     = unsqueezeInfer,
        .kernel    = &unsqueezeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "transpose",
        .exports   = {"transpose"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "Tensor",
        .infer     = transposeInfer,
        .kernel    = &transposeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "permute",
        .exports   = {"permute"},
        .params    = {{"t", ParamKind::Tensor}, {"perm", ParamKind::IntArray}},
        .resultDoc = "Tensor",
        .infer     = permuteInfer,
        .kernel    = &permuteKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "concat",
        .exports = {"concat"},
        .params =
            {{"a", ParamKind::TensorLike}, {"b", ParamKind::TensorLike}, {"axis", ParamKind::Int}},
        .resultDoc = "Tensor",
        .infer     = concatInfer,
        .kernel    = &concatKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "slice",
        .exports = {"slice"},
        .params =
            {{"t", ParamKind::Tensor},
             {"axis", ParamKind::Int},
             {"start", ParamKind::Int},
             {"end", ParamKind::Int},
             {"step", ParamKind::Int, true}},
        .resultDoc = "Tensor",
        .infer     = sliceInfer,
        .kernel    = &sliceKernel,
        .traits    = {}});
    setVjp(defs, "transpose", &transposeVjp);
    setVjp(defs, "reshape", &reshapeLikeVjp);
    setVjp(defs, "flatten", &reshapeLikeVjp);
    setVjp(defs, "unsqueeze", &reshapeLikeVjp);
    setVjp(defs, "permute", &permuteVjp);
    setVjp(defs, "concat", &concatVjp);
    setVjp(defs, "slice", &sliceVjp);
    setVjp(defs, "shape", &camel::core::noGradient);
    return defs;
}

} // namespace camel::tensor::ops
