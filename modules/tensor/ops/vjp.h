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

/// The type a gradient node that is the gradient of `value` takes: that value's tangent type, which
/// is the value's own type for anything a tensor kernel can read and the element type its kind
/// states when its own type leaves it out. `nullptr` when the value has no tensor tangent, and the
/// rule falls back to its other inputs.
type::Type *vjpTensorType(const VjpBuilder &builder, vjp_node_t value);
bool isTensorNode(const VjpBuilder &builder, vjp_node_t node);
/// A Camel float (float64 or float32) node.
bool isFloatNode(const VjpBuilder &builder, vjp_node_t node);

/// Whether a rule that converts a value to a tensor (`new`, `cast`, `reshape`, `concat`) can carry
/// a tensor's gradient back into `input`. A numeric array can: it is a tensor here (`isTensorType`
/// is `isNumericArrayType`), so the rule's own adjoint answers at the array's own type and nothing
/// is refused for it. A value of another kind that those calls accept where they accept a tensor --
/// an array of arrays (`f32[][]`), an array of records -- is converted by the forward rule (it
/// flattens it), but nothing in this module carries a tensor's gradient back into such a container:
/// the container's shape is not the tensor's shape and its elements are not the gradient's
/// elements. A container that carries no gradient of its own is skipped (`false`, the rule returns
/// without building an adjoint for it); one that depends on a differentiated parameter has no
/// gradient channel at all, and this refuses it by name with `refuseVjp` (design principles 9.9:
/// refused, never dropped) -- which also keeps it out of the kernels that would read it as a
/// tensor, where it is a crash rather than a diagnostic. `what` names the input in the reason
/// ("its input", "a part of it").
bool hasTensorAdjoint(
    const VjpBuilder &builder, const VjpCall &call, vjp_node_t input, std::string_view what);

/// Throws when the call does not have between `minimum` and `maximum` inputs.
void requireVjpInputs(const VjpCall &call, size_t minimum, size_t maximum);

/// Adds a tensor-typed operator node (shorthand for rules). `attrs` records an implementation
/// choice the rule carries from the forward node (tensor.kernel on a matmul gradient). The result
/// takes the type of the first input that states an element type; an input that is an array stating
/// none of its own takes its tangent type, and a rule whose inputs are all scalars builds a scalar
/// node and gets the first input's tangent type. A rule that reaches none of those is refused by
/// name: its node's value would be declared at an element type no tensor kernel can read. A rule
/// whose result is shaped like a *particular* value states it with addTensorOperFor instead.
vjp_node_t addTensorOper(
    VjpBuilder &builder,
    std::string_view uri,
    std::initializer_list<vjp_node_t> inputs,
    attr::AttrSetId attrs = attr::kNoAttrs);

/// The same, for a node that is the gradient *of* `primal`: it takes that value's tangent type. A
/// gradient has the element type and the shape of the value it is the gradient of (design
/// principles 9.9), and that type is where its element type is. Without this the rule would have to
/// declare the gradient with the undetermined array `Tensor`, whose element type no tensor kernel
/// can read.
vjp_node_t addTensorOperFor(
    VjpBuilder &builder,
    vjp_node_t primal,
    std::string_view uri,
    std::initializer_list<vjp_node_t> inputs,
    attr::AttrSetId attrs = attr::kNoAttrs);
vjp_node_t addTensorOperFor(
    VjpBuilder &builder,
    vjp_node_t primal,
    std::string_view uri,
    std::span<const vjp_node_t> inputs,
    attr::AttrSetId attrs = attr::kNoAttrs);
/// Adds a float64-typed operator node.
vjp_node_t
addFloatOper(VjpBuilder &builder, std::string_view uri, std::initializer_list<vjp_node_t> inputs);
vjp_node_t staticInt(VjpBuilder &builder, int64_t value);
vjp_node_t staticBool(VjpBuilder &builder, bool value);

/// Accumulates `gradient` (shaped like the result of a broadcasting operation) into `operand`,
/// reduced to the operand's shape: summed over broadcast axes for a tensor, fully for a float.
/// A numeric array is a tensor here, so an array operand is reduced the way a tensor is; an operand
/// of another kind is not one the rule can carry a gradient into, and takes none.
void accumulateOperand(VjpBuilder &builder, vjp_node_t operand, vjp_node_t gradient);

/// `gradient` converted to the float width of `value`: the rules here reduce at float64
/// (`tensor:sum`), while an `f32` value's tangent is an `f32`. Handing the accumulator another
/// width would store a float64 number in an f32 slot, and an integral gradient read back at f32's
/// width is 0.0 -- a derivative that is not the derivative (design principles 9.9). The conversion
/// belongs here, in the rule that produced the gradient, and not in the accumulator, which refuses
/// a gradient of another element type instead of converting it.
vjp_node_t floatGradientAtWidth(VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient);

/// `tensorCastTo` is `floatGradientAtWidth` for a value that states an element type of any
/// numeric kind: the gradient is converted to the value's own type with `tensor:cast_like` and
/// declared with it, so the accumulator receives a gradient of the element type the value has.
/// A conversion whose result is at another element type than its input (`cast` answers at the
/// dtype it was given) is what needs it -- handing the accumulator a gradient of another element
/// type is refused, and reading a number at another width is not the derivative (design
/// principles 9.9).
vjp_node_t tensorCastTo(VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient);

/// `tensorGradientAtElementType` is that conversion asked for only when the two element types
/// actually differ, for a rule that builds a `sum_to` or a `broadcast_like` out of the gradient:
/// the target's element type is the one the operator reads its gradient at, so the rule states the
/// conversion where a gradient operator's own declaration (f32, `gradientLike`) left a pair the
/// signature refuses. The graph keeps its node count when the two already agree.
vjp_node_t tensorGradientAtElementType(
    VjpBuilder &builder, vjp_node_t value, vjp_node_t gradient);

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
