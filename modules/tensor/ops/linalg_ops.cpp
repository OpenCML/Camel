// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Linear algebra operators: `matmul` (the `@` operator, NumPy semantics) and
 * `linear` (x @ weight + bias with weight laid out [in, out]).
 */

#include "../attrs.h"
#include "../kernels/gemm.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

/// `matmul` runs the canonical algorithm unless the node's `tensor.kernel` attribute (written by
/// tensor::fuse, design principles 9.6/7.3) says it runs on the library GEMM. The operator is
/// the same either way: the attribute is the implementation choice.
slot_t matmulKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("matmul", [&] {
        return wrap(norm.resultType(), k::matmul(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            resultAllocator(),
            usesLibraryGemm(norm.attrs())));
    });
}

std::optional<int64_t> agree(std::optional<int64_t> a, std::optional<int64_t> b, const char *what) {
    if (!a) {
        return b;
    }
    if (b && a != b) {
        throw ShapeError(std::string(what) + ": " + std::to_string(*a) + " vs " + std::to_string(*b));
    }
    return a;
}

/// matmul shape rule on possibly unknown extents (mirrors kernels::matmulShape).
std::optional<StaticShape>
matmulStaticShape(const std::optional<StaticShape> &lhs, const std::optional<StaticShape> &rhs) {
    if (!lhs || !rhs) {
        return std::nullopt;
    }
    if (lhs->empty() || rhs->empty()) {
        throw std::invalid_argument("matmul does not accept rank-0 tensors");
    }
    const bool lv = lhs->size() == 1, rv = rhs->size() == 1;
    const std::optional<int64_t> K  = lhs->back();
    const std::optional<int64_t> rK = rv ? (*rhs)[0] : (*rhs)[rhs->size() - 2];
    agree(K, rK, "matmul inner dimensions differ");
    StaticShape lb(lhs->begin(), lhs->end() - (lv ? 1 : 2));
    StaticShape rb(rhs->begin(), rhs->end() - (rv ? 1 : 2));
    StaticShape out = *broadcast(lb, rb);
    if (!lv) {
        out.push_back((*lhs)[lhs->size() - 2]);
    }
    if (!rv) {
        out.push_back(rhs->back());
    }
    return out;
}

/// A product of two arrays is computed in the one element type both of them state: an f64 array
/// beside an f32 one matches no arm and is written with the conversion (refuseMixedElementTypes,
/// design principles 5.1). The shape rule below is the one the kernel applies.
std::optional<Type *> matmulInfer(const InferContext &ctx) {
    const TensorFacts a = ctx.facts(0), b = ctx.facts(1);
    refuseMixedElementTypes(a.dtype, b.dtype);
    return tensorOf(commonElementType(a.dtype, b.dtype), matmulStaticShape(a.shape, b.shape));
}

slot_t linearKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("linear", [&] {
        Array *bias = hasArg(norm, 2) ? tensorArg(norm, 2) : nullptr;
        return wrap(
            norm.resultType(),
            k::linear(tensorArg(norm, 0), tensorArg(norm, 1), bias, resultAllocator()));
    });
}

std::optional<Type *> linearInfer(const InferContext &ctx) {
    const TensorFacts x = ctx.facts(0), w = ctx.facts(1);
    // `x @ w + bias` computes in the element type the value and the weight state, and the two of
    // them and the bias are the same type: a mixed pair matches no arm here either.
    refuseMixedElementTypes(x.dtype, w.dtype);
    if (ctx.has(2)) {
        refuseMixedElementTypes(x.dtype, ctx.facts(2).dtype);
    }
    auto dtype = commonElementType(x.dtype, w.dtype);
    if (w.shape && w.shape->size() != 2) {
        return std::nullopt;
    }
    if (!x.shape || !w.shape) {
        return tensorOf(
            dtype,
            x.shape ? std::optional(StaticShape(x.shape->size(), std::nullopt)) : std::nullopt);
    }
    if (x.shape->empty()) {
        return std::nullopt;
    }
    agree(x.shape->back(), (*w.shape)[0], "linear input features vs weight rows");
    if (ctx.has(2)) {
        const TensorFacts b = ctx.facts(2);
        if (b.shape) {
            if (b.shape->size() != 1) {
                throw ShapeError("linear expects a rank-1 bias");
            }
            agree((*b.shape)[0], (*w.shape)[1], "linear bias vs weight columns");
        }
    }
    StaticShape out = *x.shape;
    out.back()      = (*w.shape)[1];
    return tensorOf(dtype, out);
}

void matmulVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        accumulateMatmulGradients(b, call.inputs[0], call.inputs[1], *dy, call.attrs);
    }
}

// `matmul_stated(a, b, sa, sb)`: the product with both operands' extents stated beside it.
//
// The value is `matmul(a, b)` -- `matmulKernel` is the operator's own, so the kernel choice, the
// accumulation and the shape check are the definition's own -- and each list is checked against the
// shape of the operand it belongs to. Nothing at run time reads the lists to compute the value:
// they are there for the reverse law, which reduces each cotangent to its own operand's shape and has
// no value of that operand to read it from (`stdlib/rulesets/transpose.cml#matmul_stated_t`).
//
// Two lists and not one: the two operands are different shapes (the answer is neither of them), and
// each cotangent comes back to its own. The element type of the lists is the same `int[]` the stated
// reductions use.
slot_t matmulStatedKernel(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    const std::vector<int64_t> sa = intListArg(norm, 2, "matmul_stated");
    const std::vector<int64_t> sb = intListArg(norm, 3, "matmul_stated");
    checkStatedExtents(tensorArg(norm, 0), sa, 0, "matmul_stated");
    checkStatedExtents(tensorArg(norm, 1), sb, 0, "matmul_stated");
    return matmulKernel(with, norm, ctx);
}

// linear(x, weight, bias?) = x @ weight + bias.
void linearVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 3);
    if (auto dy = b.gradientOf(call.output)) {
        accumulateMatmulGradients(b, call.inputs[0], call.inputs[1], *dy, call.attrs);
        if (call.inputs.size() > 2) {
            accumulateOperand(b, call.inputs[2], *dy);
        }
    }
}

/// matmul_add / matmul_add_relu are gone with the fused operators they served: tensor::fuse
/// keeps the program's operators and records the group on their nodes (design principles 9.6),
/// so the composition's derivative rules are the matmul, add and relu rules the graph already
/// has (see autodiff's fused_grad_check case).

/// `matmul_stated`'s pullback: the product's own, because its value is the product's.
///
/// The two lists are operands the engine does not need: it holds both operands' **values**, which is
/// what the lists stand in for on the library side, where the operand a cotangent is reduced to is a
/// tangent and a tangent's type states no extents. So the engine's answer and the library's law
/// (`stdlib/rulesets/transpose.cml#matmul_stated_t`) are the same two products and the same two
/// reductions, and the table comparison `scripts/vjp-parity.mjs` sees a law with a rule behind it.
void matmulStatedVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 4, 4);
    if (auto dy = b.gradientOf(call.output)) {
        accumulateMatmulGradients(b, call.inputs[0], call.inputs[1], *dy, call.attrs);
    }
}

} // namespace

std::vector<OpDef> linalgOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "matmul",
        .exports   = {"__mat__", "matmul"},
        .params    = {{"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        .resultDoc = "float[...]",
        .infer     = matmulInfer,
        .kernel    = &matmulKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name    = "linear",
        .exports = {"linear"},
        .params =
            {{"x", ParamKind::Tensor},
             {"weight", ParamKind::Tensor},
             {"bias", ParamKind::Tensor, true}},
        .resultDoc = "float[...]",
        .infer     = linearInfer,
        .kernel    = &linearKernel,
        .traits    = {}});
    setVjp(defs, "matmul", &matmulVjp);
    setVjp(defs, "linear", &linearVjp);
    // The stated twin of the product above: the same value, with the two operands' extents as two
    // more operands (`matmulStatedKernel`). Declared in `stdlib/tensor.cml`.
    defs.push_back(OpDef{
        .name      = "matmul_stated",
        .exports   = {"matmul_stated"},
        .params    = {{"a", ParamKind::Tensor},
                      {"b", ParamKind::Tensor},
                      {"sa", ParamKind::IntArray},
                      {"sb", ParamKind::IntArray}},
        .resultDoc = "float[...]",
        .infer     = matmulInfer,
        .kernel    = &matmulStatedKernel,
        .traits    = {}});
    setVjp(defs, "matmul_stated", &matmulStatedVjp);
    return defs;
}

} // namespace camel::tensor::ops
