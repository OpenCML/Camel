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

std::optional<Type *> matmulInfer(const InferContext &ctx) {
    const TensorFacts a = ctx.facts(0), b = ctx.facts(1);
    return tensorOf(promote(a.dtype, b.dtype), matmulStaticShape(a.shape, b.shape));
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
    auto dtype = promote(x.dtype, w.dtype);
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

} // namespace

std::vector<OpDef> linalgOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "matmul",
        .exports   = {"__mat__", "matmul"},
        .params    = {{"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        .resultDoc = "float[..]",
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
        .resultDoc = "float[..]",
        .infer     = linearInfer,
        .kernel    = &linearKernel,
        .traits    = {}});
    setVjp(defs, "matmul", &matmulVjp);
    setVjp(defs, "linear", &linearVjp);
    return defs;
}

} // namespace camel::tensor::ops
