/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Lowerings of the operators that reverse-mode differentiation emits
 * (tensor:sum_to, tensor:matmul_grad_lhs, tensor:layer_norm_grad, ...) and of
 * the tree optimizer nn:sgd, so that a training step exports as one ONNX
 * graph. Most of them decompose into several ONNX nodes. They need the static
 * ranks of their operands, which shape specialization provides.
 */

#include "lowering.h"

#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"

#include <format>

namespace camel::onnx {

using tensor::ops::TensorFacts;
using type::TypeCode;

namespace {

std::string floatConst(Emitter &e, double v) {
    return e.operand(
        Value::constant(rtdata::toSlot(static_cast<rtdata::Float64>(v)), type::Type::Float64()),
        TypeCode::Float32);
}

/// ReduceSum/ReduceMean of `x` over `axes`, in the form the opset takes.
std::string reduce(
    Emitter &e, const std::string &op, const std::string &x, std::vector<int64_t> axes,
    bool keepDims) {
    const bool axesInput = op == "ReduceSum" ? e.opset() >= 13 : e.opset() >= 18;
    const Attribute keep = Attribute::makeInt("keepdims", keepDims ? 1 : 0);
    if (axesInput) {
        return e.node(op, {x, e.int64s(axes)}, {keep});
    }
    return e.node(op, {x}, {Attribute::makeInts("axes", std::move(axes)), keep});
}

tensor::StaticShape staticShape(const TensorFacts &facts, std::string_view what) {
    if (!facts.shape) {
        throw ExportError(std::format("{}: the rank of an operand is not known statically", what));
    }
    return *facts.shape;
}

bool fullyKnown(const tensor::StaticShape &shape) {
    for (int64_t d : shape) {
        if (d == tensor::kUnknownDim) {
            return false;
        }
    }
    return true;
}

/// The shape of argument `index` as an int64[] operand: a constant when static.
std::string shapeOperand(const LowerContext &ctx, size_t index) {
    const TensorFacts facts = ctx.facts(index);
    if (facts.shape && fullyKnown(*facts.shape)) {
        return ctx.emitter().int64s(*facts.shape);
    }
    return ctx.emitter().node("Shape", {ctx.input(index)});
}

/// `g` summed down to `like`'s shape (the reverse of broadcasting).
std::string sumTo(
    const LowerContext &ctx, const std::string &g, const TensorFacts &gFacts, size_t likeIndex) {
    const auto gShape     = staticShape(gFacts, ctx.uri());
    const auto likeShape  = staticShape(ctx.facts(likeIndex), ctx.uri());
    if (likeShape.size() > gShape.size()) {
        throw ExportError(std::format("{}: the gradient has a lower rank than its target", ctx.uri()));
    }
    const size_t lead = gShape.size() - likeShape.size();
    std::vector<int64_t> axes;
    for (size_t i = 0; i < gShape.size(); ++i) {
        if (i < lead) {
            axes.push_back(static_cast<int64_t>(i));
            continue;
        }
        const int64_t want = likeShape[i - lead], have = gShape[i];
        if (want == 1 && have != 1) {
            if (have == tensor::kUnknownDim) {
                throw ExportError(
                    std::format("{}: cannot tell whether a dynamic axis was broadcast", ctx.uri()));
            }
            axes.push_back(static_cast<int64_t>(i));
        }
    }
    if (axes.empty()) {
        return g;
    }
    const std::string summed = reduce(ctx.emitter(), "ReduceSum", g, axes, true);
    return ctx.emitter().node("Reshape", {summed, shapeOperand(ctx, likeIndex)});
}

Value lowerSumTo(const LowerContext &ctx) {
    const std::string g = ctx.input(0, ctx.result().dtype);
    return ctx.result(sumTo(ctx, g, ctx.facts(0), 1));
}

Value lowerBroadcastLike(const LowerContext &ctx) {
    return ctx.emit("Expand", {ctx.input(0, ctx.result().dtype), shapeOperand(ctx, 1)});
}

Value fillLike(const LowerContext &ctx, double value) {
    Emitter &e = ctx.emitter();
    return ctx.emit(
        "Expand",
        {e.operand(
             Value::constant(
                 rtdata::toSlot(static_cast<rtdata::Float64>(value)),
                 type::Type::Float64()),
             ctx.result().dtype),
         shapeOperand(ctx, 0)});
}

// expand_axis(g, like, axis, keepdims = true): g reduced along `axis`, spread back over it.
Value lowerExpandAxis(const LowerContext &ctx) {
    const auto rank = static_cast<int64_t>(ctx.rank(1));
    int64_t axis    = ctx.constInt(2);
    axis            = axis < 0 ? axis + rank : axis;
    std::string g   = ctx.input(0, ctx.result().dtype);
    if (!ctx.constBool(3, true)) {
        const int64_t axes[] = {axis};
        g                    = ctx.emitter().node("Unsqueeze", {g, ctx.emitter().int64s(axes)});
    }
    return ctx.emit("Expand", {g, shapeOperand(ctx, 1)});
}

// gelu_grad(x, dy) for the tanh approximation:
// dy * (0.5 (1 + t) + 0.5 x (1 - t^2) k (1 + 3 c x^2)), t = tanh(k (x + c x^3)).
Value lowerGeluGrad(const LowerContext &ctx) {
    Emitter &e              = ctx.emitter();
    constexpr double kScale = 0.7978845608028654, kCubic = 0.044715;
    const std::string x     = ctx.input(0, TypeCode::Float32);
    const std::string dy    = ctx.input(1, TypeCode::Float32);
    const std::string x2    = e.node("Mul", {x, x});
    const std::string inner = e.node(
        "Mul",
        {e.node("Add", {x, e.node("Mul", {e.node("Mul", {x2, x}), floatConst(e, kCubic)})}),
         floatConst(e, kScale)});
    const std::string t     = e.node("Tanh", {inner});
    const std::string left  = e.node("Mul", {e.node("Add", {t, floatConst(e, 1.0)}), floatConst(e, 0.5)});
    const std::string sech2 = e.node("Sub", {floatConst(e, 1.0), e.node("Mul", {t, t})});
    const std::string poly  = e.node(
        "Mul",
        {e.node("Add", {floatConst(e, 1.0), e.node("Mul", {x2, floatConst(e, 3.0 * kCubic)})}),
         floatConst(e, 0.5 * kScale)});
    const std::string right = e.node("Mul", {e.node("Mul", {x, sech2}), poly});
    return ctx.emit("Mul", {dy, e.node("Add", {left, right})});
}

// erf_grad(x, dy) = dy * 2 / sqrt(pi) * exp(-x^2)
Value lowerErfGrad(const LowerContext &ctx) {
    Emitter &e           = ctx.emitter();
    const std::string x  = ctx.input(0, TypeCode::Float32);
    const std::string dy = ctx.input(1, TypeCode::Float32);
    const std::string ex = e.node("Exp", {e.node("Neg", {e.node("Mul", {x, x})})});
    return ctx.emit("Mul", {dy, e.node("Mul", {ex, floatConst(e, 1.1283791670955126)})});
}

// layer_norm_grad(x, gamma, dy, which, eps): normalization over the last axis; which 0 = input,
// 1 = gamma, 2 = beta.
Value lowerLayerNormGrad(const LowerContext &ctx) {
    Emitter &e              = ctx.emitter();
    const int64_t which     = ctx.constInt(3);
    const auto rank         = static_cast<int64_t>(ctx.rank(0));
    const std::string dy    = ctx.input(2, TypeCode::Float32);
    std::vector<int64_t> lead;
    for (int64_t i = 0; i + 1 < rank; ++i) {
        lead.push_back(i);
    }
    if (which == 2) {
        return ctx.result(lead.empty() ? dy : reduce(e, "ReduceSum", dy, lead, false));
    }
    const std::string x    = ctx.input(0, TypeCode::Float32);
    const std::string xc   = e.node("Sub", {x, reduce(e, "ReduceMean", x, {-1}, true)});
    const std::string var  = reduce(e, "ReduceMean", e.node("Mul", {xc, xc}), {-1}, true);
    const std::string inv  = e.node(
        "Reciprocal",
        {e.node("Sqrt", {e.node("Add", {var, floatConst(e, ctx.constNumber(4, 1e-5))})})});
    const std::string xhat = e.node("Mul", {xc, inv});
    if (which == 1) {
        const std::string prod = e.node("Mul", {dy, xhat});
        return ctx.result(lead.empty() ? prod : reduce(e, "ReduceSum", prod, lead, false));
    }
    if (which != 0) {
        throw ExportError("tensor:layer_norm_grad: unknown gradient selector");
    }
    // dx = inv * (h - mean(h) - xhat * mean(h * xhat)), h = dy * gamma.
    const std::string h      = e.node("Mul", {dy, ctx.input(1, TypeCode::Float32)});
    const std::string meanH  = reduce(e, "ReduceMean", h, {-1}, true);
    const std::string meanHX = reduce(e, "ReduceMean", e.node("Mul", {h, xhat}), {-1}, true);
    const std::string inner =
        e.node("Sub", {e.node("Sub", {h, meanH}), e.node("Mul", {xhat, meanHX})});
    return ctx.emit("Mul", {inv, inner});
}

// softmax_grad(y, dy, axis = -1) = y * (dy - sum(y * dy, axis))
Value lowerSoftmaxGrad(const LowerContext &ctx) {
    Emitter &e           = ctx.emitter();
    const std::string y  = ctx.input(0, TypeCode::Float32);
    const std::string dy = ctx.input(1, TypeCode::Float32);
    const int64_t axis   = ctx.constInt(2, -1);
    const std::string dot = reduce(e, "ReduceSum", e.node("Mul", {y, dy}), {axis}, true);
    return ctx.emit("Mul", {y, e.node("Sub", {dy, dot})});
}

// slice_grad(dy, like, axis, start, end, step = 1): dy padded with zeros back to like's shape.
Value lowerSliceGrad(const LowerContext &ctx) {
    const auto likeShape  = staticShape(ctx.facts(1), ctx.uri());
    const auto rank       = static_cast<int64_t>(likeShape.size());
    int64_t axis          = ctx.constInt(2);
    axis                  = axis < 0 ? axis + rank : axis;
    if (ctx.constInt(5, 1) != 1) {
        throw ExportError("tensor:slice_grad: only unit steps can be exported");
    }
    const int64_t extent = likeShape[static_cast<size_t>(axis)];
    if (extent == tensor::kUnknownDim) {
        throw ExportError("tensor:slice_grad: the sliced axis must have a static extent");
    }
    const auto clamp = [&](int64_t v) {
        v = v < 0 ? v + extent : v;
        return std::clamp<int64_t>(v, 0, extent);
    };
    const int64_t start = clamp(ctx.constInt(3)), end = clamp(ctx.constInt(4));
    std::vector<int64_t> pads(static_cast<size_t>(2 * rank), 0);
    pads[static_cast<size_t>(axis)]        = start;
    pads[static_cast<size_t>(rank + axis)] = extent - end;
    return ctx.emit("Pad", {ctx.input(0, ctx.result().dtype), ctx.emitter().int64s(pads)});
}

/// The last two axes of `x` swapped.
std::string swapLast(Emitter &e, const std::string &x, size_t rank) {
    std::vector<int64_t> perm;
    for (size_t i = 0; i < rank; ++i) {
        perm.push_back(static_cast<int64_t>(i));
    }
    std::swap(perm[rank - 2], perm[rank - 1]);
    return e.node("Transpose", {x}, {Attribute::makeInts("perm", std::move(perm))});
}

// matmul_grad_lhs(dy, lhs, rhs) = sum_to(dy @ rhs^T, lhs);
// matmul_grad_rhs(dy, lhs, rhs) = sum_to(lhs^T @ dy, rhs).
template <bool Lhs> Value lowerMatmulGrad(const LowerContext &ctx) {
    Emitter &e              = ctx.emitter();
    const auto dyShape      = staticShape(ctx.facts(0), ctx.uri());
    const auto lhsShape     = staticShape(ctx.facts(1), ctx.uri());
    const auto rhsShape     = staticShape(ctx.facts(2), ctx.uri());
    if (lhsShape.size() < 2 || rhsShape.size() < 2) {
        throw ExportError(std::format("{}: vector operands cannot be exported", ctx.uri()));
    }
    const auto dtype        = ctx.result().dtype;
    const std::string dy    = ctx.input(0, dtype);
    std::string product;
    tensor::StaticShape productShape = dyShape;
    if (Lhs) {
        product = e.node("MatMul", {dy, swapLast(e, ctx.input(2, dtype), rhsShape.size())});
        productShape.back() = lhsShape.back();
    } else {
        product = e.node("MatMul", {swapLast(e, ctx.input(1, dtype), lhsShape.size()), dy});
        productShape[productShape.size() - 2] = rhsShape[rhsShape.size() - 2];
    }
    return ctx.result(sumTo(ctx, product, {dtype, productShape}, Lhs ? 1 : 2));
}

Value lowerCastLike(const LowerContext &ctx) {
    const auto like = ctx.facts(1).dtype;
    return ctx.result(ctx.input(0, like));
}

Value lowerPermuteInverse(const LowerContext &ctx) {
    const std::vector<int64_t> perm = ctx.constInts(1);
    std::vector<int64_t> inverse(perm.size(), 0);
    for (size_t i = 0; i < perm.size(); ++i) {
        const int64_t p = perm[i] < 0 ? perm[i] + static_cast<int64_t>(perm.size()) : perm[i];
        inverse.at(static_cast<size_t>(p)) = static_cast<int64_t>(i);
    }
    return ctx.emit(
        "Transpose",
        {ctx.input(0)},
        {Attribute::makeInts("perm", std::move(inverse))});
}

/// sgd on one parameter tree: every tensor p becomes p - lr * g; other leaves are kept.
Value sgdTree(Emitter &e, const Value &p, const Value &g, const std::string &lr) {
    if (p.isAggregate() != g.isAggregate() ||
        (p.isAggregate() && p.fields.size() != g.fields.size())) {
        throw ExportError("nn:sgd: the parameters and gradients differ in structure");
    }
    if (p.isAggregate()) {
        std::vector<Value> fields;
        for (size_t i = 0; i < p.fields.size(); ++i) {
            fields.push_back(sgdTree(e, p.fields[i], g.fields[i], lr));
        }
        return Value::aggregate(p.camelType, std::move(fields));
    }
    const bool scalar = p.isConstant() ? !tensor::asTensorType(p.ty) : p.form == Value::Form::Scalar;
    if (scalar) {
        // A number in the tree (e.g. a learned scale) is updated the same way.
        type::Type *ty = p.isConstant() ? p.ty : p.camelType;
        return Value::symbolicScalar(
            e.node(
                "Sub",
                {e.operand(p, TypeCode::Float32),
                 e.node("Mul", {e.operand(g, TypeCode::Float32), lr})}),
            ty);
    }
    const TensorFacts facts = factsOf(p);
    const std::string step  = e.node("Mul", {e.operand(g, facts.dtype), lr});
    return Value::symbolic(
        e.node("Sub", {e.operand(p, facts.dtype), step}),
        facts.dtype,
        facts.shape);
}

// sgd(params, grads, lr)
Value lowerSgd(const LowerContext &ctx) {
    Emitter &e           = ctx.emitter();
    const std::string lr = floatConst(e, ctx.constNumber(2, 0.0));
    return sgdTree(e, ctx.arg(0), ctx.arg(1), lr);
}

} // namespace

void addGradientLowerings(const std::function<void(std::string, LowerFn, int64_t)> &add) {
    add("tensor:sum_to", lowerSumTo, 13);
    add("tensor:broadcast_like", lowerBroadcastLike, 8);
    add("tensor:zeros_like", [](const LowerContext &ctx) { return fillLike(ctx, 0.0); }, 8);
    add("tensor:ones_like", [](const LowerContext &ctx) { return fillLike(ctx, 1.0); }, 8);
    add("tensor:expand_axis", lowerExpandAxis, 13);
    add("tensor:gelu_grad", lowerGeluGrad, 1);
    add("tensor:erf_grad", lowerErfGrad, 1);
    add("tensor:layer_norm_grad", lowerLayerNormGrad, 13);
    add("tensor:slice_grad", lowerSliceGrad, 11);
    add("tensor:softmax_grad", lowerSoftmaxGrad, 13);
    add("tensor:matmul_grad_lhs", lowerMatmulGrad<true>, 13);
    add("tensor:matmul_grad_rhs", lowerMatmulGrad<false>, 13);
    add("tensor:cast_like", lowerCastLike, 1);
    add("tensor:permute_inverse", lowerPermuteInverse, 1);
    add("nn:sgd", lowerSgd, 1);
}

} // namespace camel::onnx
