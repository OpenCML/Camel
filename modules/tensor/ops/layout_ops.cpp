// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    ::Array *array =
        ::Array::create(allocator, ArrayType::create(Type::Int64()), shape->size());
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
            shape.push_back(known ? *extent : kUnknownDim);
            probe.push_back(known ? *extent : -1);
            open += known ? 0 : 1;
        }
        if (open == 1 && fullyKnown(in.shape)) {
            try {
                return tensorOf(in.dtype, k::resolveReshape(*in.shape, probe));
            } catch (const std::exception &) {
                // No value fits: left to reshape to report when it runs.
            }
        }
        return tensorOf(in.dtype, shape);
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
        Array *t    = tensorArg(norm, 0);
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
        Array *t = tensorArg(norm, 0);
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
        const Array *parts[] = {tensorArg(norm, 0, true), tensorArg(norm, 1, true)};
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
    // Bounds outside the axis are a located fault, as for an array's slice (design principles
    // 3.9); checked before the kernel so the diagnostic is SliceOutOfRange, not a kernel error.
    // A malformed axis or step is left to the kernel.
    const Array *input = tensorArg(norm, 0);
    const int64_t axisArg     = intArg(norm, 1);
    const int64_t start = intArg(norm, 2), end = intArg(norm, 3);
    const auto rank     = static_cast<int64_t>(input->rank());
    const int64_t axis  = axisArg < 0 ? axisArg + rank : axisArg;
    if (axis >= 0 && axis < rank) {
        const int64_t extent = input->dim(static_cast<size_t>(axis));
        if (!k::normalizeSliceBounds(extent, start, end)) {
            throwRuntimeFault(
                RuntimeDiag::SliceOutOfRange,
                start,
                end,
                std::format("axis {} of a tensor", axis),
                extent);
        }
    }
    return runKernel("slice", [&] {
        const int64_t step = hasArg(norm, 4) ? intArg(norm, 4) : 1;
        return wrap(k::slice(input, axisArg, start, end, step, resultAllocator()));
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
    // Extent and bounds are known at compile time: an interval that leaves the axis is an
    // error here (design principles 3.9). With an unknown extent or bounds the operator checks
    // at run time and faults with SliceOutOfRange (3.9).
    const auto range = k::normalizeSliceBounds(ext, *start, *end);
    if (!range) {
        throw ShapeError(
            std::format(
                "slice bounds {}..{} are outside axis {} of extent {}",
                *start,
                *end,
                a,
                ext));
    }
    const int64_t s = range->start, e = range->end;
    (*in.shape)[a]  = e > s ? (e - s + *step - 1) / *step : 0;
    return tensorOf(in);
}

// ---------------------------------------------------------------- take / one_hot

bool isIntegerCode(const std::optional<TypeCode> &code) {
    return !code || *code == TypeCode::Int32 || *code == TypeCode::Int64;
}

bool isIntegerType(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::Array) {
        const Type *elem = static_cast<const ArrayType *>(type)->elemType();
        return elem != nullptr &&
               (elem->code() == TypeCode::Int32 || elem->code() == TypeCode::Int64);
    }
    return type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64;
}

/// An integer list argument: an int array or a rank-1 integer tensor, read as int64.
std::vector<int64_t> intListArg(ArgsView &args, size_t index, std::string_view op) {
    Type *type = args.type(index);
    if (type->code() == TypeCode::Array) {
        ::Array *array = args.get<::Array *>(index);
        // An empty literal `[]` has no element type to infer from (H15 literal context typing is
        // not here yet); there are no elements to convert, so it is the empty integer list.
        if (array->size() == 0) {
            return {};
        }
        if (!isIntegerType(type)) {
            reportShapeError(op, "expects an integer array");
        }
        return parseIntArray(array, type);
    }
    const Array *tensor = tensorArg(args, index);
    if (tensor->dtype() != TypeCode::Int64 && tensor->dtype() != TypeCode::Int32) {
        reportShapeError(op, "expects integer indices");
    }
    if (tensor->rank() != 1) {
        reportShapeError(op, "expects a rank-1 index tensor");
    }
    std::vector<int64_t> values(tensor->numel());
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        values[static_cast<size_t>(i)] = tensor->getAsInt64(i);
    }
    return values;
}

// `t[a:b]`, `t[a:b:s]`, `t[:b]`, `t[a:]`, `t[:]` on a tensor: the slice of the first axis
// (design principles 5.4), a view sharing the block. A slice of another axis, and a step other
// than 1, are views with strides, which need the multidimensional array representation of H17:
// the type rule rejects them at the call instead of a copy standing in for the view.
slot_t sliceSubKernel(ArgsView &, ArgsView &norm, context::Context &) {
    const Array *input  = tensorArg(norm, 0);
    const int64_t start = intArg(norm, 1);
    const int64_t step  = intArg(norm, 3);
    const bool wroteEnd = (intArg(norm, 4) & 1) != 0;
    if (input->rank() != 1) {
        reportShapeError(
            "[]",
            "a slice of a tensor of rank 1; a slice of another dimension is a view with "
            "strides, which needs the multidimensional array representation of H17");
    }
    const int64_t extent = input->dim(0);
    const int64_t end    = wroteEnd ? intArg(norm, 2) : extent;
    // Bounds outside the axis are a located fault, as for an array's slice (design principles
    // 3.9); checked before the kernel so the diagnostic is SliceOutOfRange.
    if (start < 0 || start > extent || end < 0 || end > extent) {
        throwRuntimeFault(
            RuntimeDiag::SliceOutOfRange, start, end, "axis 0 of a tensor", extent);
    }
    if (step != 1) {
        throwRuntimeFault(
            RuntimeDiag::InvalidArgument,
            "[]",
            "a slice step must be 1; a step other than 1 is a view with strides, which needs "
            "the multidimensional array representation of H17");
    }
    const int64_t count = end > start ? end - start : 0;
    return runKernel("slice_sub", [&] {
        return wrap(viewTensor(
            input, std::span<const int64_t>(&count, 1), resultAllocator(), start));
    });
}

std::optional<Type *> sliceSubInfer(const InferContext &ctx) {
    const TensorFacts in = ctx.facts(0);
    if (!in.shape) {
        return tensorOf(in); // rank unknown: nothing to check
    }
    if (in.shape->empty()) {
        throw ShapeError("a tensor without dimensions has no first dimension to slice");
    }
    if (in.shape->size() > 1) {
        throw ShapeError(
            "a slice of the first dimension of a tensor of rank 2 or more is a view with "
            "strides, which needs the multidimensional array representation of H17");
    }
    // The slice is a view sharing the block, which fixes the step: strides are H17's.
    const auto step = ctx.constInt(3);
    if (!step || *step != 1) {
        throw ShapeError(
            "a slice step other than the constant 1 is a view with strides, which needs the "
            "multidimensional array representation of H17");
    }
    const int64_t extent = (*in.shape)[0];
    const auto start     = ctx.constInt(1);
    const auto end       = ctx.constInt(2);
    const auto wroteEnd  = ctx.constInt(4);
    if (extent == kUnknownDim || !start || !wroteEnd) {
        return tensorOf(in.dtype, StaticShape{kUnknownDim});
    }
    const int64_t to = (*wroteEnd & 1) != 0 ? (end ? *end : kUnknownDim) : extent;
    if (to == kUnknownDim) {
        return tensorOf(in.dtype, StaticShape{kUnknownDim});
    }
    // Extent and bounds are known at compile time: an interval that leaves the axis is an
    // error here (design principles 3.9).
    if (*start < 0 || *start > extent || to < 0 || to > extent) {
        throw ShapeError(
            std::format(
                "slice bounds {}..{} are outside the first dimension of extent {}",
                *start,
                to,
                extent));
    }
    return tensorOf(in.dtype, StaticShape{to > *start ? to - *start : 0});
}

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
        return wrap(k::take(input, indices, resultAllocator()));
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
    const int64_t extent = input.shape ? (*input.shape)[0] : kUnknownDim;
    if (const auto constant = ctx.constInts(1)) {
        if (extent != kUnknownDim) {
            for (const int64_t index : *constant) {
                if (index < 0 || index >= extent) {
                    throw ShapeError(std::format(
                        "index {} is out of range for the first dimension of extent {}",
                        index,
                        extent));
                }
            }
        }
        out.push_back(static_cast<int64_t>(constant->size()));
    } else if (indices.shape) {
        out.push_back((*indices.shape)[0]);
    } else {
        out.push_back(kUnknownDim);
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
        return wrap(k::takeGrad(input, indices, dy, resultAllocator()));
    });
}

std::optional<Type *> takeGradInfer(const InferContext &ctx) {
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
        return wrap(k::oneHot(labels, classes, resultAllocator()));
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
        out.push_back(kUnknownDim);
    }
    out.push_back(classes.value_or(kUnknownDim));
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

// take(t, indices): rows are selected with repetition, so the reverse scatters dy back and
// sums where an index repeats. The indices take no gradient.
void takeVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(b, "tensor:take_grad", {call.inputs[0], call.inputs[1], *dy}));
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
    defs.push_back(OpDef{
        .name      = "take",
        .exports   = {"take"},
        .params    = {{"t", ParamKind::Tensor}, {"indices", ParamKind::TensorLike}},
        .resultDoc = "Tensor",
        .infer     = takeInfer,
        .kernel    = &takeKernel,
        .traits    = {}});
    // The subscript forms of design principles 5.4: `t[a:b]` and `t[idx]` on a tensor. They
    // merge into the built-in module's `__slice__` and `__idx__` groups as overloads.
    defs.push_back(OpDef{
        .name    = "slice_sub",
        .exports = {"__slice__"},
        .params =
            {{"t", ParamKind::Tensor},
             {"start", ParamKind::Int},
             {"end", ParamKind::Int},
             {"step", ParamKind::Int},
             {"endWritten", ParamKind::Int}},
        .resultDoc = "Tensor",
        .infer     = sliceSubInfer,
        .kernel    = &sliceSubKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "idx_take",
        .exports   = {"__idx__"},
        .params    = {{"t", ParamKind::Tensor}, {"indices", ParamKind::TensorLike}},
        .resultDoc = "Tensor",
        .infer     = subscriptTakeInfer,
        .kernel    = &subscriptTakeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "take_grad",
        .exports   = {},
        .params    = {{"t", ParamKind::Tensor},
                      {"indices", ParamKind::TensorLike},
                      {"dy", ParamKind::Tensor}},
        .resultDoc = "Tensor",
        .infer     = takeGradInfer,
        .kernel    = &takeGradKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "one_hot",
        .exports   = {"one_hot"},
        .params    = {{"labels", ParamKind::TensorLike}, {"classes", ParamKind::Int}},
        .resultDoc = "Tensor",
        .infer     = oneHotInfer,
        .kernel    = &oneHotKernel,
        .traits    = {}});
    setVjp(defs, "transpose", &transposeVjp);
    setVjp(defs, "reshape", &reshapeLikeVjp);
    setVjp(defs, "flatten", &reshapeLikeVjp);
    setVjp(defs, "unsqueeze", &reshapeLikeVjp);
    setVjp(defs, "permute", &permuteVjp);
    setVjp(defs, "concat", &concatVjp);
    setVjp(defs, "slice", &sliceVjp);
    setVjp(defs, "take", &takeVjp);
    setVjp(defs, "shape", &camel::core::noGradient);
    return defs;
}

} // namespace camel::tensor::ops
