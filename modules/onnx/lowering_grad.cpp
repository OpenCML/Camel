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
 * Updated: Oct. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Lowerings of the operators that reverse-mode differentiation emits
 * (tensor:sum_to, tensor:matmul_grad_lhs, nn:conv2d_kernel_grad, ...) and of
 * the tree optimizers nn:sgd and nn:adam, so that a training step exports as
 * one ONNX graph. Most of them decompose into several ONNX nodes. They need
 * the static ranks of their operands, which shape specialization provides.
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
std::string
sumTo(const LowerContext &ctx, const std::string &g, const TensorFacts &gFacts, size_t likeIndex) {
    const auto gShape    = staticShape(gFacts, ctx.uri());
    const auto likeShape = staticShape(ctx.facts(likeIndex), ctx.uri());
    if (likeShape.size() > gShape.size()) {
        throw ExportError(
            std::format("{}: the gradient has a lower rank than its target", ctx.uri()));
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
    const std::string t = e.node("Tanh", {inner});
    const std::string left =
        e.node("Mul", {e.node("Add", {t, floatConst(e, 1.0)}), floatConst(e, 0.5)});
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
    Emitter &e           = ctx.emitter();
    const int64_t which  = ctx.constInt(3);
    const auto rank      = static_cast<int64_t>(ctx.rank(0));
    const std::string dy = ctx.input(2, TypeCode::Float32);
    std::vector<int64_t> lead;
    for (int64_t i = 0; i + 1 < rank; ++i) {
        lead.push_back(i);
    }
    if (which == 2) {
        return ctx.result(lead.empty() ? dy : reduce(e, "ReduceSum", dy, lead, false));
    }
    const std::string x   = ctx.input(0, TypeCode::Float32);
    const std::string xc  = e.node("Sub", {x, reduce(e, "ReduceMean", x, {-1}, true)});
    const std::string var = reduce(e, "ReduceMean", e.node("Mul", {xc, xc}), {-1}, true);
    const std::string inv = e.node(
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
    Emitter &e            = ctx.emitter();
    const std::string y   = ctx.input(0, TypeCode::Float32);
    const std::string dy  = ctx.input(1, TypeCode::Float32);
    const int64_t axis    = ctx.constInt(2, -1);
    const std::string dot = reduce(e, "ReduceSum", e.node("Mul", {y, dy}), {axis}, true);
    return ctx.emit("Mul", {y, e.node("Sub", {dy, dot})});
}

// slice_grad(dy, like, axis, start, end, step = 1): dy padded with zeros back to like's shape.
Value lowerSliceGrad(const LowerContext &ctx) {
    const auto likeShape = staticShape(ctx.facts(1), ctx.uri());
    const auto rank      = static_cast<int64_t>(likeShape.size());
    int64_t axis         = ctx.constInt(2);
    axis                 = axis < 0 ? axis + rank : axis;
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
    Emitter &e          = ctx.emitter();
    const auto dyShape  = staticShape(ctx.facts(0), ctx.uri());
    const auto lhsShape = staticShape(ctx.facts(1), ctx.uri());
    const auto rhsShape = staticShape(ctx.facts(2), ctx.uri());
    if (lhsShape.size() < 2 || rhsShape.size() < 2) {
        throw ExportError(std::format("{}: vector operands cannot be exported", ctx.uri()));
    }
    const auto dtype     = ctx.result().dtype;
    const std::string dy = ctx.input(0, dtype);
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
    return ctx.emit("Transpose", {ctx.input(0)}, {Attribute::makeInts("perm", std::move(inverse))});
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
    const bool scalar =
        p.isConstant() ? !tensor::asTensorType(p.ty) : p.form == Value::Form::Scalar;
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

std::string int64Const(Emitter &e, int64_t v) {
    return e.operand(
        Value::constant(rtdata::toSlot(static_cast<rtdata::Int64>(v)), type::Type::Int64()));
}

/// A float64 constant operand (the Adam update computes in float64, like the runtime kernel).
std::string doubleConst(Emitter &e, double v) {
    return e.operand(
        Value::constant(rtdata::toSlot(static_cast<rtdata::Float64>(v)), type::Type::Float64()),
        TypeCode::Float64);
}

/// The graph values one Adam step applies at every leaf: the hyper-parameters and the bias
/// corrections 1 - beta^t' (rank-0 float64 tensors broadcast against every leaf shape).
struct AdamScalars {
    std::string lr, beta1, beta2, oneMinusBeta1, oneMinusBeta2, eps;
    std::string correct1, correct2;
};

/// One leaf triple of an Adam step: the updated parameter and the two carried moments.
struct AdamUpdate {
    Value p, m, v;
};

/// Casts the float64 node `name` (shape `shape`) back to float32, the dtype the parameters,
/// gradients and moments are carried in.
Value castToF32(Emitter &e, const std::string &name, const tensor::StaticShape &shape) {
    const Value f64 = Value::symbolic(name, TypeCode::Float64, shape);
    return Value::symbolic(e.operand(f64, TypeCode::Float32), TypeCode::Float32, shape);
}

/// adam on one parameter tree: every tensor p becomes
/// p - lr * (m'/correct1) / (sqrt(v'/correct2) + eps) with m' = beta1*m + (1-beta1)*g and
/// v' = beta2*v + (1-beta2)*g^2. The runtime kernel computes each leaf in float64 and casts the
/// results to float32; the graph mirrors it (f32 Pow would misround the bias corrections —
/// 1 - beta2^t amplifies the beta2 rounding by the exponent).
AdamUpdate adamTree(
    Emitter &e, const Value &p, const Value &g, const Value &m, const Value &v,
    const AdamScalars &s) {
    const bool mismatched = p.isAggregate() != g.isAggregate() ||
                            p.isAggregate() != m.isAggregate() ||
                            p.isAggregate() != v.isAggregate() ||
                            (p.isAggregate() && (p.fields.size() != g.fields.size() ||
                                                 p.fields.size() != m.fields.size() ||
                                                 p.fields.size() != v.fields.size()));
    if (mismatched) {
        throw ExportError("nn:adam: the parameters, gradients and moments differ in structure");
    }
    if (p.isAggregate()) {
        std::vector<Value> ps, ms, vs;
        ps.reserve(p.fields.size());
        ms.reserve(p.fields.size());
        vs.reserve(p.fields.size());
        for (size_t i = 0; i < p.fields.size(); ++i) {
            AdamUpdate u = adamTree(e, p.fields[i], g.fields[i], m.fields[i], v.fields[i], s);
            ps.push_back(std::move(u.p));
            ms.push_back(std::move(u.m));
            vs.push_back(std::move(u.v));
        }
        return {
            Value::aggregate(p.camelType, std::move(ps)),
            Value::aggregate(m.camelType, std::move(ms)),
            Value::aggregate(v.camelType, std::move(vs))};
    }
    const bool scalar =
        p.isConstant() ? !tensor::asTensorType(p.ty) : p.form == Value::Form::Scalar;
    if (scalar) {
        throw ExportError("nn:adam: a non-tensor leaf of the parameters cannot be exported");
    }
    const std::string pp = e.operand(p, TypeCode::Float64);
    const std::string gg = e.operand(g, TypeCode::Float64);
    const std::string mm = e.operand(m, TypeCode::Float64);
    const std::string vv = e.operand(v, TypeCode::Float64);
    const std::string nm =
        e.node("Add", {e.node("Mul", {s.beta1, mm}), e.node("Mul", {s.oneMinusBeta1, gg})});
    const std::string nv = e.node(
        "Add",
        {e.node("Mul", {s.beta2, vv}), e.node("Mul", {s.oneMinusBeta2, e.node("Mul", {gg, gg})})});
    const std::string denom =
        e.node("Add", {e.node("Sqrt", {e.node("Div", {nv, s.correct2})}), s.eps});
    const std::string np = e.node(
        "Sub",
        {pp, e.node("Mul", {s.lr, e.node("Div", {e.node("Div", {nm, s.correct1}), denom})})});
    const TensorFacts facts = factsOf(p);
    if (!facts.shape) {
        throw ExportError("nn:adam: the shape of a parameter leaf is not known statically");
    }
    return {
        castToF32(e, np, *facts.shape),
        castToF32(e, nm, *facts.shape),
        castToF32(e, nv, *facts.shape)};
}

// adam(params, grads, state, lr, beta1 = 0.9, beta2 = 0.999, eps = 1e-8): the optimizer state
// (m, v, step t) travels as extra graph inputs and outputs; t' = t + 1 is computed in the graph
// (Add) and the bias corrections as Pow/Sub on it — the same pure single-step pattern as nn:sgd.
Value lowerAdam(const LowerContext &ctx) {
    Emitter &e         = ctx.emitter();
    const double beta1 = ctx.constNumber(4, 0.9);
    const double beta2 = ctx.constNumber(5, 0.999);
    AdamScalars s{
        .lr            = doubleConst(e, ctx.constNumber(3, 0.0)),
        .beta1         = doubleConst(e, beta1),
        .beta2         = doubleConst(e, beta2),
        .oneMinusBeta1 = doubleConst(e, 1.0 - beta1),
        .oneMinusBeta2 = doubleConst(e, 1.0 - beta2),
        .eps           = doubleConst(e, ctx.constNumber(6, 1e-8))};
    const auto [m, v, t] = optimizerStateParts(ctx.arg(2));
    Value tNext          = Value::symbolicScalar(
        e.node("Add", {e.operand(t, TypeCode::Int64), int64Const(e, 1)}),
        type::Type::Int64());
    const std::string tf   = e.operand(tNext, TypeCode::Float64);
    s.correct1             = e.node("Sub", {doubleConst(e, 1.0), e.node("Pow", {s.beta1, tf})});
    s.correct2             = e.node("Sub", {doubleConst(e, 1.0), e.node("Pow", {s.beta2, tf})});
    AdamUpdate u           = adamTree(e, ctx.arg(0), ctx.arg(1), m, v, s);
    Value state            = optimizerStateValue(std::move(u.m), std::move(u.v), std::move(tNext));
    type::Type *resultType = ctx.resultType();
    if (!resultType || resultType->code() != TypeCode::Tuple) {
        resultType = type::TupleType::create({inferenceTypeOf(u.p), state.camelType});
    }
    return Value::aggregate(resultType, {std::move(u.p), std::move(state)});
}

/// A zero tree shaped like `p`: the moments of a fresh Adam state.
Value zeroTree(Emitter &e, const Value &p) {
    if (p.isAggregate()) {
        std::vector<Value> fields;
        fields.reserve(p.fields.size());
        for (const Value &field : p.fields) {
            fields.push_back(zeroTree(e, field));
        }
        return Value::aggregate(p.camelType, std::move(fields));
    }
    const bool scalar =
        p.isConstant() ? !tensor::asTensorType(p.ty) : p.form == Value::Form::Scalar;
    if (scalar) {
        type::Type *ty = p.isConstant() ? p.ty : p.camelType;
        return Value::constant(
            ty->code() == TypeCode::Float32 ? rtdata::toSlot(static_cast<float>(0))
                                            : rtdata::toSlot(static_cast<rtdata::Float64>(0)),
            ty);
    }
    const TensorFacts facts = factsOf(p);
    const std::string zero  = e.operand(
        Value::constant(rtdata::toSlot(static_cast<rtdata::Float64>(0)), type::Type::Float64()),
        facts.dtype);
    const std::string shape = facts.shape && fullyKnown(*facts.shape)
                                  ? e.int64s(*facts.shape)
                                  : e.node("Shape", {e.operand(p)});
    return Value::symbolic(e.node("Expand", {zero, shape}), facts.dtype, facts.shape);
}

// adam_state(params): a state of zero moments and step 0, built in the graph. (With constant
// parameters the state folds away at simplification instead of reaching this lowering.)
Value lowerAdamState(const LowerContext &ctx) {
    Emitter &e = ctx.emitter();
    Value t = Value::constant(rtdata::toSlot(static_cast<rtdata::Int64>(0)), type::Type::Int64());
    return optimizerStateValue(zeroTree(e, ctx.arg(0)), zeroTree(e, ctx.arg(0)), std::move(t));
}

/// The static extent of a spatial axis, or an export error naming the operator.
int64_t staticExtent(
    const tensor::StaticShape &shape, size_t axis, std::string_view uri, std::string_view what) {
    const int64_t d = shape[axis];
    if (d == tensor::kUnknownDim) {
        throw ExportError(std::format("{}: the {} must be known statically", uri, what));
    }
    return d;
}

/// (input, kernel) shapes shared by the three conv2d gradient operators.
std::pair<tensor::StaticShape, tensor::StaticShape> convShapes(const LowerContext &ctx) {
    auto x = staticShape(ctx.facts(0), ctx.uri());
    auto w = staticShape(ctx.facts(1), ctx.uri());
    if (x.size() != 4 || w.size() != 4) {
        throw ExportError(std::format("{}: expects NCHW input and OCHW kernel", ctx.uri()));
    }
    return {std::move(x), std::move(w)};
}

// conv2d_input_grad(x, w, dy, stride = 1, padding = 0): dL/dx is the adjoint of the forward
// cross-correlation, which is exactly what ConvTranspose computes (its weight layout [O, C, KH,
// KW] matches Camel's kernel). The forward stride drops a remainder r = (H + 2p - KH) mod s per
// axis; ConvTranspose's output_padding puts it back: s * (OH - 1) + r + KH - 2p = H.
Value lowerConv2dInputGrad(const LowerContext &ctx) {
    const auto [x, w]     = convShapes(ctx);
    const int64_t stride  = ctx.constInt(3, 1);
    const int64_t padding = ctx.constInt(4, 0);
    const int64_t rH      = (staticExtent(x, 2, ctx.uri(), "input height") + 2 * padding -
                             staticExtent(w, 2, ctx.uri(), "kernel height")) %
                            stride;
    const int64_t rW      = (staticExtent(x, 3, ctx.uri(), "input width") + 2 * padding -
                             staticExtent(w, 3, ctx.uri(), "kernel width")) %
                            stride;
    return ctx.emit(
        "ConvTranspose",
        {ctx.input(2, TypeCode::Float32), ctx.input(1, TypeCode::Float32)},
        {Attribute::makeInts("strides", {stride, stride}),
         Attribute::makeInts("pads", {padding, padding, padding, padding}),
         Attribute::makeInts("output_padding", {rH, rW})});
}

// conv2d_kernel_grad(x, w, dy, stride = 1, padding = 0):
// dW[o,c,kh,kw] = sum_{n,oh,ow} dy[n,o,oh,ow] * x[n,c,oh*s+kh-p, ow*s+kw-p].
// That is a correlation of x with dy, which a Conv computes when batch and channel swap roles:
// input x^T [C,N,H,W], kernel dy^T [O,N,OH,OW], unit stride, the forward stride becoming the
// dilation, and the forward padding kept. The output [C,O,KH',KW'] has KH' = H + 2p - (OH-1)*s
// in [KH, KH+s); a Slice crops the s-1 surplus rows/columns a strided forward leaves over.
Value lowerConv2dKernelGrad(const LowerContext &ctx) {
    Emitter &e            = ctx.emitter();
    const auto [x, w]     = convShapes(ctx);
    const int64_t stride  = ctx.constInt(3, 1);
    const int64_t padding = ctx.constInt(4, 0);
    const int64_t perm[]  = {1, 0, 2, 3};
    const std::string xt  = e.node(
        "Transpose",
        {ctx.input(0, TypeCode::Float32)},
        {Attribute::makeInts("perm", std::vector<int64_t>(perm, perm + 4))});
    const std::string dt = e.node(
        "Transpose",
        {ctx.input(2, TypeCode::Float32)},
        {Attribute::makeInts("perm", std::vector<int64_t>(perm, perm + 4))});
    std::string dw = e.node(
        "Conv",
        {xt, dt},
        {Attribute::makeInts("strides", {1, 1}),
         Attribute::makeInts("dilations", {stride, stride}),
         Attribute::makeInts("pads", {padding, padding, padding, padding})});
    dw = e.node(
        "Transpose",
        {dw},
        {Attribute::makeInts("perm", std::vector<int64_t>(perm, perm + 4))});
    const int64_t KH = staticExtent(w, 2, ctx.uri(), "kernel height");
    const int64_t KW = staticExtent(w, 3, ctx.uri(), "kernel width");
    // The crop is a no-op when the extents work out exactly (e.g. stride 1); emit it only when
    // static shapes cannot prove that, or prove a surplus.
    bool exact = false;
    if (x[2] != tensor::kUnknownDim && x[3] != tensor::kUnknownDim) {
        const int64_t OH = (x[2] + 2 * padding - KH) / stride + 1;
        const int64_t OW = (x[3] + 2 * padding - KW) / stride + 1;
        exact            = x[2] + 2 * padding - (OH - 1) * stride == KH &&
                           x[3] + 2 * padding - (OW - 1) * stride == KW;
    }
    if (!exact) {
        const int64_t starts[] = {0, 0}, ends[] = {KH, KW}, axes[] = {2, 3};
        dw = e.node("Slice", {dw, e.int64s(starts), e.int64s(ends), e.int64s(axes)});
    }
    return ctx.result(dw);
}

// conv2d_bias_grad(bias, dy): dL/db sums dy over the batch and both spatial axes.
Value lowerConv2dBiasGrad(const LowerContext &ctx) {
    if (ctx.rank(1) != 4) {
        throw ExportError(std::format("{}: expects a rank-4 dy", ctx.uri()));
    }
    const std::string dy = ctx.input(1, TypeCode::Float32);
    return ctx.result(reduce(ctx.emitter(), "ReduceSum", dy, {0, 2, 3}, false));
}

// max_pool2d_grad(x, dy, kernel, stride = kernel, padding = 0): each pooled gradient goes to
// the argmax of its window. MaxPool's indices output holds those argmax positions (flat over
// the whole unpadded input, first maximum on ties — the runtime kernel's rule) and MaxUnpool
// scatters dy at them, with output_size restoring the input shape. MaxUnpool assigns rather
// than accumulates, so windows must be disjoint (kernel <= stride) for the exported graph to
// match: with overlapping windows a cell shared by two windows would keep only one gradient.
Value lowerMaxPool2dGrad(const LowerContext &ctx) {
    Emitter &e            = ctx.emitter();
    const int64_t kernel  = ctx.constInt(2);
    const int64_t stride  = ctx.constInt(3, kernel);
    const int64_t padding = ctx.constInt(4, 0);
    if (kernel > stride) {
        throw ExportError(
            std::format(
                "{}: overlapping windows (kernel {} > stride {}) cannot be exported exactly",
                ctx.uri(),
                kernel,
                stride));
    }
    const std::vector<Attribute> window{
        Attribute::makeInts("kernel_shape", {kernel, kernel}),
        Attribute::makeInts("strides", {stride, stride}),
        Attribute::makeInts("pads", {padding, padding, padding, padding})};
    const auto [pooled, indices] =
        e.node2("MaxPool", {ctx.input(0, TypeCode::Float32)}, window, ElemType::Int64);
    (void)pooled; // only the indices feed MaxUnpool
    return ctx.emit(
        "MaxUnpool",
        {ctx.input(1, TypeCode::Float32), indices, shapeOperand(ctx, 0)},
        window);
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
    add("nn:adam", lowerAdam, 7);
    add("nn:adam_state", lowerAdamState, 8);
    add("nn:conv2d_input_grad", lowerConv2dInputGrad, 1);
    add("nn:conv2d_kernel_grad", lowerConv2dKernelGrad, 10);
    add("nn:conv2d_bias_grad", lowerConv2dBiasGrad, 1);
    add("nn:max_pool2d_grad", lowerMaxPool2dGrad, 11);
    // stop_gradient is the identity at run time; its meaning lives in differentiation, which
    // the export has already resolved by the time the operator is seen.
    add("autodiff:stop_gradient", [](const LowerContext &ctx) { return ctx.arg(0); }, 1);
}

} // namespace camel::onnx
