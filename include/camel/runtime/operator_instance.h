// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operator semantics of one OPER node of a frozen graph (docs/design/op-semantics,
 * sections 3 and 5): what the node's operator declares, instantiated with what the graph knows
 * at the node.
 *
 * The facts are the types of the node's inputs (a tensor type with its shape) and the values
 * of the inputs that are constants: a static DATA node, also reached through GATEs (an
 * inlined call's argument). A constant whose object is not a plain scalar counts only when
 * every user of it is a pure operator (nothing can have written it in place). A back end that
 * specializes a graph to the shapes of a call asks again on the specialized graph and gets the
 * instance for those shapes.
 */

#pragma once

#include "camel/core/operator_semantics.h"
#include "camel/runtime/graph.h"

#include <iosfwd>
#include <optional>

namespace camel::runtime {

/// The index semantics of the OPER node `node` of `graph`, or nullopt when the node is not an
/// OPER, its operator declares none, or the facts at the node do not fix it (sem::instantiate).
std::optional<camel::core::sem::IndexedInstance>
instanceOf(const GCGraph &graph, gc_node_ref_t node);

/**
 * What a back end may do with the reduction of the OPER node `node` (design 7.3): the law its
 * instance combines by, with the permissions the program granted the node
 * (GCGraph::nodePermissions). nullopt when the node has no instance or its instance does not
 * reduce. A consumer with a default relaxation (design principles 7.3) passes its
 * policy; the default asks for nothing the program did not write.
 */
std::optional<camel::core::sem::ReorderFreedom> reorderFreedomOf(
    const GCGraph &graph, gc_node_ref_t node,
    const camel::core::sem::NumericPolicy &policy =
        camel::core::sem::NumericPolicy::explicitOnly());

/**
 * `camel inspect --instances`: one line `INSTANCE {json}` per OPER node, of every graph
 * reachable from `root`, whose operator declares index semantics: {"graph", "node", "uri",
 * "key", "instance": the text form as a list of lines (sem::toString), or null when the facts
 * at the node give no instance, "permissions" (the node's, when it has any), "freedom" (what
 * reorderFreedomOf allows, when the instance reduces)}.
 */
void dumpOperatorInstances(const Program &program, std::ostream &os);

} // namespace camel::runtime
