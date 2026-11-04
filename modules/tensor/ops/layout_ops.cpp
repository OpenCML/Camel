// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shape and data-movement operators: shape, reshape, unsqueeze,
 * transpose, permute, concat.
 *
 * Static inference computes result shapes with the same helpers the kernels
 * use (layout.h) whenever every input fact is known, and degrades to unknown
 * extents otherwise.
 */

#include "../interop.h"
#include "../kernels/layout.h"
#include "camel/core/error/runtime.h"
#include "camel/core/type/composite/array.h"
#include "catalog.h"
#include "support.h"

#include <format>
#include <array>
#include <span>
#include <vector>

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;

namespace {

bool fullyKnown(const std::optional<StaticShape> &shape) {
    if (!shape) {
        return false;
    }
    for (const std::optional<int64_t> &extent : *shape) {
        if (!extent) {
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
        return camel::core::rtdata::toSlot(makeShapeArray(tensorArg(norm, 0, true), resultAllocator()));
    });
}

std::optional<Type *> shapeInfer(const InferContext &) {
    static ArrayType *intArray = ArrayType::create(Type::Int64());
    return intArray;
}

/// The shape as a constant when the type fixes every dimension.
std::optional<slot_t> shapeFold(const InferContext &ctx, mm::IAllocator &allocator) {
    const auto shape = ctx.facts(0).shape;
    if (!shape || std::ranges::any_of(*shape, [](const std::optional<int64_t> &d) { return !d; })) {
        return std::nullopt;
    }
    ::Array *array =
        ::Array::create(allocator, ArrayType::create(Type::Int64()), shape->size());
    for (size_t i = 0; i < shape->size(); ++i) {
        array->set(i, static_cast<camel::core::rtdata::Int64>(*(*shape)[i]));
    }
    return camel::core::rtdata::toSlot(array);
}

// reshape(t | number[], shape: int[])
slot_t reshapeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("reshape", [&] {
        const std::vector<int64_t> target = parseIntArray(norm.get<::Array *>(1), norm.type(1));
        Array *t = tensorArg(norm, 0, true);
        if (target.empty()) {
            // No dimension at all: the value with no dimension is the element it holds, and `resolveReshape` refuses a value that is not one element.
            (void)k::resolveReshape(t->shapeSpan(), target);
            return t->getSlot(0);
        }
        return wrap(norm.resultType(), k::reshape(t, target, resultAllocator()));
    });
}

std::optional<Type *> reshapeInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    auto target          = ctx.constInts(1);
    if (!target) {
        // A target written in place with computed extents, `[n, 3]`: the rank and the constant
        // extents are the result's. A computed extent (and -1) is not known, unless it is the
        // only one and the input's element count leaves one value for it: any other value the
        // program computes makes reshape fail when it runs.
        const auto partial = ctx.partialInts(1);
        if (!partial) {
            return tensorOf(in.dtype, std::nullopt);
        }
        StaticShape shape;
        std::vector<int64_t> probe;
        size_t open = 0;
        for (const auto &extent : *partial) {
            const bool known = extent && *extent >= 0;
            shape.push_back(known ? std::optional<int64_t>(*extent) : std::nullopt);
            probe.push_back(known ? *extent : -1);
            open += known ? 0 : 1;
        }
        if (open == 1 && fullyKnown(in.shape)) {
            try {
                return tensorOf(
                    in.dtype, knownShape(k::resolveReshape(kernelExtents(*in.shape), probe)));
            } catch (const std::exception &) {
                // No value fits: left to reshape to report when it runs.
            }
        }
        return tensorOf(in.dtype, shape);
    }
    if (fullyKnown(in.shape)) {
        return tensorOf(
            in.dtype, knownShape(k::resolveReshape(kernelExtents(*in.shape), *target)));
    }
    StaticShape shape;
    shape.reserve(target->size());
    for (int64_t extent : *target) {
        // A `-1` in a reshape target is that operator's "fill this extent in" (kernels::layout),
        // which no value can be: the facts carry it as an unknown extent (plan section 5.3).
        shape.push_back(extent < 0 ? std::nullopt : std::optional<int64_t>(extent));
    }
    return tensorOf(in.dtype, std::move(shape));
}

// unsqueeze(t, axis)
slot_t unsqueezeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("unsqueeze", [&] {
        Array *t = tensorArg(norm, 0);
        Shape shape     = t->shapeVector();
        const int64_t a = normalizeAxisOrThrow(intArg(norm, 1), shape.size() + 1);
        shape.insert(shape.begin() + a, 1);
        return wrap(norm.resultType(), k::reshape(t, shape, resultAllocator()));
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
        return tensorOf(in.dtype, StaticShape(shape.size() + 1, std::nullopt));
    }
    shape.insert(shape.begin() + normalizeAxisOrThrow(*axis, shape.size() + 1), 1);
    return tensorOf(in.dtype, shape);
}

// transpose(t): swap the last two axes
slot_t transposeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("transpose", [&] {
        return wrap(norm.resultType(), k::transposeLast2(tensorArg(norm, 0), resultAllocator()));
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
        return wrap(norm.resultType(), k::permute(tensorArg(norm, 0), perm, resultAllocator()));
    });
}

std::optional<Type *> permuteInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    auto perm            = ctx.constInts(1);
    if (in.shape && perm) {
        return tensorOf(
            in.dtype, knownShape(k::permuteShape(kernelExtents(*in.shape), *perm)));
    }
    return tensorOf(
        in.dtype,
        in.shape ? std::optional(StaticShape(in.shape->size(), std::nullopt)) : std::nullopt);
}

// concat(a, b, axis)
slot_t concatKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("concat", [&] {
        const Array *parts[] = {tensorArg(norm, 0, true), tensorArg(norm, 1, true)};
        return wrap(norm.resultType(), k::concat(parts, intArg(norm, 2), resultAllocator()));
    });
}

std::optional<Type *> concatInfer(const InferContext &ctx) {
    const TensorFacts a = ctx.facts(0), b = ctx.facts(1);
    const auto axis  = ctx.constInt(2);
    const auto dtype = a.dtype == b.dtype ? a.dtype : std::nullopt;
    if (!a.shape || !b.shape || !axis) {
        return tensorOf(
            dtype,
            a.shape ? std::optional(StaticShape(a.shape->size(), std::nullopt)) : std::nullopt);
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
        const std::optional<int64_t> x = (*a.shape)[d], y = (*b.shape)[d];
        if (d == ax) {
            out[d] = (!x || !y) ? std::nullopt : std::optional<int64_t>(*x + *y);
        } else if (x && y && x != y) {
            throw ShapeError(std::format(
                "concat along axis {}: {} and {} differ in dimension {}",
                ax,
                formatShape(*a.shape),
                formatShape(*b.shape),
                d));
        } else {
            out[d] = x ? x : y;
        }
    }
    return tensorOf(dtype, out);
}

// ---------------------------------------------------------------- take / one_hot

// The slice has one owner: the core's own node `:op/slice_sub`, which answers `t[a:b:s]` and the
// function form `slice(t, axis, a, b, s)` alike, for any rank and any element type, with one
// adjoint rule. This module's own slice kernel and type rule are gone with it (design principles
// 5.4).

// take(t, indices): rows along the first axis, in the order the indices give.
slot_t takeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *input = tensorArg(norm, 0);
    if (input->rank() < 1) {
        reportShapeError("take", "expects a tensor with at least one dimension");
    }
    const std::vector<int64_t> indices = intListArg(norm, 1, "take");
    const int64_t extent               = input->dim(0);
    for (const int64_t index : indices) {
        if (index < 0 || index >= extent) {
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                index,
                std::string("the first dimension of the input tensor"),
                extent);
        }
    }
    return runKernel("take", [&] {
        return wrap(norm.resultType(), k::take(input, indices, resultAllocator()));
    });
}

std::optional<Type *> takeInfer(const InferContext &ctx) {
    const TensorFacts input   = ctx.facts(0);
    const TensorFacts indices = ctx.facts(1);
    if (!isIntegerCode(indices.dtype)) {
        throw ShapeError("take indices must be an integer array or a rank-1 integer tensor");
    }
    if (input.shape && input.shape->empty()) {
        throw ShapeError("take expects a tensor with at least one dimension");
    }
    if (indices.shape && indices.shape->size() != 1) {
        throw ShapeError("take indices must be an int array or a rank-1 integer tensor");
    }

    StaticShape out;
    const std::optional<int64_t> extent = input.shape ? (*input.shape)[0] : std::nullopt;
    if (const auto constant = ctx.constInts(1)) {
        if (extent.has_value()) {
            for (const int64_t index : *constant) {
                if (index < 0 || index >= *extent) {
                    throw ShapeError(std::format(
                        "index {} is out of range for the first dimension of extent {}",
                        index,
                        *extent));
                }
            }
        }
        out.push_back(static_cast<int64_t>(constant->size()));
    } else if (indices.shape) {
        out.push_back((*indices.shape)[0]);
    } else {
        out.push_back(std::nullopt);
    }
    if (input.shape) {
        out.insert(out.end(), input.shape->begin() + 1, input.shape->end());
    } else {
        return tensorOf(input.dtype, std::nullopt);
    }
    return tensorOf(input.dtype, out);
}

// `t[idx]` on a tensor: the rows the indices name, in that order, which is take (design
// principles 5.4). Same kernel and shape rule as take; only the name a program writes differs.
slot_t subscriptTakeKernel(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    return takeKernel(with, norm, ctx);
}

std::optional<Type *> subscriptTakeInfer(const InferContext &ctx) {
    try {
        return takeInfer(ctx);
    } catch (const ShapeError &e) {
        throw ShapeError(std::string("t[idx]: ") + e.what());
    }
}

// take_grad(t, indices, dy): scatter-add dy back into t's shape.
slot_t takeGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *input = tensorArg(norm, 0);
    if (input->rank() < 1) {
        reportShapeError("take_grad", "expects a tensor with at least one dimension");
    }
    const std::vector<int64_t> indices = intListArg(norm, 1, "take_grad");
    const int64_t extent               = input->dim(0);
    for (const int64_t index : indices) {
        if (index < 0 || index >= extent) {
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                index,
                std::string("the first dimension of the input tensor"),
                extent);
        }
    }
    const Array *dy = tensorArg(norm, 2);
    return runKernel("take_grad", [&] {
        return wrap(norm.resultType(), k::takeGrad(input, indices, dy, resultAllocator()));
    });
}

std::optional<Type *> takeGradInfer(const InferContext &ctx) {
    return tensorOf(ctx.facts(0).dtype, ctx.facts(0).shape);
}

// row_add_at(acc, index, row): `acc` with one row's gradient added where the index puts it. The
// tangent space of a tensor states this as the operator that takes an element's tangent back into
// a tangent of the whole value (`TangentSpace::scatterUri`), which is what a round of a loop over
// the tensor needs: `for row in t` takes the row `index` of the first dimension (design principles
// 5.4), so that round's gradient is a row gradient and it belongs at that row. `take_grad` is the
// same scatter for a list of indices, built from zeros; a loop's rounds accumulate into one
// gradient, so this one starts from the accumulator and takes a single index.
slot_t rowAddAtKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *acc = tensorArg(norm, 0);
    if (acc->rank() < 1) {
        reportShapeError("row_add_at", "expects a tensor with at least one dimension");
    }
    const int64_t index = intArg(norm, 1);
    if (index < 0 || index >= acc->dim(0)) {
        throwRuntimeFault(
            RuntimeDiag::IndexOutOfRange,
            index,
            std::string("the first dimension of the tensor"),
            acc->dim(0));
    }
    const Array *row = tensorArg(norm, 2);
    return runKernel("row_add_at", [&] {
        return wrap(norm.resultType(), k::rowAddAt(acc, index, row, resultAllocator()));
    });
}

std::optional<Type *> rowAddAtInfer(const InferContext &ctx) {
    return tensorOf(ctx.facts(0).dtype, ctx.facts(0).shape);
}

// one_hot(labels, classes): [len(labels), classes] float32 rows.
slot_t oneHotKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const int64_t classes              = intArg(norm, 1);
    const std::vector<int64_t> labels = intListArg(norm, 0, "one_hot");
    if (classes < 0) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument, "one_hot", std::string("classes must be non-negative"));
    }
    for (const int64_t label : labels) {
        if (label < 0 || label >= classes) {
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                label,
                std::string("the class dimension of one_hot"),
                classes);
        }
    }
    return runKernel("one_hot", [&] {
        return wrap(norm.resultType(), k::oneHot(labels, classes, resultAllocator()));
    });
}

std::optional<Type *> oneHotInfer(const InferContext &ctx) {
    const TensorFacts labels = ctx.facts(0);
    if (!isIntegerCode(labels.dtype)) {
        throw ShapeError("one_hot labels must be an integer array or a rank-1 integer tensor");
    }
    if (labels.shape && labels.shape->size() != 1) {
        throw ShapeError("one_hot labels must be an int array or a rank-1 integer tensor");
    }
    const auto classes = ctx.constInt(1);
    if (classes && *classes < 0) {
        throw ShapeError("one_hot classes must be non-negative");
    }
    StaticShape out;
    if (const auto constant = ctx.constInts(0)) {
        out.push_back(static_cast<int64_t>(constant->size()));
    } else if (const auto partial = ctx.partialInts(0)) {
        out.push_back(static_cast<int64_t>(partial->size()));
    } else if (labels.shape) {
        out.push_back((*labels.shape)[0]);
    } else {
        out.push_back(std::nullopt);
    }
    out.push_back(classes);
    return tensorOf(TypeCode::Float32, out);
}

// ---------------------------------------------------------------- VJP rules

// transpose swaps the last two axes, so it is its own inverse.
void transposeVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 1, 1);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(call.inputs[0], addTensorOper(b, "tensor:transpose", {*dy}));
    }
}

/// reshape, unsqueeze: the gradient takes the input's shape back. `reshape` also
/// accepts a numeric array, and a numeric array is a tensor here (a tensor is a numeric array:
/// `isTensorType` is `isNumericArrayType`), so one rule answers for both spellings -- the array's
/// adjoint is the input's shape and the tensor module has every operator for it (`tensor:shape`
/// of the input, `tensor:reshape` of dy). A container it can reshape but cannot carry a gradient
/// back into is refused by name while it depends on a differentiated parameter: `tensor:reshape`
/// answers with a tensor, which is a flat numeric array, and the container the gradient has to
/// return to holds its elements in another shape (`hasTensorAdjoint`).
void reshapeLikeVjp(VjpBuilder &b, const VjpCall &call) {
    if (auto dy = b.gradientOf(call.output)) {
        if (!hasTensorAdjoint(b, call, call.inputs[0], "its input")) {
            return;
        }
        const vjp_node_t shape = b.addOper(
            ArrayType::create(Type::Int64()),
            "tensor:shape",
            std::span<const vjp_node_t>(call.inputs.data(), 1));
        // The adjoint is shaped like the input, and `addTensorOperFor` is what says so: the node
        // takes the tangent type of `call.inputs[0]`, the value the gradient belongs to (design
        // principles 9.9: a gradient has the element type and the shape of its value). `dy` is
        // shaped like the *output*, so a node declared from the inputs alone would be one shape
        // too far -- the same numbers, recorded under the reshape's result shape.
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:reshape", {*dy, shape}));
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

// concat(a, b, axis): each part receives its slice of dy. A part may be a numeric array instead
// of a tensor, and a numeric array is a tensor here (a tensor is a numeric array: `isTensorType`
// is `isNumericArrayType`), so a numeric array part's adjoint is the slice of dy its length
// selects, the same slice a tensor part receives. A container that is not a numeric array (an
// array of arrays, an array of records) is accepted where a tensor is -- the forward rule
// flattens it -- but nothing carries a tensor's gradient back into it, so a part that depends on
// a differentiated parameter is refused by name (`hasTensorAdjoint`), and a constant one takes no
// gradient of its own and is skipped: only that part -- its length still places the other part's
// slice, and that length is the container's own (`:struct/len_arr`), which a tensor extent cannot
// answer for a value that holds its elements in another shape.
void concatVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 3);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t lhs = call.inputs[0], rhs = call.inputs[1], axis = call.inputs[2];
    const bool lhsTensor = hasTensorAdjoint(b, call, lhs, "a part of it");
    const bool rhsTensor = hasTensorAdjoint(b, call, rhs, "a part of it");
    if (!lhsTensor && !rhsTensor) {
        return; // every part is a constant container: no part carries a gradient
    }
    // Where the second part starts along the axis: the first part's own length when it is a
    // tensor, and the container's length (an int the container answers with) when it is one.
    const std::array<vjp_node_t, 1> containerOnly{lhs};
    const std::array<vjp_node_t, 2> lhsAxis{lhs, axis};
    const vjp_node_t split = lhsTensor ? b.addOper(Type::Int64(), "tensor:dim", lhsAxis)
                                       : b.addOper(Type::Int64(), ":struct/len_arr", containerOnly);
    // The core's own slice node, six words: the value, the axis, the two bounds, the step and the
    // mask that says which bounds the program wrote. Both bounds are written here, so the mask is
    // 3 (`src/compile/gct/builder.cpp`: `intConstant((written[1] ? 1 : 0) | (written[0] ? 2 : 0))`
    // over the start and the end) and the step is the constant 1. The adjoint slices dy out with
    // the one core operator, the same node the subscript builds, so concat's adjoint no longer
    // means a slice of the module's own.
    if (lhsTensor) {
        b.accumulateGradient(
            lhs,
            addTensorOper(
                b,
                ":op/slice_sub",
                {*dy, axis, staticInt(b, 0), split, staticInt(b, 1), staticInt(b, 3)}));
    }
    if (rhsTensor) {
        const std::array<vjp_node_t, 2> dyAxis{*dy, axis};
        const vjp_node_t total   = b.addOper(Type::Int64(), "tensor:dim", dyAxis);
        b.accumulateGradient(
            rhs,
            addTensorOper(
                b,
                ":op/slice_sub",
                {*dy, axis, split, total, staticInt(b, 1), staticInt(b, 3)}));
    }
}

// take(t, indices): rows are selected with repetition, so the reverse scatters dy back and
// sums where an index repeats. The indices take no gradient.
void takeVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    if (call.inputs.size() > 2) {
        // The same take on the dimension the third word names (`m[:, idx]`, design principles
        // 5.4). `tensor:take_grad` is the first dimension's scatter; the core's own scatter is the
        // reverse of the read along any dimension (`:tangent/scatter_take_arr`, built from the
        // zero array, design principles 9.9), and the core's rule for `:op/idx_take` builds
        // exactly this node, so the three-word call is answered with it and the axis has one
        // reading in every program.
        type::Type *const tangent =
            camel::core::machineryTangentTypeOf(b.nodeType(call.inputs[0]));
        if (tangent == nullptr) {
            return; // an array whose elements have no tangent: the take has none either
        }
        const std::array<vjp_node_t, 1> zeroInputs{call.inputs[0]};
        const vjp_node_t zero = b.addOper(tangent, ":tangent/zeros_arr", zeroInputs);
        const std::array<vjp_node_t, 4> scatter{
            zero, call.inputs[1], *dy, call.inputs[2]};
        b.accumulateGradient(
            call.inputs[0], b.addOper(tangent, ":tangent/scatter_take_arr", scatter));
        return;
    }
    b.accumulateGradient(
        call.inputs[0],
        addTensorOperFor(
            b,
            call.inputs[0],
            "tensor:take_grad",
            {call.inputs[0], call.inputs[1], *dy}));
}

// pick_at(t, at): the element at `at[0]` of a rank-1 tensor whose length `at[1]` states
// (`elementAtKernel` below). A read moves no element, so its adjoint is the read's own: the
// cotangent of the element goes back to the position that was read, and every other position keeps
// a zero (design principles 9.9: the reverse of a gather is a scatter-add). That is the adjoint the
// core files for the same read under its own spelling
// (`src/builtin/operators/derivative.cpp#idxArrayVjp`), written here with the position taken out of
// the list.
//
// The zeros are `:tangent/zeros_arr` of `t` and not an array built from `at[1]`: a pullback rule is
// handed the operand, and the length that operand holds is the one `elementAtKernel` checked the
// stated length against -- a length that disagrees is a fault there, never a silently reshaped
// read -- so the zeros are as long as `at[1]` says. `at[1]` is therefore read nowhere; the list is
// read only at `at[0]` (`:op/idx_arr(at, 0)`), the position the cotangent lands on. That is the one
// difference from the library's law for this read
// (`stdlib/rulesets/transpose.cml#pick_at_t`): a transpose law reads only known operands and has no
// array to take a length from, so it builds the same zeros from `at[1]`. Both scatter into `at[0]`,
// so both answer the same array
// (`test/cases/compile/transforms/pick_at_gradient_answered_by_the_engine.cml`).
void pickAtVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    type::Type *const tangent = camel::core::machineryTangentTypeOf(b.nodeType(call.inputs[0]));
    if (tangent == nullptr) {
        return; // an array whose elements have no tangent: the read has none either
    }
    const std::array<vjp_node_t, 1> zeroInputs{call.inputs[0]};
    const vjp_node_t zero = b.addOper(tangent, ":tangent/zeros_arr", zeroInputs);
    const std::array<vjp_node_t, 2> positionInputs{call.inputs[1], staticInt(b, 0)};
    const vjp_node_t position = b.addOper(Type::Int64(), ":op/idx_arr", positionInputs);
    const std::array<vjp_node_t, 3> scatter{zero, position, *dy};
    b.accumulateGradient(call.inputs[0], b.addOper(tangent, ":tangent/scatter_add_arr", scatter));
}

// pick_at(t, at): the element at `at[0]` of a rank-1 tensor whose length `at[1]` states.
//
// The length is not a convenience and not a hint: it is the operand that makes the read's pullback
// writable at all. `xs[i]`'s transpose is a scatter into an array of zeros with `xs`'s length
// (design principles 9.9), `T[]` carries no length in its type (5.4), and a transpose law may read
// only known operands -- the array itself is the operand the cotangent comes back through, so on
// that side it has no value (author, 2026-11-03 11:10 and 17:35). So the linear half of the read
// states the length: `rulesets.jvp`'s `jvp_idx_arr` passes `[i, len(xs)]`, a value the forward side
// computes, and `rulesets.transpose`'s `pick_at_t` builds its zeros from `at[1]` and reads
// nothing of the array.
//
// It is a second spelling of `:op/idx_arr` rather than a third operand of it, because that
// operator's key is filed at two operands in the core (`src/core/forward.cpp`,
// `kRuleKeyExceptions`) and a law's parameters are bound one per operand: a law filed under it
// cannot receive a third value (measured -- `skip=rule uri=:op/idx_arr reason=no-rule`).
//
// The position and the length are one integer list because the transpose walk answers an operator
// with one or two normal operands (the guard `tensor:mean_axis`'s three ports are held by); two
// integer operands beside the tensor would be three.
//
// The stated length is checked against the tensor and the position against both: a length that
// disagrees is a fault, not a silently reshaped answer. An answer sized by a number nobody verified
// is the one failure this operator exists to make impossible.
slot_t elementAtKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *t = tensorArg(norm, 0);
    if (t->rank() != 1) {
        reportShapeError("pick_at", "expects a tensor with exactly one dimension");
    }
    const std::vector<int64_t> at = intListArg(norm, 1, "pick_at");
    if (at.size() != 2) {
        reportShapeError(
            "pick_at",
            "expects a two-entry integer list: the position and the length of the tensor");
    }
    const int64_t length = at[1];
    if (length < 0 || static_cast<uint64_t>(length) != t->numel()) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            std::string("pick_at"),
            std::format(
                "the stated length {} does not agree with the tensor's {}", length, t->numel()));
    }
    const int64_t index = at[0];
    if (index < 0 || index >= length) {
        throwRuntimeFault(
            RuntimeDiag::IndexOutOfRange, index, std::string("the tensor"), length);
    }
    // The element is the array's own slot: the type the read declares is the element type the
    // tensor states (`elementAtInfer`), so nothing is converted and no kernel is needed.
    return t->getSlot(static_cast<size_t>(index));
}

/// The read's result type: one element of the tensor's own element type.
std::optional<Type *> elementAtInfer(const InferContext &ctx) {
    const TensorFacts t = ctx.facts(0);
    if (t.shape && t.shape->size() != 1) {
        throw ShapeError("pick_at expects a tensor with exactly one dimension");
    }
    // An element type nobody stated answers `f64`, the way the whole-tensor reductions answer it.
    return Type::create(t.dtype.value_or(TypeCode::Float64));
}

// The stated take's adjoint, `take_stated(t, indices, extents)`.
//
// It is `take`'s adjoint: `tensor:take_grad(t, indices, dy)` takes the array's shape from `t` and
// the row count from the indices. The extents are read nowhere here, and since the indices became
// a parameter of their own the slice that used to cut them out of a packed list is gone -- the node
// this rule walks to `take_grad` is the caller's own `indices`. That is the one difference from
// the library's law for this operator (`stdlib/rulesets/transpose.cml#take_stated_t`), which
// builds the zeros from the extents because a transpose law reads only known operands and the
// tensor a cotangent comes back through has no value there (author, 2026-11-03 11:10 and 17:35).
// Both answer the same array:
// `test/cases/compile/transforms/stated_take_answered_by_the_engine.cml` prints the two together.
void takeStatedVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 3);
    const auto dy = b.gradientOf(call.output);
    if (!dy) {
        return;
    }
    const vjp_node_t t       = call.inputs[0];
    const vjp_node_t indices = call.inputs[1];
    b.accumulateGradient(t, addTensorOperFor(b, t, "tensor:take_grad", {t, indices, *dy}));
}

// take_stated(t, indices, extents): the rows `take` reads, with the indices it reads **and** the
// operand's extents stated beside them.
//
// A take's adjoint puts each row's cotangent back where that row came from, in an array of zeros
// with the *operand's* shape -- and the operand is the one thing the reverse side does not have: a
// transpose law reads only known operands, and `t` is the operand the cotangent comes back through
// (author, 2026-11-03 11:10, 17:35). The indices do not supply the row count and the cotangent
// supplies only the columns, so the extents have to be stated by the forward half
// (`rulesets.jvp`'s `jvp_take` passes `shape(t)` beside the indices).
//
// The two arrays are separate parameters: `indices` is what `take` reads and `extents` is the
// shape the adjoint spreads the rows back into. They used to travel in one list with a leading
// count, because the transpose walk was believed to answer an operator with one or two normal
// operands; the walk takes two `int[]` operands beside the tensor, which is what the take_stated
// law reads today.
//
// Both are checked: the extents against the tensor's shape (`support.h#checkStatedExtents`) and
// every index against the first extent, so a gradient is never built over a shape nobody verified.
slot_t takeStatedKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *input = tensorArg(norm, 0);
    if (input->rank() < 1) {
        reportShapeError("take_stated", "expects a tensor with at least one dimension");
    }
    const std::vector<int64_t> indices = intListArg(norm, 1, "take_stated");
    const std::vector<int64_t> extents = intListArg(norm, 2, "take_stated");
    checkStatedExtents(input, extents, 0, "take_stated");
    const int64_t extent = input->dim(0);
    for (const int64_t index : indices) {
        if (index < 0 || index >= extent) {
            throwRuntimeFault(
                RuntimeDiag::IndexOutOfRange,
                index,
                std::string("the first dimension of the input tensor"),
                extent);
        }
    }
    return runKernel("take_stated", [&] {
        return wrap(norm.resultType(), k::take(input, indices, resultAllocator()));
    });
}

/// The stated take's result type. Both the index count and the extents are values of the run in this
/// spelling, so the type keeps the rank open (`T[..]`, which `take`'s own open-rank spelling also
/// answers) and states the operand's element type: the answer is `take`'s, at the shape the value
/// has, and the reverse law is what the extents are for.
std::optional<Type *> takeStatedInfer(const InferContext &ctx) {
    const TensorFacts input = ctx.facts(0);
    if (input.shape && input.shape->empty()) {
        throw ShapeError("take_stated expects a tensor with at least one dimension");
    }
    return tensorOf(input.dtype, std::nullopt);
}

} // namespace

std::vector<OpDef> layoutOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "shape",
        .exports   = {"shape"},
        .params    = {{"t", ParamKind::TensorLike}},
        .resultDoc = "int[]",
        .infer         = shapeInfer,
        .kernel        = &shapeKernel,
        .traits        = {},
        .foldFromTypes = shapeFold});
    defs.push_back(OpDef{
        .name      = "reshape",
        .exports   = {"reshape"},
        .params    = {{"t", ParamKind::TensorLike}, {"shape", ParamKind::IntArray}},
        .resultDoc = "float[...]",
        .infer          = reshapeInfer,
        .kernel         = &positionally<reshapeKernel>,
        .traits         = {},
        .withAfterNorm  = true,
        .ordinaryParams = 1});
    defs.push_back(OpDef{
        .name      = "unsqueeze",
        .exports   = {"unsqueeze"},
        .params    = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int}},
        .resultDoc = "float[...]",
        .infer          = unsqueezeInfer,
        .kernel         = &positionally<unsqueezeKernel>,
        .traits         = {},
        .withAfterNorm  = true,
        .ordinaryParams = 1});
    defs.push_back(OpDef{
        .name      = "transpose",
        .exports   = {"transpose"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "float[...]",
        .infer     = transposeInfer,
        .kernel    = &transposeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "permute",
        .exports   = {"permute"},
        .params    = {{"t", ParamKind::Tensor}, {"perm", ParamKind::IntArray}},
        .resultDoc = "float[...]",
        .infer          = permuteInfer,
        .kernel         = &positionally<permuteKernel>,
        .traits         = {},
        .withAfterNorm  = true,
        .ordinaryParams = 1});
    defs.push_back(OpDef{
        .name    = "concat",
        .exports = {"concat"},
        .params =
            {{"a", ParamKind::TensorLike}, {"b", ParamKind::TensorLike}, {"axis", ParamKind::Int}},
        .resultDoc = "float[...]",
        .infer          = concatInfer,
        .kernel         = &positionally<concatKernel>,
        .traits         = {},
        .withAfterNorm  = true,
        .ordinaryParams = 2});
    defs.push_back(OpDef{
        .name      = "take",
        .exports   = {"take"},
        .params    = {{"t", ParamKind::Tensor}, {"indices", ParamKind::TensorLike}},
        .resultDoc = "float[...]",
        .infer     = takeInfer,
        .kernel    = &takeKernel,
        .traits    = {}});
    // The read with a stated length: `t[at[0]]` of a rank-1 tensor whose length `at[1]` states.
    // Both are the element type's own answer (see `elementAtKernel` and `stdlib/tensor.cml`).
    defs.push_back(OpDef{
        .name      = "pick_at",
        .exports   = {"pick_at"},
        .params    = {{"t", ParamKind::Tensor}, {"at", ParamKind::IntArray}},
        .resultDoc = "T",
        .infer     = elementAtInfer,
        .kernel    = &elementAtKernel,
        .traits    = {}});
    // The take with its indices and its extents stated (`takeStatedKernel` above): the second
    // spelling a `take`'s reverse law needs, one ordinary operand beside the list.
    defs.push_back(OpDef{
        .name      = "take_stated",
        .exports   = {"take_stated"},
        .params    =
            {{"t", ParamKind::Tensor}, {"indices", ParamKind::IntArray}, {"extents", ParamKind::IntArray}},
        .resultDoc = "T[...]",
        .infer     = takeStatedInfer,
        .kernel    = &takeStatedKernel,
        .traits    = {}});
    // The subscript forms of design principles 5.4: `t[a:b]` and `t[idx]` on a tensor. Both are
    // the core's own operators (`t[a:b]` is `:op/slice_sub`), so only the index form needs a
    // declaration of its own; a call that names the dimension it takes on (three words) is the
    // core's declaration's, and this one answers the first dimension's two-word call.
    defs.push_back(OpDef{
        .name      = "idx_take",
        .exports   = {"__idx__"},
        .params    = {{"t", ParamKind::Tensor}, {"indices", ParamKind::TensorLike}},
        .resultDoc = "float[...]",
        .infer     = subscriptTakeInfer,
        .kernel    = &subscriptTakeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "take_grad",
        .exports   = {},
        .params    = {{"t", ParamKind::Tensor},
                      {"indices", ParamKind::TensorLike},
                      {"dy", ParamKind::Tensor}},
        .resultDoc = "float[...]",
        .infer     = takeGradInfer,
        .kernel    = &takeGradKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "row_add_at",
        .exports   = {},
        .params    = {{"acc", ParamKind::Tensor}, {"index", ParamKind::Int}, {"row", ParamKind::Tensor}},
        .resultDoc = "float[...]",
        .infer     = rowAddAtInfer,
        .kernel    = &rowAddAtKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "one_hot",
        .exports   = {"one_hot"},
        .params    = {{"labels", ParamKind::TensorLike}, {"classes", ParamKind::Int}},
        // The rows are `f32` whatever the labels are (`oneHotInfer`, and the kernel's
        // `createTensor(TypeCode::Float32, ...)`), so this is the one element type to state: the
        // family word `float[..]` would say the element type follows an argument, and this operator
        // has no such argument.
        .resultDoc = "f32[...]",
        .infer     = oneHotInfer,
        .kernel    = &oneHotKernel,
        .traits    = {}});
    setVjp(defs, "transpose", &transposeVjp);
    setVjp(defs, "reshape", &reshapeLikeVjp);
    setVjp(defs, "unsqueeze", &reshapeLikeVjp);
    setVjp(defs, "permute", &permuteVjp);
    setVjp(defs, "concat", &concatVjp);
    setVjp(defs, "take", &takeVjp);
    setVjp(defs, "shape", &camel::core::noGradient);
    // The subscript forms are differentiated by the same rule as the calls they name: the
    // indices of `t[idx]` take no gradient, and t's receives the scatter-add (takeVjp).
    setVjp(defs, "idx_take", &takeVjp);
    // The read with a stated length is differentiated by the read's own adjoint (`pickAtVjp` above):
    // `pick_at` is `xs[i]` with the length stated, so its pullback is `:op/idx_arr`'s, with the
    // position read out of the list.
    setVjp(defs, "pick_at", &pickAtVjp);
    // one_hot's input is discrete (an integer array or a rank-1 integer tensor): its derivative
    // is zero by definition (design principles 9.9), and the float one-hot rows are locally
    // constant in it. The rule is therefore empty: no gradient, no error, and no caller has to
    // write stop_gradient around a label. `classes` is an int parameter and takes no gradient
    // either. The rows' own gradient is used up here, as for every locally constant result.
    setVjp(defs, "one_hot", &camel::core::noGradient);
    // The stated take, beside the two reads whose value it is (`takeStatedVjp`): a program may
    // write `take_stated` itself, and its value is `take`'s, so the reverse pass answers it with
    // the same scatter.
    setVjp(defs, "take_stated", &takeStatedVjp);
    return defs;
}

} // namespace camel::tensor::ops
