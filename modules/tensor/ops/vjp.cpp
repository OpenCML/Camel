// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared helpers for operator VJP rules (see vjp.h).
 */

#include "vjp.h"

#include "camel/core/error/runtime.h"

#include "../type.h"
#include "../attrs.h"
#include "../dtype.h"
#include "op_def.h"

#include <array>
#include <stdexcept>
#include <string>

namespace camel::tensor::ops {

namespace {

/// True when `type` states the element type of a *tensor*: an array — at a rank, or with none
/// fixed, which is what a bare `Tensor` is — whose innermost element is a numeric scalar a tensor
/// kernel has. The undetermined array (`Tensor`, the alias's transitional spelling) states none,
/// and a node declared with it is specialized at an element type no kernel can read --
/// `tensor:subtract` on one answers "Unsupported Tensor dtype" at run time.
///
/// A scalar is not a tensor and never states a tensor's element type. `:op/div_d` answers
/// `float64`, which is a numeric scalar, so a rule that scales a gradient by a count and names
/// the scaling node among the inputs handed the gradient node the scalar's type: the node's value
/// was the `[3, 2]` tensor and its declared type was one number, so the reader that reads it by
/// its type (`tensor:sum_to`, which takes its operand for an array) found the element where the
/// tensor was and refused -- `sum_to: cannot broadcast [3, 2] to []`, the wall behind every red
/// the f64 arm uncovered.
bool statesTensorElementType(const type::Type *type) {
    if (type == nullptr || type->code() != type::TypeCode::Array) {
        return false;
    }
    const type::Type *elem = type;
    while (elem != nullptr && elem->code() == type::TypeCode::Array) {
        elem = type::asArrayType(elem)->elemType();
    }
    return elem != nullptr && isSupportedTensorScalar(elem->code());
}

/// The type a gradient node takes when the rule names the value the node is the gradient of: the
/// tangent type of that value, which is the value's own type for anything a tensor kernel can read
/// (an array states its element type already), and the element type the value's kind states when
/// its own type leaves it out. A gradient is made of the element type of the value it is the
/// gradient of (design principles 9.9), and the undetermined array states none: a node declared
/// with it is specialized at an element type no kernel can read -- `tensor:subtract` on one answers
/// "Unsupported Tensor dtype" at run time. `nullptr` means the value has no tensor tangent, and the
/// caller falls back to the rule's other inputs.
type::Type *vjpTensorType(const VjpBuilder &builder, vjp_node_t value) {
    type::Type *type = builder.nodeType(value);
    if (statesTensorElementType(type)) {
        return type;
    }
    // The machinery's own question: does a value of this type need a gradient channel at all
    // (`machineryTangentTypeOf`) -- not the gradient type a user's parameter gets.
    type::Type *tangent = camel::core::machineryTangentTypeOf(type);
    return statesTensorElementType(tangent) ? tangent : nullptr;
}

/// The type a gradient node takes when the rule does not name the value it is the gradient of: the
/// type of the first input that states one. A gradient is made of the element type of the value it
/// is the gradient of (design principles 9.9), and a rule's first input is the output gradient or
/// an operand of the forward node, which states the same one.
///
/// Two fallbacks follow for a node whose inputs are not tensors. An array whose own type leaves its
/// element type out (`Tensor`, the alias's transitional spelling) answers with its tangent type,
/// which is where its element type is; it is an array, so a rule that also names a scalar operand
/// still types its node from the value that is a tensor. Scalars alone answer with the first
/// input's own tangent type: the rule built the node out of scalars (the exponent of a power
/// lowered by one, a count), the node is that scalar, and the tangent of a scalar is a scalar of
/// the same width. `nullptr` means no input answers at all.
type::Type *gradientTypeFrom(const VjpBuilder &builder, std::span<const vjp_node_t> inputs) {
    for (vjp_node_t input : inputs) {
        type::Type *type = builder.nodeType(input);
        if (statesTensorElementType(type)) {
            return type;
        }
    }
    for (vjp_node_t input : inputs) {
        if (type::Type *tangent = camel::core::machineryTangentTypeOf(builder.nodeType(input));
            tangent != nullptr && tangent->code() == type::TypeCode::Array) {
            return tangent;
        }
    }
    for (vjp_node_t input : inputs) {
        if (type::Type *tangent = camel::core::machineryTangentTypeOf(builder.nodeType(input))) {
            return tangent;
        }
    }
    return nullptr;
}

/// Refuses a gradient node that no input gives an element type: the node's value would be declared
/// at an element type no tensor kernel can read, so the rule is refused by name here, where the
/// node was built, rather than left for a kernel to read the wrong way.
[[noreturn]] void refuseUntypedGradient(std::string_view uri) {
    throw std::logic_error(
        "vjp: the gradient node of '" + std::string(uri) + "' has no tensor element type");
}

} // namespace

bool isTensorNode(const VjpBuilder &builder, vjp_node_t node) {
    return isTensorType(builder.nodeType(node));
}

bool hasTensorAdjoint(
    const VjpBuilder &builder, const VjpCall &call, vjp_node_t input, std::string_view what) {
    const type::Type *type = builder.nodeType(input);
    if (type == nullptr || isTensorType(type)) {
        return true;
    }
    if (builder.isActive(input)) {
        refuseVjp(
            call,
            std::string(what) + " is a value of type '" + type->toString() +
                "', and a tensor here is a numeric array: this module converts such a container by "
                "flattening it, but no operator carries a tensor's gradient back into one, so that "
                "value's gradient would be dropped silently. Convert a numeric array, a tensor "
                "parameter or a slice of one instead");
    }
    return false;
}

bool isFloatNode(const VjpBuilder &builder, vjp_node_t node) {
    const type::Type *t = builder.nodeType(node);
    return t && (t->code() == type::TypeCode::Float64 || t->code() == type::TypeCode::Float32);
}

void requireVjpInputs(const VjpCall &call, size_t minimum, size_t maximum) {
    if (call.inputs.size() < minimum || call.inputs.size() > maximum) {
        throw std::runtime_error("VJP rule input-count mismatch for " + std::string(call.uri));
    }
}

vjp_node_t addTensorOper(
    VjpBuilder &builder,
    std::string_view uri,
    std::initializer_list<vjp_node_t> inputs,
    attr::AttrSetId attrs) {
    const std::span<const vjp_node_t> span(inputs.begin(), inputs.size());
    type::Type *type = gradientTypeFrom(builder, span);
    if (type == nullptr) {
        refuseUntypedGradient(uri);
    }
    return builder.addOper(type, uri, span, attrs);
}

namespace {

/// The type a gradient node that is the gradient *of* `primal` takes: that value's tangent type
/// when it has a tensor one, otherwise the first input that states one, otherwise the tangent type
/// of the first input that has one (a node built out of scalars is that scalar), otherwise nothing
/// -- and the rule is refused by name rather than given a node no kernel can read.
type::Type *gradientTypeFor(
    const VjpBuilder &builder,
    vjp_node_t primal,
    std::span<const vjp_node_t> inputs) {
    if (type::Type *type = vjpTensorType(builder, primal)) {
        return type;
    }
    return gradientTypeFrom(builder, inputs);
}

} // namespace

vjp_node_t addTensorOperFor(
    VjpBuilder &builder,
    vjp_node_t primal,
    std::string_view uri,
    std::initializer_list<vjp_node_t> inputs,
    attr::AttrSetId attrs) {
    const std::span<const vjp_node_t> span(inputs.begin(), inputs.size());
    type::Type *type = gradientTypeFor(builder, primal, span);
    if (type == nullptr) {
        refuseUntypedGradient(uri);
    }
    return builder.addOper(type, uri, span, attrs);
}

vjp_node_t addTensorOperFor(
    VjpBuilder &builder,
    vjp_node_t primal,
    std::string_view uri,
    std::span<const vjp_node_t> inputs,
    attr::AttrSetId attrs) {
    type::Type *type = gradientTypeFor(builder, primal, inputs);
    if (type == nullptr) {
        refuseUntypedGradient(uri);
    }
    return builder.addOper(type, uri, inputs, attrs);
}

vjp_node_t addFloatOper(
    VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs) {
    return builder.addOper(
        type::Type::Float64(),
        uri,
        std::span<const vjp_node_t>(inputs.begin(), inputs.size()));
}

vjp_node_t staticInt(VjpBuilder &builder, int64_t value) {
    return builder.addStatic(camel::core::rtdata::toSlot<int64_t>(value), type::Type::Int64());
}

vjp_node_t staticBool(VjpBuilder &builder, bool value) {
    return builder.addStatic(camel::core::rtdata::toSlot<bool>(value), type::Type::Bool());
}

vjp_node_t floatGradientAtWidth(VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient) {
    const type::Type *valueType    = builder.nodeType(value);
    const type::Type *gradientType = builder.nodeType(gradient);
    if (valueType == nullptr || gradientType == nullptr) {
        return gradient;
    }
    const type::TypeCode valueCode    = valueType->code();
    const type::TypeCode gradientCode = gradientType->code();
    if (valueCode == gradientCode) {
        return gradient;
    }
    // The two conversions that exist: :op/dtof is float64 -> f32 (result f32), :op/ftod is
    // f32 -> float64 (result float64). Both are differentiable themselves.
    if (valueCode == type::TypeCode::Float32 && gradientCode == type::TypeCode::Float64) {
        const std::array<vjp_node_t, 1> inputs{gradient};
        return builder.addOper(type::Type::Float32(), ":op/dtof", inputs);
    }
    if (valueCode == type::TypeCode::Float64 && gradientCode == type::TypeCode::Float32) {
        const std::array<vjp_node_t, 1> inputs{gradient};
        return builder.addOper(type::Type::Float64(), ":op/ftod", inputs);
    }
    return gradient;
}

vjp_node_t tensorCastTo(VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient) {
    type::Type *valueType = builder.nodeType(value);
    if (!statesTensorElementType(valueType)) {
        return gradient;
    }
    if (valueType->code() == type::TypeCode::Array) {
        const std::array<vjp_node_t, 2> inputs{gradient, value};
        return builder.addOper(valueType, "tensor:cast_like", inputs);
    }
    // A scalar value: the gradient is a number of the value's own width, converted with the two
    // scalar conversions the graph has (`:op/dtof`, `:op/ftod`).
    return floatGradientAtWidth(builder, value, gradient);
}

/// A gradient of `value` at the element type `value` states. `sum_to` and `broadcast_like` read
/// their gradient operand at the target's element type (`gradientLikeTyped` in gradient_ops.cpp),
/// while the gradient operators whose kernels compute in f32 only declare f32 (`gradientLike`, same
/// file) -- and what a signature refuses is the *fact* a pass computed, not the type the rule wrote:
/// the accumulator's gradient node is a value the rule itself is building, so its declared type can
/// still say the two agree while the operator that fills it answers f32. The conversion is therefore
/// stated for every gradient these two operators receive, which is what design principles 9.9 asks
/// of the rule that produced the gradient (vjp.h, tensorCastTo). A value whose type leaves the
/// element type out takes none, as in the accumulator.
vjp_node_t tensorGradientAtElementType(
    VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient) {
    return tensorCastTo(builder, value, gradient);
}

void accumulateOperand(VjpBuilder &builder, vjp_node_t operand, vjp_node_t gradient) {
    if (isTensorNode(builder, operand)) {
        builder.accumulateGradient(
            operand,
            addTensorOperFor(
                builder,
                operand,
                "tensor:sum_to",
                {tensorGradientAtElementType(builder, operand, gradient), operand}));
    } else if (isFloatNode(builder, operand)) {
        const vjp_node_t total = isTensorNode(builder, gradient)
                                     ? addFloatOper(builder, "tensor:sum", {gradient})
                                     : gradient;
        builder.accumulateGradient(operand, floatGradientAtWidth(builder, operand, total));
    }
}

void accumulateMatmulGradients(
    VjpBuilder &builder,
    vjp_node_t lhs,
    vjp_node_t rhs,
    vjp_node_t dy,
    attr::AttrSetId forwardAttrs) {
    // The gradient operators run the same licence decision as the forward matmul: tensor::fuse
    // writes tensor.kernel on the forward node, and the rule carries that one key to the nodes it
    // emits. Copying the whole set would also copy tensor.fusion, which describes a forward group.
    const attr::AttrValueId kernel = attr::valueOf(forwardAttrs, kernelAttrKey());
    const attr::AttrSetId attrs =
        kernel == attr::kNoValue ? attr::kNoAttrs : attr::single(kernelAttrKey(), kernel);
    builder.accumulateGradient(
        lhs,
        addTensorOperFor(builder, lhs, "tensor:matmul_grad_lhs", {dy, lhs, rhs}, attrs));
    builder.accumulateGradient(
        rhs,
        addTensorOperFor(builder, rhs, "tensor:matmul_grad_rhs", {dy, lhs, rhs}, attrs));
}

vjp_node_t reluGradient(VjpBuilder &builder, vjp_node_t output, vjp_node_t dy) {
    const vjp_node_t mask =
        addTensorOper(builder, "tensor:gt", {output, builder.addStaticFloat(0.0)});
    return addTensorOper(builder, "tensor:multiply", {dy, mask});
}

void setVjp(std::vector<OpDef> &defs, std::string_view name, VjpFn rule) {
    for (OpDef &def : defs) {
        if (def.name == name) {
            def.vjp = rule;
            return;
        }
    }
    throw std::logic_error("setVjp: no operator definition named " + std::string(name));
}

void refuseVjp(const VjpCall &call, std::string_view reason) {
    throw camel::core::error::RuntimeFault(
        camel::core::error::RuntimeDiag::RuntimeError,
        {"autodiff: cannot differentiate operator '" + std::string(call.uri) + "': " +
         std::string(reason)},
        "",
        nullptr,
        camel::core::error::ExecutionSiteHint{
            .cachedOrigin = static_cast<camel::source::origin_id_t>(call.origin)});
}

} // namespace camel::tensor::ops
