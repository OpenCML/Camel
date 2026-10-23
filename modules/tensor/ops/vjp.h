// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
namespace attr   = camel::core::attr;
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

/// Adds a tensor-typed operator node (shorthand for rules). `attrs` records an implementation
/// choice the rule carries from the forward node (tensor.kernel on a matmul gradient).
vjp_node_t addTensorOper(
    VjpBuilder &builder,
    std::string_view uri,
    std::initializer_list<vjp_node_t> inputs,
    attr::AttrSetId attrs = attr::kNoAttrs);
/// Adds a float64-typed operator node.
vjp_node_t
addFloatOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs);
vjp_node_t staticInt(VjpBuilder &builder, int64_t value);
vjp_node_t staticBool(VjpBuilder &builder, bool value);

/// Accumulates `gradient` (shaped like the result of a broadcasting operation) into `operand`,
/// reduced to the operand's shape: summed over broadcast axes for a tensor, fully for a float.
/// Operands of other kinds (numeric arrays, integers) take no gradient.
void accumulateOperand(VjpBuilder &builder, vjp_node_t operand, vjp_node_t gradient);

/// `gradient` converted to the float width of `value`: the rules here reduce at float64
/// (`tensor:sum`), while an `f32` value's tangent is an `f32`. Handing the accumulator another
/// width would store a float64 number in an f32 slot, and an integral gradient read back at f32's
/// width is 0.0 -- a derivative that is not the derivative (design principles 9.9). The conversion
/// belongs here, in the rule that produced the gradient, and not in the accumulator, which refuses
/// a gradient of another element type instead of converting it.
vjp_node_t floatGradientAtWidth(VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient);

/// Gradients of lhs @ rhs given the output gradient dy (batch dims and vectors included).
void accumulateMatmulGradients(
    VjpBuilder &builder,
    vjp_node_t lhs,
    vjp_node_t rhs,
    vjp_node_t dy,
    attr::AttrSetId forwardAttrs);
/// dy masked by a relu output: dy * (y > 0).
vjp_node_t reluGradient(VjpBuilder &builder, vjp_node_t output, vjp_node_t dy);

/// Refuses the differentiation of `call`, with a diagnostic located at the operator and naming
/// it. A rule calls it when its mathematics is known but this module has no operator with which
/// to express the adjoint of this input: dropping the gradient instead would return a zero the
/// caller never asked for. `reason` says which input and why (design principles 9.9: a
/// continuous value that depends on a differentiated parameter is refused, never zeroed).
[[noreturn]] void refuseVjp(const VjpCall &call, std::string_view reason);

struct OpDef;
/// Attaches `rule` to the definition named `name` in `defs` (throws if absent).
void setVjp(std::vector<OpDef> &defs, std::string_view name, VjpFn rule);

} // namespace camel::tensor::ops
