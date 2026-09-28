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
 * Linear algebra operators: `matmul` (the `@` operator, NumPy semantics) and
 * `linear` (x @ weight + bias with weight laid out [in, out]).
 */

#include "../kernels/gemm.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

slot_t matmulKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("matmul", [&] {
        return wrap(k::matmul(tensorArg(norm, 0), tensorArg(norm, 1), resultAllocator()));
    });
}

int64_t agree(int64_t a, int64_t b, const char *what) {
    if (a == kUnknownDim) {
        return b;
    }
    if (b != kUnknownDim && a != b) {
        throw ShapeError(std::string(what) + ": " + std::to_string(a) + " vs " + std::to_string(b));
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
    const int64_t K  = lhs->back();
    const int64_t rK = rv ? (*rhs)[0] : (*rhs)[rhs->size() - 2];
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
        TensorObject *bias = hasArg(norm, 2) ? tensorArg(norm, 2) : nullptr;
        return wrap(k::linear(tensorArg(norm, 0), tensorArg(norm, 1), bias, resultAllocator()));
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
            x.shape ? std::optional(StaticShape(x.shape->size(), kUnknownDim)) : std::nullopt);
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

} // namespace

std::vector<OpDef> linalgOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "matmul",
        .exports   = {"__mat__", "matmul"},
        .params    = {{"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        .resultDoc = "Tensor",
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
        .resultDoc = "Tensor",
        .infer     = linearInfer,
        .kernel    = &linearKernel,
        .traits    = {}});
    return defs;
}

} // namespace camel::tensor::ops
