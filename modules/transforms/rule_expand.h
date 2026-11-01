// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/compile/gir/rule_templates.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"

#include <string>
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

} // namespace camel::transforms