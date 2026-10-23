// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared helpers for operator VJP rules (see vjp.h).
 */

#include "vjp.h"

#include "camel/core/error/runtime.h"

#include "../type.h"
#include "../attrs.h"
#include "op_def.h"

#include <array>
#include <stdexcept>
#include <string>

namespace camel::tensor::ops {

type::Type *vjpTensorType() { return TensorType::Default(); }

bool isTensorNode(const VjpBuilder &builder, vjp_node_t node) {
    return isTensorType(builder.nodeType(node));
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
    return builder.addOper(
        vjpTensorType(),
        uri,
        std::span<const vjp_node_t>(inputs.begin(), inputs.size()),
        attrs);
}

vjp_node_t
addFloatOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs) {
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

void accumulateOperand(VjpBuilder &builder, vjp_node_t operand, vjp_node_t gradient) {
    if (isTensorNode(builder, operand)) {
        builder.accumulateGradient(
            operand,
            addTensorOper(builder, "tensor:sum_to", {gradient, operand}));
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
        addTensorOper(builder, "tensor:matmul_grad_lhs", {dy, lhs, rhs}, attrs));
    builder.accumulateGradient(
        rhs,
        addTensorOper(builder, "tensor:matmul_grad_rhs", {dy, lhs, rhs}, attrs));
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
