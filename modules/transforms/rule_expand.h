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
camel::runtime::gc_node_ref_t expandRuleRightSide(
    camel::runtime::GraphDraftGroup &group, camel::runtime::GraphTransaction &transaction,
    const camel::compile::gir::GenericRuleInstance &instance,
    const std::vector<camel::runtime::gc_node_ref_t> &values,
    const std::vector<camel::runtime::gc_node_ref_t> &tangents, std::string &why);

/// The same expansion, reached from the rule's *name*: the transform that has a rule's name and its
/// operands -- and no instance, because it never runs a rule itself -- asks here.
///
/// `valueOf` answers an operand's value by the place the right side binds it, and is asked only for
/// the places the right side's body reads. That is what lets a position be answered where one of the
/// operands has no value: the rule that answers it names only the other one, so the walk is never
/// asked for a value it does not carry.
///
/// Answers the expanded node, or `kInvalidNodeRef` with `why`: no rule of that name in this context,
/// an instance that did not come about, or a value the rule reads and the walk lacks.
camel::runtime::gc_node_ref_t expandRuleByName(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction, std::string_view ruleName,
    const std::function<camel::runtime::gc_node_ref_t(size_t)> &valueOf,
    const std::vector<camel::runtime::gc_node_ref_t> &tangents, std::string &why);

} // namespace camel::transforms