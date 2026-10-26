// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Operators serving differentiation: the gradient-shaped counterparts of
 * forward operators that derivative rules emit, the tangent space of tensors,
 * and a few shape queries that are useful on their own (zeros_like,
 * ones_like, numel, dim).
 *
 * The internal operators have no source-level name; rules refer to them by
 * URI.
 */

#include "../attrs.h"
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

Array *likeArg(ArgsView &norm, size_t index) { return tensorArg(norm, index); }

slot_t zerosLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("zeros_like", [&] {
        const Array *t = tensorArg(norm, 0);
        return wrap(norm.resultType(), k::full(t->dtype(), t->shapeSpan(), 0.0, resultAllocator()));
    });
}

slot_t onesLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("ones_like", [&] {
        const Array *t = tensorArg(norm, 0);
        return wrap(norm.resultType(), k::full(t->dtype(), t->shapeSpan(), 1.0, resultAllocator()));
    });
}

slot_t numelKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return camel::core::rtdata::toSlot(static_cast<int64_t>(tensorArg(norm, 0, true)->numel()));
}

slot_t dimKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("dim", [&] {
        const Array *t = tensorArg(norm, 0, true);
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
        return wrap(
            norm.resultType(),
            k::sumTo(tensorArg(norm, 0), likeArg(norm, 1)->shapeSpan(), resultAllocator()));
    });
}

slot_t broadcastLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("broadcast_like", [&] {
        const Array *like = likeArg(norm, 1);
        // A scalar is spread at the element type of the value it is made like: the result is that
        // value's gradient, and it is read at the width that value's type names. Filling at any
        // other width puts a number of one width behind a type of another -- an f32 1.0 read as
        // f64 is 0.0078125, and a row of them is not the adjoint.
        const TypeCode dtype = like->dtype();
        if (norm.type(0)->code() == TypeCode::Float64 || norm.type(0)->code() == TypeCode::Float32) {
            return wrap(
                norm.resultType(),
                k::fill(numberArg(norm, 0), dtype, like->shapeSpan(), resultAllocator()));
        }
        return wrap(
            norm.resultType(),
            k::broadcastTo(tensorArg(norm, 0), like->shapeSpan(), resultAllocator()));
    });
}

slot_t expandAxisKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("expand_axis", [&] {
        return wrap(norm.resultType(), k::expandAxis(
            tensorArg(norm, 0),
            likeArg(norm, 1)->shapeSpan(),
            intArg(norm, 2),
            hasArg(norm, 3) && boolArg(norm, 3),
            resultAllocator()));
    });
}

slot_t geluGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("gelu_grad", [&] {
        return wrap(
            norm.resultType(),
            k::geluGrad(tensorArg(norm, 0), tensorArg(norm, 1), resultAllocator()));
    });
}

slot_t erfGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("erf_grad", [&] {
        return wrap(
            norm.resultType(),
            k::erfGrad(tensorArg(norm, 0), tensorArg(norm, 1), resultAllocator()));
    });
}

// layer_norm_grad(x, gamma, dy, which, eps?): which 0 = input, 1 = gamma, 2 = beta.
slot_t layerNormGradKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("layer_norm_grad", [&] {
        const int64_t which = intArg(norm, 3);
        if (which < 0 || which > 2) {
            throw std::invalid_argument("layer_norm_grad: unknown gradient selector");
        }
        return wrap(norm.resultType(), k::layerNormGrad(
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
        return wrap(norm.resultType(), k::sliceGrad(
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
    const bool useLibrary = usesLibraryGemm(norm.attrs());
    return runKernel(Lhs ? "matmul_grad_lhs" : "matmul_grad_rhs", [&] {
        const Array *dy  = tensorArg(norm, 0);
        const Array *lhs = tensorArg(norm, 1);
        const Array *rhs = tensorArg(norm, 2);
        return wrap(norm.resultType(),
            Lhs ? k::matmulGradLhs(dy, lhs, rhs, useLibrary, resultAllocator())
                : k::matmulGradRhs(dy, lhs, rhs, useLibrary, resultAllocator()));
    });
}

slot_t castLikeKernel(ArgsView &, ArgsView &norm, context::Context &) {
    return runKernel("cast_like", [&] {
        Array *g          = tensorArg(norm, 0);
        const Array *like = likeArg(norm, 1);
        return wrap(
            norm.resultType(),
            g->dtype() == like->dtype() ? g : k::cast(g, like->dtype(), resultAllocator()));
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
        return wrap(norm.resultType(), k::permute(tensorArg(norm, 0), inverse, resultAllocator()));
    });
}

// ---------------------------------------------------------------- inference

std::optional<Type *> sameAsFirst(const InferContext &ctx) { return tensorOf(ctx.facts(0)); }

std::optional<Type *> intScalar(const InferContext &) { return Type::Int64(); }

/// The signature of `sum_to`, `broadcast_like`, `expand_axis`, `gelu_grad`, `erf_grad`,
/// `slice_grad` and the two matmul gradients: `::<T>(g: T[..], like: T[..])`. The result is
/// argument `index`'s shape and element type, and the other argument is that same element type. A
/// gradient is read at the width of the value it is the gradient of (design principles 9.9): an
/// f32 fill read as f64 is 0.0078125, not the derivative.
///
/// These operators used to declare f32 whatever the value's own width was, from the time their
/// kernels were written for f32. An f64 training step then built `sum_to(<f32 gradient>, <f64
/// like>)` -- the mixed pair below -- the call was refused, and the node that fed it reached the
/// exporter with no rank facts (feat/modules/onnx, the train-step cases). The declaration follows
/// the value's own width instead, and a kernel that takes no other width says so at the call,
/// naming the operator, the argument type and the set it takes (feat.modules.tensor.dtype).
///
/// Two *tensors* whose element types disagree match no arm -- there is no conversion between
/// numeric types to reach one (design principles 5.1) -- so the call is refused by the signature,
/// not narrowed into an arm.
///
/// A *number* on the other side is not such a pair: a number has no element type of its own and is
/// read at the width of the tensor it meets, the same rule every elementwise operator follows
/// (`x * 0.5` with an f32 `x` computes at f32), and `index_eval.cpp` reads it that way. The
/// gradient of a broadcast is the case that needs it: `broadcast_like(0.5, y)` with an f32 `y`
/// carries an f64 number, and the gradient that comes back must be read at y's width.
InferFn gradientLikeTyped(size_t index) {
    return [index](const InferContext &ctx) -> std::optional<Type *> {
        const auto &like  = ctx.facts(index);
        const auto &other = ctx.facts(index == 0 ? 1 : 0);
        // A number is an operand with no shape to spread: a rank-0 value, or one whose shape the
        // facts do not pin down. Only a pair of tensors with known shapes of different element
        // types is provably mixed.
        const bool otherIsNumber = !other.shape || other.shape->empty();
        if (like.dtype && other.dtype && *like.dtype != *other.dtype && !otherIsNumber) {
            return std::nullopt;
        }
        return tensorOf(like.dtype, like.shape);
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
    StaticShape result(shape->size(), std::nullopt);
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
        .resultDoc = "float[..]",
        .infer     = std::move(infer),
        .kernel    = kernel,
        .traits    = {}};
}

// ---------------------------------------------------------------- tangent space

/// An integer or boolean tensor is discrete: its derivative is zero by definition (design
/// principles 9.9). It has no tangent space, but a value computed from a parameter and held in
/// one -- an argmax result, a comparison mask -- neither loses a gradient nor must be refused
/// when it is converted back to float: the derivative there is zero.
bool tensorIsDiscrete(const type::Type *type) {
    if (!isTensorType(type)) {
        return false;
    }
    const auto dtype = tensorFactsOf(type).dtype;
    return dtype && (*dtype == TypeCode::Int32 || *dtype == TypeCode::Int64 ||
                     *dtype == TypeCode::Bool);
}

// ---------------------------------------------------------------- VJP rules
// The gradient operators are differentiable too, so a gradient can be differentiated again.

/// sum_to(g, like) reduces a broadcast; its gradient broadcasts back.
void sumToVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:broadcast_like", {*dy, call.inputs[0]}));
    }
}

/// broadcast_like(g, like) spreads g; its gradient sums back to g's shape (a number for a number).
void broadcastLikeVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t g = call.inputs[0];
        if (isTensorNode(b, g)) {
            b.accumulateGradient(g, addTensorOperFor(b, g, "tensor:sum_to", {*dy, g}));
        } else if (isFloatNode(b, g)) {
            // tensor:sum answers at float64; a value of type f32 keeps the gradient at its own
            // width (see floatGradientAtWidth).
            b.accumulateGradient(
                g,
                floatGradientAtWidth(b, g, addFloatOper(b, "tensor:sum", {*dy})));
        }
    }
}

/// expand_axis(g, like, axis, keepdims) spreads g over an axis; its gradient sums over it.
void expandAxisVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 3, 4);
    if (auto dy = b.gradientOf(call.output)) {
        const vjp_node_t keepDims = call.inputs.size() > 3 ? call.inputs[3] : staticBool(b, true);
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(
                b,
                call.inputs[0],
                "tensor:sum_axis",
                {*dy, call.inputs[2], keepDims}));
    }
}

void castLikeVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:cast_like", {*dy, call.inputs[0]}));
    }
}

void permuteInverseVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 2, 2);
    if (auto dy = b.gradientOf(call.output)) {
        b.accumulateGradient(
            call.inputs[0],
            addTensorOperFor(b, call.inputs[0], "tensor:permute", {*dy, call.inputs[1]}));
    }
}

/// slice_grad(dy, like, axis, start, end, step) pads dy into place; its gradient slices it out.
void sliceGradVjp(VjpBuilder &b, const VjpCall &call) {
    requireVjpInputs(call, 5, 6);
    if (auto g = b.gradientOf(call.output)) {
        const vjp_node_t step = call.inputs.size() > 5 ? call.inputs[5] : staticInt(b, 1);
        b.accumulateGradient(
            call.inputs[0],
            addTensorOper(
                b,
                "tensor:slice",
                {*g, call.inputs[2], call.inputs[3], call.inputs[4], step}));
    }
}

} // namespace

std::vector<OpDef> gradientOps() {
    std::vector<OpDef> defs;
    defs.push_back(OpDef{
        .name      = "zeros_like",
        .exports   = {"zeros_like"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "float[..]",
        .infer     = sameAsFirst,
        .kernel    = &zerosLikeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name      = "ones_like",
        .exports   = {"ones_like"},
        .params    = {{"t", ParamKind::Tensor}},
        .resultDoc = "float[..]",
        .infer     = sameAsFirst,
        .kernel    = &onesLikeKernel,
        .traits    = {}});
    defs.push_back(OpDef{
        .name          = "numel",
        .exports       = {"numel"},
        .params        = {{"t", ParamKind::TensorLike}},
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
            for (const std::optional<int64_t> &d : *shape) {
                if (!d) {
                    return std::nullopt;
                }
                count *= *d;
            }
            return camel::core::rtdata::toSlot(static_cast<camel::core::rtdata::Int64>(count));
        }});
    defs.push_back(OpDef{
        .name          = "dim",
        .exports       = {"dim"},
        .params        = {{"t", ParamKind::TensorLike}, {"axis", ParamKind::Int}},
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
            if (axis < 0 || axis >= rank || !(*shape)[static_cast<size_t>(axis)].has_value()) {
                return std::nullopt;
            }
            return camel::core::rtdata::toSlot(
                static_cast<camel::core::rtdata::Int64>(*(*shape)[static_cast<size_t>(axis)]));
        }});

    // sum_to(g, like) is `::<T>(g: T[..], like: T[..])`: two tensors of one element type, and its
    // arms are the widths its kernel carries -- f32 and f64 (indexing.cpp says which, and design
    // principles 5.1: no conversion to reach an arm). A number on the `g` side is read at `like`'s
    // width (gradientLikeTyped above), which is what a broadcast's gradient passes.
    defs.push_back(internal(
        "sum_to",
        {{"g", ParamKind::Tensor}, {"like", ParamKind::Tensor}},
        gradientLikeTyped(1),
        &sumToKernel));
    defs.push_back(internal(
        "broadcast_like",
        {{"g", ParamKind::TensorOrScalar}, {"like", ParamKind::Tensor}},
        gradientLikeTyped(1),
        &broadcastLikeKernel));
    defs.push_back(internal(
        "expand_axis",
        {{"g", ParamKind::Tensor},
         {"like", ParamKind::Tensor},
         {"axis", ParamKind::Int},
         {"keepdims", ParamKind::Bool, true}},
        gradientLikeTyped(1),
        &expandAxisKernel));
    defs.push_back(internal(
        "gelu_grad",
        {{"x", ParamKind::Tensor}, {"dy", ParamKind::Tensor}},
        gradientLikeTyped(0),
        &geluGradKernel));
    defs.push_back(internal(
        "erf_grad",
        {{"x", ParamKind::Tensor}, {"dy", ParamKind::Tensor}},
        gradientLikeTyped(0),
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
        gradientLikeTyped(1),
        &sliceGradKernel));
    defs.push_back(internal(
        "matmul_grad_lhs",
        {{"dy", ParamKind::Tensor}, {"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        gradientLikeTyped(1),
        &matmulGradKernel<true>));
    defs.push_back(internal(
        "matmul_grad_rhs",
        {{"dy", ParamKind::Tensor}, {"lhs", ParamKind::Tensor}, {"rhs", ParamKind::Tensor}},
        gradientLikeTyped(2),
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
    setVjp(defs, "sum_to", &sumToVjp);
    setVjp(defs, "broadcast_like", &broadcastLikeVjp);
    setVjp(defs, "expand_axis", &expandAxisVjp);
    setVjp(defs, "cast_like", &castLikeVjp);
    setVjp(defs, "permute_inverse", &permuteInverseVjp);
    setVjp(defs, "slice_grad", &sliceGradVjp);
    setVjp(defs, "zeros_like", &camel::core::noGradient);
    setVjp(defs, "ones_like", &camel::core::noGradient);
    // numel and dim answer with an integer, and a discrete value's derivative is zero by
    // definition (design principles 9.9): reading a shape is locally constant in the tensor it
    // reads. Without a rule the backward pass, which visits any operator one of whose inputs is
    // active, refused them with "no derivative rule" — a shape query in a loss was refused
    // although its contribution is exactly zero, exactly as `shape` already was.
    setVjp(defs, "numel", &camel::core::noGradient);
    setVjp(defs, "dim", &camel::core::noGradient);
    return defs;
}

void registerTensorTangentSpace() {
    // A tensor is a numeric array, so the code the tangent space and the discrete predicate are
    // registered for is the array's. The space is the tensor's own: summing two gradients and
    // writing the zero gradient are tensor operations (`tensor:add`, `tensor:zeros_like`), which
    // answer at the tensor's dtype and shape. The array kind's space (`:tangent/add_arr`,
    // `:tangent/zeros_arr`) is the one for an array whose elements are values the core holds one
    // slot each: it sums through `get<slot_t>` and answers a slot array, which is not a numeric
    // tensor. A tensor gradient that came back as one left the next tensor operator reading a
    // value its type does not promise — expand_axis on the gradient of a mean_axis, which is the
    // `export_grad_mean` crash. The tangent type of an array is taken from its element type
    // (derivative.cpp's `computeTangent`), so nothing states one here: a tensor's element type
    // is always in its type. The space owns the
    // tensors of the array code and not the arrays of values that share it — an array of
    // structs, tuples or arrays keeps the core's array space (`:tangent/add_arr`,
    // `:tangent/zeros_arr`), which answers for the values this one does not own.
    camel::core::TangentSpace space;
    space.owns         = &isTensorType;
    space.addUri       = "tensor:add";
    space.zerosLikeUri = "tensor:zeros_like";
    // One row's gradient goes back to its row: a round of a loop over a tensor takes the row an
    // index names, so the scatter has to write a row of the tensor's own dtype and width.
    // `tensor:row_add_at(acc, index, row)` is that scatter, the single-index counterpart of
    // `tensor:take_grad`.
    space.scatterUri = "tensor:row_add_at";
    camel::core::DerivativeRegistry::instance().setTangentSpace(
        camel::core::type::TypeCode::Array, std::move(space));
    camel::core::DerivativeRegistry::instance().setDiscretePredicate(
        camel::core::type::TypeCode::Array, &tensorIsDiscrete);
}

} // namespace camel::tensor::ops
