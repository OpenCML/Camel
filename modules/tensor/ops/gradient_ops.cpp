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
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Operators serving differentiation: the gradient-shaped counterparts of
 * forward operators that derivative rules emit, the tangent space of tensors,
 * and a few shape queries that are useful on their own (zeros_like,
 * ones_like, numel, dim).
 *
 * The internal operators have no source-level name; rules refer to them by
 * URI, like the fused operators of tensor::fuse.
 */

#include "../kernels/elementwise.h"
#include "../kernels/gradient.h"
#include "../kernels/layout.h"
#include "../type.h"
#include "camel/core/derivative.h"
#include "catalog.h"
#include "support.h"

namespace camel::tensor::ops {

using namespace camel::core::type;
namespace k = camel::tensor::kernels;

namespace {

TensorObject *likeArg(ArgsView &norm, size_t index) { return tensorArg(norm, index); }

slot_t zerosLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("zeros_like", [&] {
        const TensorObject *t = tensorArg(norm, 0);
        return wrap(k::full(t->dtype(), t->shapeSpan(), 0.0, resultAllocator()));
    });
}

slot_t onesLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("ones_like", [&] {
        const TensorObject *t = tensorArg(norm, 0);
        return wrap(k::full(t->dtype(), t->shapeSpan(), 1.0, resultAllocator()));
    });
}

slot_t numelKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return camel::core::rtdata::toSlot(static_cast<int64_t>(tensorArg(norm, 0)->numel()));
}

slot_t dimKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("dim", [&] {
        const TensorObject *t = tensorArg(norm, 0);
        const auto rank       = static_cast<int64_t>(t->rank());
        int64_t axis          = intArg(norm, 1);
        axis                  = axis < 0 ? axis + rank : axis;
        if (axis < 0 || axis >= rank) {
            throw std::invalid_argument("dim axis out of range");
        }
        return camel::core::rtdata::toSlot(static_cast<int64_t>(t->dim(static_cast<size_t>(axis))));
    });
}

slot_t sumToKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("sum_to", [&] {
        return wrap(k::sumTo(tensorArg(norm, 0), likeArg(norm, 1)->shapeSpan(), resultAllocator()));
    });
}

slot_t broadcastLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("broadcast_like", [&] {
        const auto shape = likeArg(norm, 1)->shapeSpan();
        if (norm.type(0)->code() == TypeCode::Float64 ||
            norm.type(0)->code() == TypeCode::Float32) {
            return wrap(k::fill(numberArg(norm, 0), shape, resultAllocator()));
        }
        return wrap(k::broadcastTo(tensorArg(norm, 0), shape, resultAllocator()));
    });
}

slot_t expandAxisKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("expand_axis", [&] {
        return wrap(k::expandAxis(
            tensorArg(norm, 0),
            likeArg(norm, 1)->shapeSpan(),
            intArg(norm, 2),
            hasArg(norm, 3) && boolArg(norm, 3),
            resultAllocator()));
    });
}

slot_t geluGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("gelu_grad", [&] {
        return wrap(k::geluGrad(tensorArg(norm, 0), tensorArg(norm, 1), resultAllocator()));
    });
}

slot_t erfGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("erf_grad", [&] {
        return wrap(k::erfGrad(tensorArg(norm, 0), tensorArg(norm, 1), resultAllocator()));
    });
}

// layer_norm_grad(x, gamma, dy, which, eps?): which 0 = input, 1 = gamma, 2 = beta.
slot_t layerNormGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("layer_norm_grad", [&] {
        const int64_t which = intArg(norm, 3);
        if (which < 0 || which > 2) {
            throw std::invalid_argument("layer_norm_grad: unknown gradient selector");
        }
        return wrap(k::layerNormGrad(
            tensorArg(norm, 0),
            tensorArg(norm, 1),
            tensorArg(norm, 2),
            hasArg(norm, 4) ? numberArg(norm, 4) : 1e-5,
            static_cast<k::LayerNormGrad>(which),
            resultAllocator()));
    });
}

// slice_grad(dy, like, axis, start, end, step?)
slot_t sliceGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("slice_grad", [&] {
        return wrap(k::sliceGrad(
            tensorArg(norm, 0),
            likeArg(norm, 1)->shapeSpan(),
            intArg(norm, 2),
            intArg(norm, 3),
            intArg(norm, 4),
            hasArg(norm, 5) ? intArg(norm, 5) : 1,
            resultAllocator()));
    });
}

template <bool Lhs> slot_t matmulGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel(Lhs ? "matmul_grad_lhs" : "matmul_grad_rhs", [&] {
        const TensorObject *dy  = tensorArg(norm, 0);
        const TensorObject *lhs = tensorArg(norm, 1);
        const TensorObject *rhs = tensorArg(norm, 2);
        return wrap(
            Lhs ? k::matmulGradLhs(dy, lhs, rhs, resultAllocator())
                : k::matmulGradRhs(dy, lhs, rhs, resultAllocator()));
    });
}

slot_t castLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("cast_like", [&] {
        TensorObject *g          = tensorArg(norm, 0);
        const TensorObject *like = likeArg(norm, 1);
        return wrap(g->dtype() == like->dtype() ? g : k::cast(g, like->dtype(), resultAllocator()));
    });
}

slot_t permuteInverseKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("permute_inverse", [&] {
        const Shape perm = shapeArg(norm, 1);
        Shape inverse(perm.size(), 0);
        for (size_t i = 0; i < perm.size(); ++i) {
            const int64_t p = perm[i] < 0 ? perm[i] + static_cast<int64_t>(perm.size()) : perm[i];
            if (p < 0 || p >= static_cast<int64_t>(perm.size())) {
                throw std::invalid_argument("permute_inverse: axis out of range");
            }
            inverse[static_cast<size_t>(p)] = static_cast<int64_t>(i);
        }
        return wrap(k::permute(tensorArg(norm, 0), inverse, resultAllocator()));
    });
}

// ---------------------------------------------------------------- inference

std::optional<Type *> sameAsFirst(const InferContext &ctx) { return tensorOf(ctx.facts(0)); }

std::optional<Type *> intScalar(const InferContext &) { return Type::Int64(); }

/// A float gradient shaped like argument `index`.
InferFn gradientLike(size_t index) {
    return [index](const InferContext &ctx) -> std::optional<Type *> {
        return tensorOf(TypeCode::Float32, ctx.facts(index).shape);
    };
}

std::optional<Type *> layerNormGradInfer(const InferContext &ctx) {
    const auto which = ctx.constInt(3);
    if (!which) {
        return tensorOf(TypeCode::Float32, std::nullopt);
    }
    return tensorOf(TypeCode::Float32, ctx.facts(*which == 0 ? 0 : 1).shape);
}

std::optional<Type *> castLikeInfer(const InferContext &ctx) {
    return tensorOf(ctx.facts(1).dtype, ctx.facts(0).shape);
}

/// permute_inverse(g, perm): g's axes put back where `perm` took them from.
std::optional<Type *> permuteInverseInfer(const InferContext &ctx) {
    const auto shape = ctx.facts(0).shape;
    const auto perm  = ctx.constInts(1);
    if (!shape || !perm || perm->size() != shape->size()) {
        return tensorOf(TypeCode::Float32, std::nullopt);
    }
    StaticShape result(shape->size(), kUnknownDim);
    const auto rank = static_cast<int64_t>(perm->size());
    for (size_t i = 0; i < perm->size(); ++i) {
        const int64_t p = (*perm)[i] < 0 ? (*perm)[i] + rank : (*perm)[i];
        if (p < 0 || p >= rank) {
            return tensorOf(TypeCode::Float32, std::nullopt);
        }
        result[static_cast<size_t>(p)] = (*shape)[i];
    }
    return tensorOf(TypeCode::Float32, result);
}

OpDef internal(
    std::string_view name, std::vector<ParamSpec> params, InferFn infer, operator_t kernel) {
    return OpDef{
        .name      = name,
        .exports   = {},
        .params    = std::move(params),
        .resultDoc = "Tensor",
        .infer     = std::move(infer),
        .kernel    = kernel,
        .traits    = {}};
}

// ---------------------------------------------------------------- tangent space

Type *tensorTangentType(Type *primal) {
    const TensorType *tensor = asTensorType(primal);
    if (tensor == nullptr) {
        return nullptr;
    }
    if (auto dtype = tensor->dtype();
        dtype && *dtype != TypeCode::Float32 && *dtype != TypeCode::Float64) {
        return nullptr; // integer and boolean tensors have no gradient
    }
    return primal;
}

} // namespace

std::vector<OpDef> gradientOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "zeros_like",
        .exports   = {"zeros_like"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "Tensor",
        .infer     = sameAsFirst,
        .kernel    = &zerosLikeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "ones_like",
        .exports   = {"ones_like"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "Tensor",
        .infer     = sameAsFirst,
        .kernel    = &onesLikeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name          = "numel",
        .exports       = {"numel"},
        .params        = {{"t", ParamKind::Tensor}},
        .resultDoc     = "int",
        .infer         = intScalar,
        .kernel        = &numelKernel,
        .traits        = {},
        .foldFromTypes = [](const InferContext &ctx, mm::IAllocator &) -> std::optional<slot_t> {
            const auto shape = ctx.facts(0).shape;
            if (!shape) {
                return std::nullopt;
            }
            int64_t count = 1;
            for (int64_t d : *shape) {
                if (d == kUnknownDim) {
                    return std::nullopt;
                }
                count *= d;
            }
            return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Int64>(count));
        }});
    defs.push_back(OpDef{
        .name          = "dim",
        .exports       = {"dim"},
        .params        = {{"t", ParamKind::Tensor}, {"axis", ParamKind::Int}},
        .resultDoc     = "int",
        .infer         = intScalar,
        .kernel        = &dimKernel,
        .traits        = {},
        .foldFromTypes = [](const InferContext &ctx, mm::IAllocator &) -> std::optional<slot_t> {
            const auto shape = ctx.facts(0).shape;
            const auto axisArg = ctx.constInt(1);
            if (!shape || !axisArg) {
                return std::nullopt;
            }
            const auto rank = static_cast<int64_t>(shape->size());
            const int64_t axis = *axisArg < 0 ? *axisArg + rank : *axisArg;
            if (axis < 0 || axis >= rank || (*shape)[static_cast<size_t>(axis)] == kUnknownDim) {
                return std::nullopt;
            }
            return camel::core::rtdata::toSlot(
                static_cast<camel::core::rtdata::Int64>((*shape)[static_cast<size_t>(axis)]));
        }});

    defs.push_back(internal(
        "sum_to",
        {{"g", ParamKind::Tensor}, {"like", ParamKind::Tensor}},
        gradientLike(1),
        &sumToKernel));
    defs.push_back(internal(
        "broadcast_like",
        {{"g", ParamKind::TensorOrScalar}, {"like", ParamKind::Tensor}},
        gradientLike(1),
        &broadcastLikeKernel));
    defs.push_back(internal(
        "expand_axis",
        {{"g", ParamKind::Tensor},
         {"like", ParamKind::Tensor},
         {"axis", ParamKind::Int},
         {"keepdims", ParamKind::Bool, true}},
        gradientLike(1),
        &expandAxisKernel));
    defs.push_back(internal(
        "gelu_grad",
        {{"x", ParamKind::Tensor}, {"dy", ParamKind::Tensor}},
        gradientLike(0),
        &geluGradKernel));
    defs.push_back(internal(
        "erf_grad",
        {{"x", ParamKind::Tensor}, {"dy", ParamKind::Tensor}},
        gradientLike(0),
        &erfGradKernel));
    defs.push_back(internal(
        "layer_norm_grad",
        {{"x", ParamKind::Tensor},
         {"gamma", ParamKind::Tensor},
         {"dy", ParamKind::Tensor},
         {"which", ParamKind::Int},
         {"eps", ParamKind::Number, true}},
        layerNormGradInfer,
        &layerNormGradKernel));
    defs.push_back(internal(
        "slice_grad",
        {{"dy", ParamKind::Tensor},
         {"like", ParamKind::Tensor},
         {"axis", ParamKind::Int},
         {"start", ParamKind::Int},
         {"end", ParamKind::Int},
         {"step", ParamKind::Int, true}},
        gradientLike(1),
        &sliceGradKernel));
    defs.push_back(internal(
        "matmul_grad_lhs",
        {{"dy", ParamKind::Tensor}, {"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        gradientLike(1),
        &matmulGradKernel<true>));
    defs.push_back(internal(
        "matmul_grad_rhs",
        {{"dy", ParamKind::Tensor}, {"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        gradientLike(2),
        &matmulGradKernel<false>));
    defs.push_back(internal(
        "cast_like",
        {{"g", ParamKind::Tensor}, {"like", ParamKind::Tensor}},
        castLikeInfer,
        &castLikeKernel));
    defs.push_back(internal(
        "permute_inverse",
        {{"g", ParamKind::Tensor}, {"perm", ParamKind::IntArray}},
        permuteInverseInfer,
        &permuteInverseKernel));
    return defs;
}

void registerTensorTangentSpace() {
    camel::core::TangentSpace space;
    space.tangentType  = &tensorTangentType;
    space.addUri       = "tensor:add";
    space.zerosLikeUri = "tensor:zeros_like";
    camel::core::DerivativeRegistry::instance().setTangentSpace(TensorType::typeCode(), space);
}

} // namespace camel::tensor::ops
