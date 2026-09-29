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
 * Reverse-mode rules (vector-Jacobian products) attached to operator
 * definitions.
 *
 * A rule emits the backward computation of one operator call into a graph
 * through the abstract VjpBuilder: it reads the gradient of the call's output
 * and accumulates gradients into the call's inputs, adding operator nodes as
 * needed. The builder is implemented by the autodiff engine (the nn module),
 * so operator definitions carry their own derivative without depending on the
 * engine, and the engine needs no per-operator table of its own.
 *
 * Also provides the helpers shared by the tensor and nn rules.
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace camel::tensor::ops {

namespace type   = camel::core::type;
using vjp_node_t = camel::runtime::gc_node_ref_t;

class VjpBuilder {
  public:
    virtual ~VjpBuilder() = default;

    /// Static type of a node of the graph being differentiated.
    virtual type::Type *nodeType(vjp_node_t node) const = 0;
    /// A float64 constant node.
    virtual vjp_node_t addStaticFloat(double value) = 0;
    /// An operator node with the given norm inputs.
    virtual vjp_node_t
    addOper(type::Type *type, std::string_view uri, std::span<const vjp_node_t> normInputs) = 0;
    /// Gradient accumulated so far for a node, if any.
    virtual std::optional<vjp_node_t> gradientOf(vjp_node_t primal) const = 0;
    /// Adds `gradient` to the gradient of `primal`.
    virtual void accumulateGradient(vjp_node_t primal, vjp_node_t gradient) = 0;
};

/// One operator call being differentiated.
struct VjpCall {
    std::string_view uri;
    std::span<const vjp_node_t> inputs;
    vjp_node_t output;
};

using VjpFn = void (*)(VjpBuilder &builder, const VjpCall &call);

// ---------------------------------------------------------------- helpers for rules

/// The general tensor type used for gradient nodes.
type::Type *vjpTensorType();
bool isTensorNode(const VjpBuilder &builder, vjp_node_t node);
bool isFloatNode(const VjpBuilder &builder, vjp_node_t node);

/// Throws when the call does not have between `minimum` and `maximum` inputs.
void requireVjpInputs(const VjpCall &call, size_t minimum, size_t maximum);

/// Adds a tensor-typed operator node (shorthand for rules).
vjp_node_t
addTensorOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs);

/// Gradients of lhs @ rhs given the output gradient dy.
void accumulateMatmulGradients(VjpBuilder &builder, vjp_node_t lhs, vjp_node_t rhs, vjp_node_t dy);
/// Gradient of an addend (tensor or float) of a sum whose gradient is dy.
void accumulateAddendGradient(VjpBuilder &builder, vjp_node_t addend, vjp_node_t dy);
/// dy masked by a relu output: dy * (y > 0).
vjp_node_t reluGradient(VjpBuilder &builder, vjp_node_t output, vjp_node_t dy);

struct OpDef;
/// Attaches `rule` to the definition named `name` in `defs` (throws if absent).
void setVjp(std::vector<OpDef> &defs, std::string_view name, VjpFn rule);

} // namespace camel::tensor::ops
