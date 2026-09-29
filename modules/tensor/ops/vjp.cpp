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
 * Shared helpers for operator VJP rules (see vjp.h).
 */

#include "vjp.h"

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
    type::Type *t = builder.nodeType(node);
    return t && t->equals(type::Type::Float64());
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

void accumulateMatmulGradients(VjpBuilder &builder, vjp_node_t lhs, vjp_node_t rhs, vjp_node_t dy) {
    const vjp_node_t rhsT = addTensorOper(builder, "tensor:transpose", {rhs});
    builder.accumulateGradient(lhs, addTensorOper(builder, "tensor:matmul", {dy, rhsT}));
    const vjp_node_t lhsT = addTensorOper(builder, "tensor:transpose", {lhs});
    builder.accumulateGradient(rhs, addTensorOper(builder, "tensor:matmul", {lhsT, dy}));
}

void accumulateAddendGradient(VjpBuilder &builder, vjp_node_t addend, vjp_node_t dy) {
    if (isTensorNode(builder, addend) || isFloatNode(builder, addend)) {
        builder.accumulateGradient(addend, dy);
    }
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

} // namespace camel::tensor::ops
