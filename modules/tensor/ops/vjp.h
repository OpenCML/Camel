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
 * Helpers for the reverse-mode rules of tensor operators.
 *
 * Rules are written against the core VjpBuilder (camel/core/derivative.h),
 * which an autodiff engine implements; an operator definition carries its
 * rule, and registration publishes it in the core DerivativeRegistry.
 */

#pragma once

#include "camel/core/derivative.h"

#include <span>
#include <string_view>
#include <vector>

namespace camel::tensor::ops {

namespace type   = camel::core::type;
using vjp_node_t = camel::core::vjp_node_t;
using camel::core::VjpBuilder;
using camel::core::VjpCall;
using VjpFn = camel::core::VjpRule;

// ---------------------------------------------------------------- helpers for rules

/// The general tensor type used for gradient nodes.
type::Type *vjpTensorType();
bool isTensorNode(const VjpBuilder &builder, vjp_node_t node);
/// A Camel float (float64 or float32) node.
bool isFloatNode(const VjpBuilder &builder, vjp_node_t node);

/// Throws when the call does not have between `minimum` and `maximum` inputs.
void requireVjpInputs(const VjpCall &call, size_t minimum, size_t maximum);

/// Adds a tensor-typed operator node (shorthand for rules).
vjp_node_t
addTensorOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs);
/// Adds a float64-typed operator node.
vjp_node_t
addFloatOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs);
vjp_node_t staticInt(VjpBuilder &builder, int64_t value);
vjp_node_t staticBool(VjpBuilder &builder, bool value);

/// Accumulates `gradient` (shaped like the result of a broadcasting operation) into `operand`,
/// reduced to the operand's shape: summed over broadcast axes for a tensor, fully for a float.
/// Operands of other kinds (numeric arrays, integers) take no gradient.
void accumulateOperand(VjpBuilder &builder, vjp_node_t operand, vjp_node_t gradient);

/// Gradients of lhs @ rhs given the output gradient dy (batch dims and vectors included).
void accumulateMatmulGradients(VjpBuilder &builder, vjp_node_t lhs, vjp_node_t rhs, vjp_node_t dy);
/// dy masked by a relu output: dy * (y > 0).
vjp_node_t reluGradient(VjpBuilder &builder, vjp_node_t output, vjp_node_t dy);

struct OpDef;
/// Attaches `rule` to the definition named `name` in `defs` (throws if absent).
void setVjp(std::vector<OpDef> &defs, std::string_view name, VjpFn rule);

} // namespace camel::tensor::ops
