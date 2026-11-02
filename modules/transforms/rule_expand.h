// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/compile/gir/rule_templates.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace camel::transforms {

/// The position that asks for the whole answer: a right side that answers one value, taken as it
/// stands. Every other position asks for one name's cotangent.
inline constexpr size_t kWholeAnswer = static_cast<size_t>(-1);

/// The type a rule is generic in: the type the primitive works on. A primitive over scalars is its
/// own type (`+` on two `f32`), and one over arrays works on the element of the array -- `matmul`
/// multiplies elements, `sum_axis` adds them -- so the rule is filed once per element type and says
/// which axis the batch is on, never what shape a row has.
///
/// One function, three askers: `vmap`, `jvp` and the transposer each instantiate a rule at a type
/// their operands name. Writing `Float64()` where this answer belongs is a special case in the place
/// that has none.
camel::core::type::Type *ruleElementTypeOf(camel::core::type::Type *type);

/// The element of the first operand that is not a condition, from the operands' own types, or the
/// first of them when every one is a condition; nullptr when the list holds no type at all.
///
/// A rule is generic in the element its values are made of, and that is not always the first port:
/// a condition is not a value arm. `where(cond, lhs, rhs)` takes a `bool` first and answers the
/// element of the two arms, so a rule instantiated on the first port would be a `bool` rule applied
/// to a float batch. The node's own type is no better: `argmax_axis` takes a tensor of numbers and
/// answers `int64` positions.
camel::core::type::Type *operandElementTypeOf(
    std::span<camel::core::type::Type *const> operandTypes);

/// The same question asked of a built graph: the element named by the node at `ref`'s first value
/// port, or nullptr when it has none.
camel::core::type::Type *firstOperandTypeOf(
    camel::runtime::GCGraph &function, camel::runtime::gc_node_ref_t ref);

/// The rule's right side expanded at the match site: the body of the closure it answers with is
/// copied into this graph, its captures bound to the operands and its parameters to the tangents,
/// and the copy's exit is the operator's derivative.
///
/// Module-internal on purpose: the right side is a graph of the rule's own program, and the one
/// thing a transform needs from it is "stand this in where the operator stood". It is not a
/// general inliner and is not offered as one -- what it refuses, it refuses by name in `why`.
///
/// Two transforms reach a rule's right side without running the rule themselves: `jvp` (the
/// operator's tangent) and `transpose` (its cotangent). Neither may call the closure the right
/// side answers with -- a compile-time graph has no closure to call -- so the one lambda layer is
/// expanded here instead.
///
/// A rule's left side names N values and its right side answers N cotangents -- a tuple, in the
/// order the left side writes them -- so `position` picks the i-th component, and only the nodes
/// that component needs are copied: a position never asks for a value its own answer does not read.
/// `kWholeAnswer` takes the exit itself, which is what a right side that answers one value means.
camel::runtime::gc_node_ref_t expandRuleRightSide(
    camel::runtime::GraphDraftGroup &group, camel::runtime::GraphTransaction &transaction,
    const camel::compile::gir::GenericRuleInstance &instance,
    const std::function<camel::runtime::gc_node_ref_t(size_t)> &valueOf,
    const std::vector<camel::runtime::gc_node_ref_t> &tangents, std::string &why,
    size_t position = kWholeAnswer);

/// The same expansion, reached from the rule's *name*: the transform that has a rule's name and its
/// operands -- and no instance, because it never runs a rule itself -- asks here.
///
/// `valueOf` answers an operand's value by the place the right side binds it, and is asked only for
/// the places the answer this `position` selects reads. That is what lets a position be answered
/// where one of the operands has no value: the component that answers it names only the other one,
/// so the walk is never asked for a value it does not carry.
///
/// Answers the expanded node, or `kInvalidNodeRef` with `why`: no rule of that name in this context,
/// an instance that did not come about, a position the right side does not answer, or a value the
/// answer reads and the walk lacks.
camel::runtime::gc_node_ref_t expandRuleByName(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction, std::string_view ruleName,
    const std::function<camel::runtime::gc_node_ref_t(size_t)> &valueOf,
    const std::vector<camel::runtime::gc_node_ref_t> &tangents, size_t position, std::string &why);

} // namespace camel::transforms