// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The core's kinds of fact, as the facts framework declares them. The first to move is
 * `core.shape`, and it is the shape every later kind is written in: a computation reads the graph
 * through the view the store hands it (`GraphAccess::view`) and reports what it read into
 * `ReadLog`, so the fact is stale when that node's own content changed and not when anything else
 * in the graph did.
 *
 * The value, its text form and its readers stay where they are (`camel/runtime/core_facts.h`):
 * `TensorShape` is the kind's payload in both registries, and a second copy of it would be a second
 * truth about what a shape is. That header is a dependency of a moved kind, not of the framework --
 * a kind that returns this value is the reason for it, and the kinds still living in
 * `src/kernel/runtime/core_facts.cpp` are the reason it is still there.
 */

#include "camel/facts/core_kinds.h"

#include "camel/runtime/core_facts.h"

#include <optional>
#include <span>

namespace camel::facts {

using camel::runtime::fact::TensorShape;

KindId shapeKind() {
    static const KindId id = registerKind<TensorShape>(Kind<TensorShape>{
        .key     = "core.shape",
        .owner   = "core",
        .version = 1,
        // Derived: the shape is what the node's type says, and the type is part of the graph, so a
        // stored answer is correct until the node it is anchored on changes.
        .cls = Class::Derived,
        // One node: a shape is asked of the value one node produces.
        .scope = Scope::Node,
        // Reported: the computation says which node it read, so a change elsewhere in the graph
        // does not invalidate the answer. The runtime kind it replaces said `Dependency::Node`,
        // which was the coarse version of the same claim.
        .dependence = Dependence::Reported,
        // Adding nodes or edges cannot change the shape a node's type states.
        .monotone = false,
        .compute  = [](std::span<const Anchor> anchors, const GraphAccess &graphs, ReadLog &log)
            -> std::optional<TensorShape> {
            if (anchors.size() != 1 || anchors[0].graph == nullptr ||
                anchors[0].node == kWholeGraph || !graphs.view) {
                return std::nullopt;
            }
            const graph::GraphView *view = graphs.view(*anchors[0].graph);
            if (view == nullptr) {
                return std::nullopt;
            }
            const auto ref = static_cast<graph::NodeRef>(anchors[0].node);
            if (!view->contains(ref)) {
                return std::nullopt;
            }
            // What it read: the node's own record -- its type, which is what the shape comes from.
            // A reader that reports nothing is stale as soon as anything the fact is anchored on
            // changes, which would be right but would recompute a shape whenever a neighbour moved.
            log.node(anchors[0].node);
            return camel::runtime::fact::shapeOfType(view->typeOf(ref));
        },
        // No merge, and the reason: a shape is about one value, and there is no shape of a union of
        // two values. A caller that wants the shapes of several nodes asks for each of them.
        // Stating it here is what makes this a cache of a per-node answer rather than a half-stated
        // analysis (design principles red line 4).
        .merge = {},
        // The text the kind prints; `camel inspect --facts` shows this line.
        .print = [](const TensorShape &shape) { return camel::runtime::fact::toString(shape); },
        // No anchorsIn override: the default for a Node kind is every node of the view, and every
        // node of a graph has a type, so every node is a place to ask.
        .anchorsIn = {},
        // No encode/decode yet: their forms live with the runtime kind for now, and a portable form
        // belongs to the step that persists facts, not to the move of the kind.
        .encode = {},
        .decode = {},
    });
    return id;
}

} // namespace camel::facts