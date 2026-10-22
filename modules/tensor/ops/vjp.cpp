// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared helpers for operator VJP rules (see vjp.h).
 */

#include "vjp.h"

#include "camel/core/error/runtime.h"

#include "../type.h"
#include "op_def.h"

#include <stdexcept>
#include <string>

namespace camel::tensor::ops {

type::Type *vjpTensorType() { return TensorType::Default(); }

bool isTensorNode(const VjpBuilder &builder, vjp_node_t node) {
    return asTensorType(builder.nodeType(node)) != nullptr;
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

vjp_node_t
addTensorOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs) {
    return builder.addOper(
        vjpTensorType(),
        uri,
        std::span<const vjp_node_t>(inputs.begin(), inputs.size()));
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

void accumulateOperand(VjpBuilder &builder, vjp_node_t operand, vjp_node_t gradient) {
    if (isTensorNode(builder, operand)) {
        builder.accumulateGradient(
            operand,
            addTensorOper(builder, "tensor:sum_to", {gradient, operand}));
    } else if (isFloatNode(builder, operand)) {
        const vjp_node_t total = isTensorNode(builder, gradient)
                                     ? addFloatOper(builder, "tensor:sum", {gradient})
                                     : gradient;
        builder.accumulateGradient(operand, total);
    }
}

void accumulateMatmulGradients(VjpBuilder &builder, vjp_node_t lhs, vjp_node_t rhs, vjp_node_t dy) {
    builder.accumulateGradient(
        lhs,
        addTensorOper(builder, "tensor:matmul_grad_lhs", {dy, lhs, rhs}));
    builder.accumulateGradient(
        rhs,
        addTensorOper(builder, "tensor:matmul_grad_rhs", {dy, lhs, rhs}));
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
